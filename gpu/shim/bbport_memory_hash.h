// bbport: hash CPU bytes through the runtime's unprotected backing view. Texture
// descriptors may span unmapped/reserved pages, which sparse uploads read as zero.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <new>
#include <xxhash.h>

extern "C" void runtime_memory_read_backing(uintptr_t address, void* data, uint64_t size);

namespace BbMemory {
inline uint64_t HashBacking(uintptr_t address, uint64_t size) {
    // Bound scratch space even for large mip levels. Never dereference the guest
    // pointer: it can be unmapped or read-protected while a draw is recorded.
    std::array<unsigned char, 16384> bytes;
    if (size <= bytes.size()) {
        runtime_memory_read_backing(address, bytes.data(), size);
        return XXH3_64bits(bytes.data(), static_cast<size_t>(size));
    }
    std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(),
                                                               &XXH3_freeState);
    if (!state) throw std::bad_alloc{};
    XXH3_64bits_reset(state.get());
    while (size) {
        const auto count = std::min<uint64_t>(size, bytes.size());
        runtime_memory_read_backing(address, bytes.data(), count);
        XXH3_64bits_update(state.get(), bytes.data(), static_cast<size_t>(count));
        address += count;
        size -= count;
    }
    return XXH3_64bits_digest(state.get());
}
} // namespace BbMemory
