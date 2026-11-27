/*
 * Exercise the public Metal API on a physical device. Run with the
 * compositor bundle and metal_procs=metal_api_probe during A660 bring-up.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool
completed(id<MTLCommandBuffer> command, const char *label)
{
	[command commit];
	[command waitUntilCompleted];
	if (command.status == MTLCommandBufferStatusCompleted) {
		return true;
	}
	printf("metal_api_probe: %s failed status=%lu error=%s\n", label, (unsigned long)command.status,
	    command.error ? command.error.localizedDescription.UTF8String : "-");
	return false;
}

static bool
check_copy(id<MTLDevice> device, id<MTLCommandQueue> queue)
{
	const size_t words = 32;
	const uint32_t guard = 0xa5a5a5a5;
	id<MTLBuffer> source = [device newBufferWithLength:words * sizeof(uint32_t) options:MTLResourceStorageModeShared];
	id<MTLBuffer> destination = [device newBufferWithLength:words * sizeof(uint32_t) options:MTLResourceStorageModeShared];
	id<MTLCommandBuffer> command = [queue commandBuffer];
	id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
	uint32_t *input = source.contents, *output = destination.contents;

	if (!input || !output || !command || !encoder) {
		if (encoder) {
			[encoder endEncoding];
		}
		printf("metal_api_probe: copy resources unavailable\n");
		return false;
	}
	for (size_t i = 0; i < words; i++) {
		input[i] = 0x8c00f000u ^ (uint32_t)(i * 39);
		output[i] = guard;
	}
	[encoder copyFromBuffer:source sourceOffset:4 * sizeof(uint32_t)
	    toBuffer:destination destinationOffset:8 * sizeof(uint32_t) size:12 * sizeof(uint32_t)];
	[encoder endEncoding];
	if (!completed(command, "blit")) {
		return false;
	}
	size_t correct = 0;
	for (size_t i = 0; i < words; i++) {
		const uint32_t expected = i >= 8 && i < 20 ? input[i - 4] : guard;
		correct += output[i] == expected;
	}
	printf("metal_api_probe: blit %zu/%zu words match\n", correct, words);
	return correct == words;
}

static id<MTLLibrary>
open_library(id<MTLDevice> device, const char *path)
{
	NSError *error = nil;
	NSString *file = [NSString stringWithUTF8String:path];
	if (!file) {
		puts("metal_api_probe: invalid library path");
		return nil;
	}
	id<MTLLibrary> library = [device newLibraryWithURL:[NSURL fileURLWithPath:file] error:&error];
	if (!library) {
		printf("metal_api_probe: library failed: %s\n", error.localizedDescription.UTF8String ?: "-");
	}
	return library;
}

static id<MTLRenderPipelineState>
triangle_pipeline(id<MTLDevice> device, id<MTLLibrary> library)
{
	NSError *error = nil;
	MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
	descriptor.vertexFunction = [library newFunctionWithName:@"rv"];
	descriptor.fragmentFunction = [library newFunctionWithName:@"rf"];
	descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	if (!descriptor.vertexFunction || !descriptor.fragmentFunction) {
		puts("metal_api_probe: triangle shader functions unavailable");
		return nil;
	}
	id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
	if (!pipeline) {
		printf("metal_api_probe: triangle pipeline failed: %s\n", error.localizedDescription.UTF8String ?: "-");
	}
	return pipeline;
}

static bool
check_render(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLRenderPipelineState> pipeline)
{
	enum { width = 64, height = 64 };
	const char *label = pipeline ? "triangle" : "clear";
	MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
	    width:width height:height mipmapped:NO];
	descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	descriptor.storageMode = MTLStorageModeShared;
	id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
	id<MTLCommandBuffer> command = [queue commandBuffer];
	if (!texture || !command) {
		printf("metal_api_probe: %s resources unavailable\n", label);
		return false;
	}

	id<MTLBuffer> vertices = nil;
	if (pipeline) {
		/* The shared AIR fixture declares room for six vertices at buffer index 0. */
		const float corners[6][4] = {
		    {-0.5f, -0.5f, 0.5f, 1.0f}, {0.5f, -0.5f, 0.5f, 1.0f}, {0.0f, 0.5f, 0.5f, 1.0f},
		};
		vertices = [device newBufferWithBytes:corners length:sizeof(corners) options:MTLResourceStorageModeShared];
		if (!vertices) {
			puts("metal_api_probe: triangle vertex buffer unavailable");
			return false;
		}
	}

	MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
	MTLRenderPassColorAttachmentDescriptor *color = pass.colorAttachments[0];
	color.texture = texture;
	color.loadAction = MTLLoadActionClear;
	color.storeAction = MTLStoreActionStore;
	color.clearColor = MTLClearColorMake(0.25, 0.5, 0.75, 1.0);
	id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
	if (!encoder) {
		printf("metal_api_probe: render encoder unavailable\n");
		return false;
	}
	if (pipeline) {
		[encoder setCullMode:MTLCullModeNone];
		[encoder setRenderPipelineState:pipeline];
		[encoder setVertexBuffer:vertices offset:0 atIndex:0];
		[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	}
	[encoder endEncoding];
	if (!completed(command, label)) {
		return false;
	}

	uint8_t pixels[height][width][4] = {0};
	[texture getBytes:pixels bytesPerRow:sizeof(pixels[0])
	    fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
	unsigned correct = 0, red = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const uint8_t *p = pixels[y][x];
			/* Screen vertices (16,48), (48,48), (32,16). Compare at pixel centers. */
			bool inside = pipeline && y < 48 && 2 * abs(2 * x + 1 - 64) < 2 * y + 1 - 32;
			bool is_red = p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255;
			bool is_clear = p[0] == 191 && p[1] == 128 && p[2] == 64 && p[3] == 255;
			red += is_red;
			correct += inside ? is_red : is_clear;
		}
	}
	printf("metal_api_probe: %s %u/%u BGRA pixels match; red=%u; first=%u,%u,%u,%u\n",
	    label, correct, width * height, red, pixels[0][0][0], pixels[0][0][1], pixels[0][0][2], pixels[0][0][3]);
	return correct == width * height;
}

static bool
check_compute(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLLibrary> library)
{
	enum { invocations = 64, words = 1024 };
	const uint32_t initial = 0xa5a5a5a5u, seed = 0x12345678u;
	id<MTLFunction> function = [library newFunctionWithName:@"fill_id"];
	if (!function) {
		puts("metal_api_probe: fill_id shader unavailable");
		return false;
	}
	NSError *error = nil;
	id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
	if (!pipeline) {
		printf("metal_api_probe: compute pipeline failed: %s\n", error.localizedDescription.UTF8String ?: "-");
		return false;
	}
	id<MTLBuffer> output = [device newBufferWithLength:words * sizeof(uint32_t) options:MTLResourceStorageModeShared];
	id<MTLBuffer> constants = [device newBufferWithBytes:&seed length:sizeof(seed) options:MTLResourceStorageModeShared];
	id<MTLCommandBuffer> command = [queue commandBuffer];
	uint32_t *values = output.contents;
	if (!values || !constants || !command) {
		puts("metal_api_probe: compute resources unavailable");
		return false;
	}
	for (size_t i = 0; i < words; i++) {
		values[i] = initial;
	}
	id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
	if (!encoder) {
		puts("metal_api_probe: compute encoder unavailable");
		return false;
	}
	[encoder setComputePipelineState:pipeline];
	[encoder setBuffer:output offset:0 atIndex:0];
	[encoder setBuffer:constants offset:0 atIndex:1];
	[encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(invocations, 1, 1)];
	[encoder endEncoding];
	if (!completed(command, "compute")) {
		return false;
	}

	unsigned matched = 0, guards = 0;
	for (uint32_t i = 0; i < invocations; i++) {
		matched += values[i] == i * 0x9e3779b1u + seed;
	}
	for (size_t i = invocations; i < words; i++) {
		guards += values[i] == initial;
	}
	printf("metal_api_probe: compute %u/%u outputs match; %u/%u guard words match\n",
	    matched, invocations, guards, words - invocations);
	return matched == invocations && guards == words - invocations;
}

int
main(int argc, const char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc > 3 || (argc == 3 && strcmp(argv[2], "--compute") != 0)) {
		puts("usage: metal_api_probe [triangle.metallib [--compute]]");
		return 2;
	}
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (!device) {
			puts("metal_api_probe: no Metal device");
			return 1;
		}
		printf("metal_api_probe: device=%s\n", device.name.UTF8String ?: "-");
		id<MTLCommandQueue> queue = [device newCommandQueue];
		if (!queue) {
			puts("metal_api_probe: no command queue");
			return 1;
		}
		if (!check_copy(device, queue) || !check_render(device, queue, nil)) {
			return 1;
		}
		if (argc >= 2) {
			id<MTLLibrary> library = open_library(device, argv[1]);
			if (!library) {
				return 1;
			}
			id<MTLRenderPipelineState> triangle = triangle_pipeline(device, library);
			if (!triangle || !check_render(device, queue, triangle) ||
			    (argc == 3 && !check_compute(device, queue, library))) {
				return 1;
			}
		}
		puts("metal_api_probe: PASS");
		return 0;
	}
}
