#ifndef BB_RUNTIME_H
#define BB_RUNTIME_H
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include "win32_compat.h"
#endif
#include "host_sync.h"
#ifdef _WIN32
/* runtime_setjmp/runtime_longjmp (runtime_host.c): every Win64 callee-saved register, and no
 * SEH unwinding, which cannot pass the guest frames in between (they have no unwind data). */
typedef struct { _Alignas(16) unsigned char registers[256]; } RuntimeRecoverBuf;
int runtime_setjmp(RuntimeRecoverBuf *buf) __attribute__((returns_twice));
__attribute__((noreturn)) void runtime_longjmp(RuntimeRecoverBuf *buf);
#define RUNTIME_RECOVER_SET(buf) runtime_setjmp(&(buf))
#define RUNTIME_RECOVER_JUMP(buf) runtime_longjmp(&(buf))
#else
#include <setjmp.h>
typedef sigjmp_buf RuntimeRecoverBuf;
#define RUNTIME_RECOVER_SET(buf) sigsetjmp(buf, 0)
#define RUNTIME_RECOVER_JUMP(buf) siglongjmp(buf, 1)
#endif
/* Recovery point for speculative guest memory reads on this thread (probe.c fault handler). */
extern __thread RuntimeRecoverBuf *runtime_fault_recover;
/* Restarts the game (in-game settings menu, render resolution change). */
void runtime_restart(void);
#define ABI __attribute__((sysv_abi))
typedef void (ABI *GuestCallback)(void);
void runtime_start(uint64_t capabilities);
uintptr_t runtime_resolve(const char *name, int is_data);
void runtime_report(void);
void runtime_finalize(void *dso);
uintptr_t runtime_mutex_resolve(const char *name);
void runtime_mutex_report(void);
uintptr_t runtime_memory_resolve(const char *name);
void runtime_memory_report(void);
int runtime_memory_is_mapped(uintptr_t address, uint64_t size);
typedef struct { uintptr_t start; uint64_t size; const unsigned char *data; int type; } RuntimeView;
size_t runtime_memory_views(RuntimeView *out, size_t max);
const char *runtime_import_name(const char *name);
uintptr_t runtime_rwlock_resolve(const char *name);
void runtime_rwlock_report(void);
void runtime_set_libc_tls(const void *data, uint64_t filesz, uint64_t memsz);
void runtime_set_module_tls(uint64_t module, const void *data, uint64_t filesz, uint64_t memsz);
void runtime_set_procparam(void *param);
void **runtime_application_heap_api(void);
uintptr_t runtime_thread_resolve(const char *name);
void runtime_thread_report(void);
void runtime_set_main_tls(const void *data, uint64_t filesz, uint64_t memsz, uint64_t align);
void runtime_thread_attach_main(void);
int32_t *runtime_errno(void);
uintptr_t runtime_sema_resolve(const char *name);
void runtime_sema_report(void);
unsigned runtime_sema_waiters(uint32_t id);
uintptr_t runtime_time_resolve(const char *name);
void runtime_content_configure(const uint32_t values[5]);
/* Mouse camera (runtime_camera.c): checks the v1.09 camera code once the image is mapped; the
 * pad thread then hands it the captured mouse's motion and whether the right stick is used. */
void runtime_camera_configure(unsigned char *image, uint64_t size);
void runtime_camera_update(float dx, float dy, int captured, float sensitivity, int invert_y, int stick_camera);
/* Menu probe and trace (runtime_menu.c, BB_MENU_PROBE=1): F7 writes the game's live menu
 * objects to logs/menu-probe-*.txt, F8 its next seconds of calls into Scaleform to
 * logs/menu-trace-*.txt. The pad thread hands over the key presses so far, the free cursor's
 * position in the window and the pad state it reports (buttons, left stick). */
void runtime_menu_configure(unsigned char *image, uint64_t size);
/* Returns the buttons the mouse adds in a menu (d-pad steps toward the pointed item, confirm,
 * cancel). */
uint32_t runtime_menu_update(unsigned probe_requests, unsigned trace_requests, float pointer_x, float pointer_y,
                             int window_w, int window_h, uint32_t free_buttons, uint32_t buttons, uint8_t left_x,
                             uint8_t left_y);
uintptr_t runtime_content_resolve(const char *name);
void runtime_content_report(void);
typedef struct { const char *name; void *function; } RuntimeExport;
#define RUNTIME_LOOKUP(table, nid) runtime_lookup(table, sizeof(table)/sizeof(*(table)), nid)
const char *runtime_symbol(const char *nid);
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid);
uintptr_t runtime_kernel_resolve(const char *name);
uintptr_t runtime_file_resolve(const char *name);
uintptr_t runtime_services_resolve(const char *name);
/* The game's text dialog (sceImeDialog) is open: the pad gives the game nothing meanwhile. */
int runtime_ime_active(void);
void runtime_file_report(void);
void runtime_file_configure(const char *app0, const char *user);
int runtime_file_mount(const char *guest, const char *host);
void runtime_file_unmount(const char *guest);
int runtime_file_translate(const char *guest, char *out, size_t size);
int64_t runtime_file_open(const char *path, int flags, int mode);
int64_t runtime_file_close(int fd);
int64_t runtime_file_read(int fd, void *buffer, uint64_t size);
int64_t runtime_file_pread(int fd, void *buffer, uint64_t size, int64_t offset);
int64_t runtime_file_write(int fd, const void *buffer, uint64_t size);
int64_t runtime_file_lseek(int fd, int64_t offset, int whence);
int64_t runtime_file_stat(const char *path, void *guest_stat);
int64_t runtime_file_fstat(int fd, void *guest_stat);
int64_t runtime_file_getdents(int fd, char *buffer, uint64_t size, int64_t *base);
void runtime_thread_keys_cleanup(void);
/* Guest-visible errno values are FreeBSD's. */
int32_t runtime_guest_errno(int host_errno);
void *runtime_low_map(size_t size, int prot);
uintptr_t runtime_ajm_resolve(const char *name);
void runtime_ajm_report(void);
uintptr_t runtime_audio_resolve(const char *name);
void runtime_audio_report(void);
uintptr_t runtime_pad_resolve(const char *name);
void runtime_pad_report(void);
uintptr_t runtime_rtc_resolve(const char *name);
const char *runtime_file_user_dir(void);
void runtime_savedata_configure(const char *title);
uintptr_t runtime_savedata_resolve(const char *name);
void runtime_savedata_report(void);
void runtime_thread_attach_host(const char *name);
#endif
