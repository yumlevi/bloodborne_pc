#include "win32_memory.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    const uint64_t section_size=UINT64_C(8)*1024*1024*1024;
    const uintptr_t guest=UINT64_C(0x0800000000);
    const uint64_t page=16384;
    void *backing=NULL;
    assert(win_mem_space(guest,guest+page)==0);
    assert(win_mem_section(section_size,&backing)==0);
    assert(win_mem_commit(page,page)==0);
    ((uint32_t *)((unsigned char *)backing+page))[0]=0x12345678u;
    assert(win_mem_map(guest,guest+page,page,3,NULL)==0);
    assert(*(volatile uint32_t *)guest==0x12345678u);
    *(volatile uint32_t *)guest=0x87654321u;
    assert(((uint32_t *)((unsigned char *)backing+page))[0]==0x87654321u);
    puts("PASS: Windows memory pool reserves a large section and commits pages on demand");
    return 0;
}
