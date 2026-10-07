// Regression: texture hashes must not read a guest page with no host access.
#include "bbport_memory_hash.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static uintptr_t guest;
static std::vector<unsigned char> backing;
static size_t max_read;
static bool hole;

// Same contract as runtime_memory_read_backing: CPU backing bytes and zero gaps.
extern "C" void runtime_memory_read_backing(uintptr_t address, void* data, uint64_t size) {
    assert(address >= guest && address - guest + size <= backing.size());
    max_read = std::max(max_read, static_cast<size_t>(size));
    auto* out = static_cast<unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        const size_t offset = address - guest + i;
        out[i] = hole && offset >= 4096 && offset < 8192 ? 0 : backing[offset];
    }
}

int main() {
    backing.resize(131072);
    for (size_t i = 0; i < backing.size(); ++i) backing[i] = (i * 37 + i / 251) & 255;
#ifdef _WIN32
    auto* reserved = VirtualAlloc(nullptr, backing.size(), MEM_RESERVE, PAGE_NOACCESS);
#else
    auto* reserved = mmap(nullptr, backing.size(), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(reserved != MAP_FAILED);
#endif
    assert(reserved);
    guest = reinterpret_cast<uintptr_t>(reserved);
    const std::array<size_t, 13> sizes{0, 1, 16, 240, 256, 4096, 8192, 16383, 16384,
                                      16385, 32768, 65537, 131072};
    for (bool sparse : {false, true}) {
        hole = sparse;
        auto expected = backing;
        if (hole) std::fill(expected.begin() + 4096, expected.begin() + 8192, 0);
        for (auto size : sizes) {
            assert(BbMemory::HashBacking(guest, size) == XXH3_64bits(expected.data(), size));
        }
        // The crash address was partway through a page; also cover an unaligned span.
        assert(BbMemory::HashBacking(guest + 0x600, 65537) ==
               XXH3_64bits(expected.data() + 0x600, 65537));
    }
    assert(max_read <= 16384);
#ifdef _WIN32
    VirtualFree(reserved, 0, MEM_RELEASE);
#else
    munmap(reserved, backing.size());
#endif
    std::puts("PASS: protected guest pages, sparse holes, unaligned spans and chunked XXH3 hashes");
}
