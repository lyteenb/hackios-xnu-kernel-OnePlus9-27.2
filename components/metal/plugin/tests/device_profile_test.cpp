// SPDX-License-Identifier: MIT
#include <cerrno>
#include <cstring>

#include "hakios-metal/command.hpp"
#include "hakios-metal/compiler.hpp"
#include "hakios-metal/fake_gpu.hpp"
#include "hakios-metal/hakios_metal.h"
#include "fixtures/fill_id_bc.hpp"
#include "fixtures/tri_flat_vs_bc.hpp"
#include "test.hpp"

using namespace hakios::metal;

namespace {

std::unique_ptr<FakeGpu> fakeA660() {
    return std::make_unique<FakeGpu>(false, 0, true, a6xx::kA660Profile);
}

}  // namespace

TEST(a660_selects_the_board_profile_without_opening_hardware_submission) {
    Gpu gpu(fakeA660());
    CHECK_EQ(gpu.chipId(), 0x06060001u);
    CHECK(gpu.profile() == &a6xx::kA660Profile);
    CHECK(a6xx::profileForChip(0) == &a6xx::kA635Profile);
    CHECK(a6xx::profileForChip(0x06060000u) == nullptr);
    CHECK(hakios_metal_chip_known(0));
    CHECK(hakios_metal_chip_known(0x06030500u));
    CHECK(!hakios_metal_chip_known(0x06060001u));
    CHECK(!gpu.usesVerifiedCommandTables());
    uint64_t value = 0;
    CHECK_EQ(gpu.param(kParamGmemSize, &value), 0);
    CHECK_EQ(value, 1536u * 1024);
    CHECK_EQ(gpu.param(kParamHighestBankBit, &value), 0);
    CHECK_EQ(value, 16u);

    int error = 0;
    auto bo = gpu.newBo(4, 0, &error);
    CHECK(bo != nullptr);
    if (bo == nullptr) return;
    uint32_t fence = 0;
    CHECK_EQ(gpu.submit(0, *bo, 0, 1, {}, &fence), ENOTSUP);
}

TEST(a660_fake_submits_aligned_buffer_commands_and_refuses_graphics) {
    auto fake = fakeA660();
    auto* transport = fake.get();
    Gpu gpu(std::move(fake), Gpu::ProbeMode::kA660Memory);
    std::string why;
    auto queue = CommandQueue::make(gpu, &why);
    auto src = newBuffer(gpu, 16, &why);
    auto dst = newBuffer(gpu, 16, &why);
    CHECK(queue != nullptr && src != nullptr && dst != nullptr);
    if (!queue || !src || !dst) return;

    std::memset(dst->contents(), 0, 16);
    auto cb = queue->commandBuffer();
    CHECK(cb->fill(*src, 4, 8, 0xa5));
    CHECK(cb->copy(*src, 4, *dst, 4, 8));
    cb->commit();
    cb->waitUntilCompleted();
    CHECK(cb->status() == CommandBuffer::kCompleted);
    const uint8_t expected[16] = {0, 0, 0, 0, 0xa5, 0xa5, 0xa5, 0xa5,
                                  0xa5, 0xa5, 0xa5, 0xa5, 0, 0, 0, 0};
    CHECK(std::memcmp(dst->contents(), expected, sizeof(expected)) == 0);
    CHECK_EQ(transport->stats().faults, 0u);

    CHECK(!gpu.canDispatchCompute());
    Gpu computeGpu(fakeA660(), Gpu::ProbeMode::kA660Compute);
    CHECK(computeGpu.canDispatchCompute());
    CHECK(!computeGpu.usesVerifiedCommandTables());

    auto graphics = queue->commandBuffer();
    CHECK(!graphics->clear(RenderPass{}));
    CHECK(graphics->error().find("verified GPU command tables") != std::string::npos);
}

TEST(a660_private_memory_covers_all_three_shader_cores) {
    Gpu gpu(fakeA660(), Gpu::ProbeMode::kA660Compositor);
    std::string why;
    auto queue = CommandQueue::make(gpu, &why);
    CHECK(queue != nullptr);
    if (!queue) return;

    for (bool perWave : {false, true}) {
        PrivateMemory layout{};
        auto scratch = queue->privateMemory(64, perWave, &layout, &why);
        CHECK(scratch != nullptr);
        if (!scratch) return;

        CHECK_EQ(layout.address, scratch->gpuAddress());
        CHECK_EQ(layout.perFiber, 512u);
        CHECK_EQ(layout.perSp, 2u * 1024 * 1024);
        CHECK_EQ(layout.perWave, perWave);
        CHECK_EQ(scratch->length(), 6u * 1024 * 1024);
        CHECK_EQ(scratch->bo().size(), 6u * 1024 * 1024);
    }
}

TEST(a660_gmem_tiling_uses_the_three_ccus_and_96_pixel_alignment) {
    Gpu gpu(fakeA660());
    TextureDesc desc;
    desc.format = PixelFormat::kRGBA8Unorm;
    desc.width = 1024;
    desc.height = 128;
    desc.usage = kTextureUsageRenderTarget;
    std::string why;
    auto texture = Texture::make(gpu, desc, &why);
    CHECK(texture != nullptr);
    if (!texture || gpu.profile() == nullptr) return;

    RenderPass pass;
    pass.color0 = texture.get();
    render::GmemPlan plan;
    CHECK(render::gmemPlan(pass, &plan, &why, ~0u, *gpu.profile()));
    CHECK_EQ(plan.profile.numCcu, 3u);
    CHECK_EQ(plan.cols, 2u);
    CHECK_EQ(plan.binW, 576u);
    CHECK_EQ(plan.room[0], gpu.profile()->gmemCcuColorOffset() & ~0x3fffu);
    CHECK(plan.room[0] > 512u * 1024);
    if (plan.binW == 0) return;

    const uint32_t area[4] = {130, 17, 899, 99};
    CHECK(render::gmemTiles(&plan, area));
    CHECK_EQ(plan.x0, 96u);
    CHECK_EQ(plan.y0, 16u);
}

TEST(a660_sysmem_probe_encodes_the_three_ccu_layout_and_sp_debug_register) {
    Gpu gpu(fakeA660(), Gpu::ProbeMode::kA660Blit);
    CHECK(gpu.canClearAndBlit());
    CHECK(!gpu.canDispatchCompute());
    CHECK(!gpu.canDraw());
    CHECK(!gpu.usesVerifiedCommandTables());
    Gpu drawGpu(fakeA660(), Gpu::ProbeMode::kA660Draw);
    CHECK(drawGpu.canDraw());
    CHECK(!drawGpu.usesVerifiedCommandTables());
    Gpu compositorGpu(fakeA660(), Gpu::ProbeMode::kA660Compositor);
    CHECK(compositorGpu.usesA660CompositorProbe());
    CHECK(compositorGpu.canDraw() && compositorGpu.canClearAndBlit() && compositorGpu.canDispatchCompute());
    CHECK(!compositorGpu.usesVerifiedCommandTables());
    CHECK(!compositorGpu.canUseGmem());
    Gpu gmemGpu(fakeA660(), Gpu::ProbeMode::kA660Gmem);
    CHECK(gmemGpu.canDraw() && gmemGpu.canUseGmem());
    CHECK(!gmemGpu.canClearAndBlit() && !gmemGpu.canDispatchCompute());
    CHECK(!gmemGpu.usesVerifiedCommandTables());
    CHECK_EQ(a6xx::rbCcuCntlGmem(a6xx::kA660Profile.gmemCcuColorOffset()), 0xba400000u);
    TextureDesc desc;
    desc.format = PixelFormat::kRGBA8Unorm;
    desc.width = 8;
    desc.height = 8;
    desc.usage = kTextureUsageRenderTarget;
    std::string why;
    auto texture = Texture::make(gpu, desc, &why);
    CHECK(texture != nullptr);
    if (!texture) return;

    RenderPass pass;
    pass.color0 = texture.get();
    pass.load = LoadAction::kClear;
    uint8_t table[render::kTableBytes] = {};
    pm4::Stream stream;
    CHECK(render::encodeClear(stream, pass, 0x100000000, table, &why, {}, *gpu.profile()));

    bool sp = false, ccu = false;
    const auto& words = stream.words();
    for (size_t i = 0; i < words.size();) {
        const pm4::Header header = pm4::decode(words[i++]);
        if (header.kind == pm4::Header::kBad || header.count > words.size() - i) break;
        if (header.kind == pm4::Header::kRegisters && header.count != 0) {
            if (header.reg == a6xx::REG_A6XX_SP_DBG_ECO_CNTL) {
                CHECK_EQ(words[i], 0x01000000u);
                sp = true;
            }
            if (header.reg == a6xx::REG_A6XX_RB_CCU_CNTL) {
                CHECK_EQ(words[i], a6xx::rbCcuCntlSysmem(3 * 65536));
                ccu = true;
            }
        }
        i += header.count;
    }
    CHECK(sp && ccu);
}

TEST(a660_mesa_catalog_compiles_and_lays_out_resources_for_the_selected_chip) {
#ifdef HAKIOS_METAL_MESA
    Gpu gpu(fakeA660(), Gpu::ProbeMode::kA660Memory);
    const auto* profile = gpu.profile();
    CHECK(profile != nullptr);
    if (profile == nullptr) return;
    CHECK(hasCompiler(a6xx::kChipId));
    CHECK(hasCompiler(profile->chipId));
    CHECK(!hasCompiler(0x06060000u));

    std::string why;
    auto kernel = compileAir(kernels::fill_id_bc, sizeof(kernels::fill_id_bc), &why, {}, profile->chipId);
    CHECK(kernel != nullptr && !kernel->words.empty());
    auto stage = compileStage(kernels::tri_flat_vs_bc, sizeof(kernels::tri_flat_vs_bc),
                              &why, {}, 0, nullptr, profile->chipId);
    CHECK(stage != nullptr && !stage->words.empty());

    TextureDesc desc;
    desc.format = PixelFormat::kRGBA8Unorm;
    desc.width = 128;
    desc.height = 64;
    desc.usage = kTextureUsageRenderTarget;
    desc.compressed = true;
    auto texture = Texture::make(gpu, desc, &why);
    CHECK(texture != nullptr && texture->compressed() && texture->flagPitch() > 0);
#else
    CHECK(!hasCompiler(a6xx::kA660Profile.chipId));
#endif
}
