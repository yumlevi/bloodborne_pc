/* Mouse camera: the captured mouse turns the game camera directly, as imedved's Cheat Engine
 * table for shadPS4 (shadPS4BBMouseandFOV, Bloodborne v1.09) does. Four stores of the camera
 * update stop writing its angles, and a hook records the camera object (r13) on every update;
 * the pad thread then writes pitch and yaw from the mouse motion. The code is patched only while
 * the mouse drives the camera: right stick input (a pad, or the camera keys) restores it, so the
 * game's own camera works as before. Lock-on keeps the game's aim; the spyglass has its own
 * angles. Offsets are image offsets (PS4 address - 0x400000, shadPS4 address - 0x800000000). */
#define _GNU_SOURCE
#include "runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define HOOK_SITE 0x143CEAAu      /* vmovss xmm0,[r13+140]; vmovss xmm1,[r13+150] */
#define SPYGLASS_ROOT 0x553E8D0u  /* [[root]+68]+84 != 0: spyglass up; [[[root]+68]+68]+148/14C: its pitch/yaw */
/* Camera object fields. */
#define CAM_MARKER 0x130          /* the hook writes 0.8 here (the game's store is removed): a live camera */
#define CAM_PITCH_AIMED 0x140     /* pitch after lock-on */
#define CAM_YAW 0x144
#define CAM_PITCH 0x150
#define CAM_LOCK_ON 0x154         /* 1.0 while locked on */
#define CAM_YAW_AIMED 0x270       /* yaw after lock-on */
#define MARKER 0.8f
#define PITCH_MIN (-1.94f)
#define PITCH_MAX 1.71f
#define PI_F 3.14159265f
#define CAMERA_GONE_NS 250000000ull /* no camera update for this long: loading, cutscene, menu */

static const uint8_t hook_original[18]={0xC4,0xC1,0x7A,0x10,0x85,0x40,0x01,0x00,0x00,
                                        0xC4,0xC1,0x7A,0x10,0x8D,0x50,0x01,0x00,0x00};
static const struct { uint32_t offset; uint8_t original[9]; } stores[]={
    {0x143C6E8u,{0xC4,0xC1,0x7A,0x11,0x85,0x44,0x01,0x00,0x00}}, /* vmovss [r13+144],xmm0 (yaw) */
    {0x143C870u,{0xC4,0xC1,0x7A,0x11,0xAD,0x44,0x01,0x00,0x00}}, /* vmovss [r13+144],xmm5 (yaw) */
    {0x143C984u,{0xC4,0xC1,0x7A,0x11,0x8D,0x30,0x01,0x00,0x00}}, /* vmovss [r13+130],xmm1 */
    {0x143DDE6u,{0xC4,0xC1,0x7A,0x11,0x85,0x94,0x02,0x00,0x00}}, /* vmovss [r13+294],xmm0 */
};

static unsigned char *image;
static uint8_t *trampoline;      /* executable: the hook's code, then the camera slot */
static uintptr_t *slot;          /* r13 of the latest camera update, taken by the pad thread */
static int supported, engaged;
static uintptr_t camera;
static uint64_t camera_seen;
static int have_angles, locked;
static float pitch, yaw;

/* Rewrites guest code (the image's code pages are read+execute). */
static int write_code(uint32_t offset, const void *bytes, size_t size) {
    unsigned char *p=image+offset;
    uintptr_t page=(uintptr_t)p&~(uintptr_t)4095, end=((uintptr_t)p+size+4095)&~(uintptr_t)4095;
#ifdef _WIN32
    DWORD old;
    if (!VirtualProtect((void *)page,end-page,PAGE_EXECUTE_READWRITE,&old)) return -1;
    memcpy(p,bytes,size);
    VirtualProtect((void *)page,end-page,old,&old);
    FlushInstructionCache(GetCurrentProcess(),p,size);
#else
    if (mprotect((void *)page,end-page,PROT_READ|PROT_WRITE|PROT_EXEC)) return -1;
    memcpy(p,bytes,size);
    mprotect((void *)page,end-page,PROT_READ|PROT_EXEC);
#endif
    return 0;
}

void runtime_camera_configure(unsigned char *base, uint64_t size) {
    image=base;
    if (size<SPYGLASS_ROOT+8 || memcmp(image+HOOK_SITE,hook_original,sizeof(hook_original))) {
        printf("Mouse camera: unavailable (not the v1.09 camera code at 0x%x)\n",HOOK_SITE);
        return;
    }
    for (size_t i=0;i<sizeof(stores)/sizeof(*stores);++i) {
        if (memcmp(image+stores[i].offset,stores[i].original,9)) {
            printf("Mouse camera: unavailable (not the v1.09 camera code at 0x%x)\n",stores[i].offset);
            return;
        }
    }
#ifdef _WIN32
    trampoline=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
#else
    trampoline=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (trampoline==MAP_FAILED) trampoline=NULL;
#endif
    if (!trampoline) { puts("Mouse camera: unavailable (no executable memory)"); return; }
    /* The two loads it replaces, the marker, r13 into the slot (offset 56), back after the hook. */
    static const uint8_t code[]={
        0xC4,0xC1,0x7A,0x10,0x85,0x40,0x01,0x00,0x00,             /*  0 vmovss xmm0,[r13+140]  */
        0xC4,0xC1,0x7A,0x10,0x8D,0x50,0x01,0x00,0x00,             /*  9 vmovss xmm1,[r13+150]  */
        0x41,0xC7,0x85,0x30,0x01,0x00,0x00,0xCD,0xCC,0x4C,0x3F,   /* 18 mov dword [r13+130],0.8 */
        0x4C,0x89,0x2D,0x14,0x00,0x00,0x00,                       /* 29 mov [rip+20],r13       */
        0xFF,0x25,0x00,0x00,0x00,0x00,                            /* 36 jmp [rip+0]            */
    };
    memcpy(trampoline,code,sizeof(code));
    const uint64_t back=(uint64_t)(uintptr_t)(image+HOOK_SITE+sizeof(hook_original));
    memcpy(trampoline+42,&back,8);
    slot=(uintptr_t *)(trampoline+56);
    *slot=0;
    supported=1;
    printf("Mouse camera: ready (F1 releases the mouse; the right stick hands the camera back)\n");
}

static void engage(void) {
    uint8_t hook[18]={0xFF,0x25,0x00,0x00,0x00,0x00};                 /* jmp [rip+0]; trampoline */
    const uint64_t target=(uint64_t)(uintptr_t)trampoline;
    memcpy(hook+6,&target,8);
    memset(hook+14,0x90,4);
    static const uint8_t nops[9]={0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    __atomic_store_n(slot,0,__ATOMIC_RELEASE);
    for (size_t i=0;i<sizeof(stores)/sizeof(*stores);++i) write_code(stores[i].offset,nops,9);
    write_code(HOOK_SITE,hook,sizeof(hook));
    camera=0; have_angles=0; locked=0; engaged=1;
    puts("Mouse camera: the mouse turns the camera");
}

static void disengage(const char *why) {
    write_code(HOOK_SITE,hook_original,sizeof(hook_original));
    for (size_t i=0;i<sizeof(stores)/sizeof(*stores);++i) write_code(stores[i].offset,stores[i].original,9);
    camera=0; engaged=0;
    printf("Mouse camera: the game's camera (%s)\n",why);
}

static float read_f32(uintptr_t a) { float v; memcpy(&v,(const void *)a,4); return v; }
static int32_t read_i32(uintptr_t a) { int32_t v; memcpy(&v,(const void *)a,4); return v; }
static uintptr_t read_ptr(uintptr_t a) { uintptr_t v; memcpy(&v,(const void *)a,8); return v; }
static void write_f32(uintptr_t a, float v) { memcpy((void *)a,&v,4); }

/* One camera update from the mouse motion; 0 when the object is not the live camera. Its guest
 * reads may fault (a stale object or spyglass chain): the caller recovers. */
static int apply(float dx, float dy, float sensitivity, int invert_y) {
    if (fabsf(read_f32(camera+CAM_MARKER)-MARKER)>0.001f) return 0;
    if (read_f32(camera+CAM_LOCK_ON)==1.0f) { locked=1; return 1; } /* the game aims at the target */
    if (locked) {
        pitch=read_f32(camera+CAM_PITCH_AIMED); yaw=read_f32(camera+CAM_YAW_AIMED);
        locked=0; have_angles=1;
    }
    if (!have_angles) { pitch=read_f32(camera+CAM_PITCH); yaw=read_f32(camera+CAM_YAW); have_angles=1; }
    uintptr_t pitch_at=camera+CAM_PITCH, yaw_at=camera+CAM_YAW;
    const uintptr_t root=read_ptr((uintptr_t)image+SPYGLASS_ROOT);
    if (root) {
        const uintptr_t view=read_ptr(root+0x68);
        if (view && read_i32(view+0x84)) {
            const uintptr_t spyglass=read_ptr(view+0x68);
            if (spyglass) { pitch_at=spyglass+0x148; yaw_at=spyglass+0x14C; }
        }
    }
    pitch+=(invert_y ? -dy : dy)*sensitivity;
    pitch=pitch<PITCH_MIN ? PITCH_MIN : pitch>PITCH_MAX ? PITCH_MAX : pitch;
    yaw+=dx*sensitivity;
    if (yaw>PI_F) yaw-=2.0f*PI_F;
    else if (yaw<-PI_F) yaw+=2.0f*PI_F;
    write_f32(pitch_at,pitch);
    write_f32(yaw_at,yaw);
    return 1;
}

void runtime_camera_update(float dx, float dy, int captured, float sensitivity, int invert_y, int stick_camera) {
    if (!supported) return;
    if (!captured || stick_camera) {
        if (engaged) disengage(captured ? "right stick" : "mouse released: menu, F1 or another window");
        return;
    }
    if (!engaged) {
        if (fabsf(dx)+fabsf(dy)<2.0f) return; /* sensor noise does not take the camera from the stick */
        engage();
        return;
    }
    const uintptr_t fresh=__atomic_exchange_n(slot,0,__ATOMIC_ACQ_REL);
    const uint64_t now=host_monotonic_ns();
    if (fresh) {
        if (fresh!=camera) { camera=fresh; have_angles=0; locked=0; }
        camera_seen=now;
    } else if (!camera || now-camera_seen>CAMERA_GONE_NS) {
        camera=0;
        return;
    }
    RuntimeRecoverBuf recover;
    if (RUNTIME_RECOVER_SET(recover)) { /* a guest read faulted: the camera object is gone */
        runtime_fault_recover=NULL;
        camera=0; have_angles=0;
        return;
    }
    runtime_fault_recover=&recover;
    const int live=apply(dx,dy,sensitivity,invert_y);
    runtime_fault_recover=NULL;
    if (!live) have_angles=0;
}
