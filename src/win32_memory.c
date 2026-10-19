/* Windows guest address space. The guest range is reserved at start as one placeholder
 * (VirtualAlloc2); a mapping splits the placeholder and replaces its piece with a view of
 * the pool section (MapViewOfFile3), which allows 4 KiB granularity and aliasing like
 * mmap(MAP_SHARED|MAP_FIXED) of a memfd. Windows cannot unmap part of a view, so views are
 * tracked here: a removed range unmaps the views it touches and maps their remaining pieces
 * again (the section keeps the data). */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32_memory.h"

typedef PVOID (WINAPI *VirtualAlloc2Fn)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef PVOID (WINAPI *MapViewOfFile3Fn)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef BOOL (WINAPI *UnmapViewOfFile2Fn)(HANDLE, PVOID, ULONG);
static VirtualAlloc2Fn virtual_alloc2;
static MapViewOfFile3Fn map_view3;
static UnmapViewOfFile2Fn unmap_view2;
static HANDLE section;
static unsigned char *section_backing;
static uint64_t section_size;
static uintptr_t space_start, space_end;
typedef struct { uintptr_t start, end; uint64_t phys; } View;
static View *views;
static size_t view_count, view_capacity;

static void report(const char *what, uintptr_t a, uintptr_t b) {
    fprintf(stderr, "Runtime: %s [%#llx, %#llx) failed: Windows error %lu\n", what,
            (unsigned long long)a, (unsigned long long)b, GetLastError());
}
int win_mem_space(uintptr_t start, uintptr_t end) {
    if (space_end) return 0;
    HMODULE kernel = GetModuleHandleW(L"kernelbase.dll");
    virtual_alloc2 = kernel ? (VirtualAlloc2Fn)(void *)GetProcAddress(kernel, "VirtualAlloc2") : NULL;
    map_view3 = kernel ? (MapViewOfFile3Fn)(void *)GetProcAddress(kernel, "MapViewOfFile3") : NULL;
    unmap_view2 = kernel ? (UnmapViewOfFile2Fn)(void *)GetProcAddress(kernel, "UnmapViewOfFile2") : NULL;
    if (!virtual_alloc2 || !map_view3 || !unmap_view2) {
        fputs("STOP: Windows 10 1803 or newer is required (placeholder memory API)\n", stderr);
        exit(21);
    }
    if (virtual_alloc2(GetCurrentProcess(), (void *)start, end - start, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                       PAGE_NOACCESS, NULL, 0) != (void *)start) {
        report("reserving the guest address space", start, end);
        return -1;
    }
    space_start = start; space_end = end;
    return 0;
}
int win_mem_section(uint64_t size, void **backing) {
    /* SEC_COMMIT charges the full 5-10 GiB guest pool against the Windows commit
     * limit at startup.  Reserve it instead and commit physical ranges when the
     * guest allocates them.  Aliased section views see the same committed pages. */
    section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_EXECUTE_READWRITE | SEC_RESERVE,
                                 (DWORD)(size >> 32), (DWORD)size, NULL);
    if (!section) { report("creating the memory pool section", 0, size); return -1; }
    *backing = MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!*backing) { report("mapping the memory pool", 0, size); return -1; }
    section_backing=*backing; section_size=size;
    return 0;
}
int win_mem_commit(uint64_t offset, uint64_t size) {
    if (!section_backing || offset>section_size || size>section_size-offset) {
        SetLastError(ERROR_INVALID_PARAMETER); return -1;
    }
    void *address=section_backing+offset;
    if (VirtualAlloc(address,(SIZE_T)size,MEM_COMMIT,PAGE_READWRITE)!=address) {
        report("committing memory pool pages",(uintptr_t)offset,(uintptr_t)(offset+size));
        return -1;
    }
    return 0;
}
static DWORD page_mode(int prot) {
    switch (prot & 7) {
    case 0: return PAGE_NOACCESS;
    case 1: return PAGE_READONLY;
    case 2: case 3: return PAGE_READWRITE;
    case 4: return PAGE_EXECUTE;
    case 5: return PAGE_EXECUTE_READ;
    default: return PAGE_EXECUTE_READWRITE;
    }
}
static size_t view_index(uintptr_t a) { /* first view with end > a */
    size_t lo = 0, hi = view_count;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (views[mid].end <= a) lo = mid + 1; else hi = mid; }
    return lo;
}
static int view_insert(View v) {
    if (view_count == view_capacity) {
        size_t capacity = view_capacity ? view_capacity * 2 : 256;
        View *next = realloc(views, capacity * sizeof(*views));
        if (!next) return -1;
        views = next; view_capacity = capacity;
    }
    size_t at = view_index(v.start);
    memmove(views + at + 1, views + at, (view_count - at) * sizeof(*views));
    views[at] = v; ++view_count;
    return 0;
}
static void view_erase(size_t at) {
    memmove(views + at, views + at + 1, (view_count - at - 1) * sizeof(*views));
    --view_count;
}
/* Makes x a placeholder boundary. Placeholder pieces report their own AllocationBase. */
static int split_at(uintptr_t x) {
    if (x == space_start || x == space_end) return 0;
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery((void *)x, &info, sizeof(info))) return -1;
    if ((uintptr_t)info.AllocationBase == x) return 0;
    if (info.State != MEM_RESERVE) { SetLastError(ERROR_INVALID_ADDRESS); return -1; }
    return VirtualFree((void *)x, info.RegionSize, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) ? 0 : -1;
}
/* [a,b) holds placeholders only: make it exactly one placeholder. */
static int make_placeholder(uintptr_t a, uintptr_t b) {
    if (split_at(a) || split_at(b)) return -1;
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery((void *)a, &info, sizeof(info))) return -1;
    if (info.RegionSize < b - a && !VirtualFree((void *)a, b - a, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS)) return -1;
    return 0;
}
static int map_view(uintptr_t a, uintptr_t b, uint64_t phys) {
    if (make_placeholder(a, b)) return -1;
    if (map_view3(section, GetCurrentProcess(), (void *)a, phys, b - a, MEM_REPLACE_PLACEHOLDER,
                  PAGE_READWRITE, NULL, 0) != (void *)a) return -1;
    return view_insert((View){a, b, phys});
}
static int inside(uintptr_t a, uintptr_t b) { return a < b && a >= space_start && b <= space_end; }
int win_mem_release(uintptr_t a, uintptr_t b, WinMemRestore restore) {
    if (!inside(a, b)) { SetLastError(ERROR_INVALID_ADDRESS); report("releasing", a, b); return -1; }
    for (size_t i = view_index(a); i < view_count && views[i].start < b;) {
        View v = views[i];
        if (!unmap_view2(GetCurrentProcess(), (void *)v.start, MEM_PRESERVE_PLACEHOLDER)) { report("unmapping", v.start, v.end); return -1; }
        view_erase(i);
        if (v.start < a) {
            if (map_view(v.start, a, v.phys)) { report("remapping", v.start, a); return -1; }
            if (restore) restore(v.start, a);
            ++i;
        }
        if (v.end > b) {
            if (map_view(b, v.end, v.phys + (b - v.start))) { report("remapping", b, v.end); return -1; }
            if (restore) restore(b, v.end);
        }
    }
    if (make_placeholder(a, b)) { report("reserving", a, b); return -1; }
    return 0;
}
int win_mem_map(uintptr_t a, uintptr_t b, uint64_t phys, int prot, WinMemRestore restore) {
    if (win_mem_release(a, b, restore)) return -1;
    if (map_view(a, b, phys)) { report("mapping", a, b); return -1; }
    return (prot & 7) == 3 ? 0 : win_mem_protect(a, b, prot);
}
/* VirtualProtect cannot cross views: one call per view. */
int win_mem_protect(uintptr_t a, uintptr_t b, int prot) {
    DWORD mode = page_mode(prot), old;
    for (size_t i = view_index(a); i < view_count && views[i].start < b; ++i) {
        uintptr_t start = views[i].start > a ? views[i].start : a, end = views[i].end < b ? views[i].end : b;
        if (!VirtualProtect((void *)start, end - start, mode, &old)) { report("protecting", start, end); return -1; }
    }
    return 0;
}
void *win_mem_private(uintptr_t a, uintptr_t b) {
    if (!inside(a, b)) return NULL;
    MEMORY_BASIC_INFORMATION info;
    /* Taken pieces (earlier private allocations, views) are skipped by the caller's search. */
    for (uintptr_t at = a; at < b; at = (uintptr_t)info.BaseAddress + info.RegionSize)
        if (!VirtualQuery((void *)at, &info, sizeof(info)) || info.State != MEM_RESERVE) return NULL;
    if (make_placeholder(a, b)) return NULL;
    return virtual_alloc2(GetCurrentProcess(), (void *)a, b - a, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                          PAGE_READWRITE, NULL, 0);
}
#endif
