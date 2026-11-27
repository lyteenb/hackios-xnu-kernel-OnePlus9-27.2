#pragma once

#include "hakios-metal/a6xx.hpp"

namespace hakios::metal::a6xx {

struct DeviceProfile {
    uint32_t chipId;
    uint32_t mesaGpuId;
    uint32_t gmemBytes;
    uint32_t highestBankBit;
    uint32_t numCcu;
    uint32_t fibersPerSp;
    uint32_t tileAlignW;
    uint32_t tileAlignH;
    uint32_t numVscPipes;
    uint32_t spDbgEcoCntl;
    bool hardwareEnabled;

    constexpr uint32_t sysmemCcuColorOffset() const {
        return numCcu * kSysmemPerCcuDepthCacheSize;
    }

    constexpr uint32_t gmemCcuColorOffset() const {
        return gmemBytes - numCcu * kSysmemPerCcuColorCacheSize / kGmemCcuColorCacheFraction;
    }
};

inline constexpr DeviceProfile kA635Profile{
    kChipId, 0, kGmemBytes, 14, kNumCcu, 128 * 2 * 16, kTileAlignW, kTileAlignH, kNumVscPipes, kMagicSpDbgEcoCntl, true
};

// The board chip ID, GMEM and highest bank bit come from lemonade/board.dts and
// the 16.5 driver. Mesa 25.0.7's freedreno_devices.py lists the rest in GPUId(660).
// Mesa uses that numeric GPU ID for the family; the kernel reports the silicon revision.
inline constexpr DeviceProfile kA660Profile{
    0x06060001, 660, 1536 * 1024, 16, 3, 128 * 2 * 16, 96, 16, 32, 0x01000000, false
};

inline constexpr const DeviceProfile* profileForChip(uint32_t chipId) {
    switch (chipId) {
        case 0: // Preserve the legacy A635 behavior for kernels without a chip ID.
        case kA635Profile.chipId: return &kA635Profile;
        case kA660Profile.chipId: return &kA660Profile;
        default: return nullptr;
    }
}

}  // namespace hakios::metal::a6xx
