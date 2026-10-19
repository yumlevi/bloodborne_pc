// bbport: the MemoryManager surface used by the video core and GnmDriver.
// Guest memory lives at identical host addresses; the C runtime owns the VMAs.
#pragma once
#include "common/types.h"
#include "common/singleton.h"
#include "core/address_space.h"

namespace Vulkan { class Rasterizer; }
extern "C" int runtime_memory_is_mapped(uintptr_t address, uint64_t size);

namespace Core {
class MemoryManager {
public:
    void SetRasterizer(Vulkan::Rasterizer* rasterizer_);
    AddressSpace& GetAddressSpace() { return impl; }
    bool IsValidGpuMapping(VAddr virtual_addr, u64 size) {
        // The PS4's GPU can only handle 40-bit addresses. A zero size is used by callers as
        // "validate this address", so probe one byte instead of accepting a reserved VMA.
        const u64 probe_size = size ? size : 1;
        return virtual_addr < 0x100'0000'0000ULL &&
               probe_size <= 0x100'0000'0000ULL - virtual_addr &&
               runtime_memory_is_mapped(virtual_addr, probe_size) != 0;
    }
    u64 ClampRangeSize(VAddr virtual_addr, u64 size);
    void CopySparseMemory(VAddr source, u8* dest, u64 size);
    /// Reads through the always-accessible backing view. Reserved/unmapped pieces become zero,
    /// and the runtime's VMA lock prevents a concurrent unmap from invalidating the copy.
    void ReadGuestMemory(VAddr source, void* dest, u64 size);
    bool TryWriteBacking(void* address, const void* data, u64 size);
    /// CPU wrote guest memory outside page tracking (e.g. decoded video frames).
    void InvalidateMemory(VAddr address, u64 size);
    Vulkan::Rasterizer* GetRasterizer() const { return rasterizer; }

private:
    AddressSpace impl;
    Vulkan::Rasterizer* rasterizer{};
};
using Memory = Common::Singleton<MemoryManager>;
} // namespace Core
