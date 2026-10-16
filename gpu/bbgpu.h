/* C interface of the GPU library (gpu/): shadPS4's Liverpool/Vulkan video core,
 * GnmDriver, VideoOut and kernel event queues, adapted to the native loader. */
#ifndef BBGPU_H
#define BBGPU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *title;          /* window title */
    const char *serial;         /* CUSA id, names the pipeline cache */
    const char *user_dir;       /* pipeline cache/logs directory */
    uint32_t sdk_version;       /* from the eboot's procparam */
    uint32_t psf_attributes;    /* param.sfo ATTRIBUTE */
    int32_t width, height;      /* initial window size */
} BbGpuConfig;
/* Registers kernel event queues (needed with or without graphics). */
void bbgpu_register_kernel(void);
/* Creates window, Vulkan device, presenter and GPU command processor. */
int bbgpu_init(const BbGpuConfig *config);
/* Function for an imported NID ("NID#lib#mod"), or 0 when the GPU library does not provide it. */
uintptr_t bbgpu_resolve(const char *scoped_nid);
/* Called first by the loader's SIGSEGV handler: 1 when a GPU page-tracking fault was handled.
 * The context is the handler's ucontext_t (Linux) or the vectored exception handler's
 * EXCEPTION_POINTERS (Windows). */
int bbgpu_handle_fault(void *ucontext, void *address);
/* BB_WRITE_LOG=1: prints the logged GPU-side writes to guest memory near the fault
 * (same context as bbgpu_handle_fault). */
void bbgpu_dump_guest_writes(void *ucontext);
/* Keyboard text entry through the game window (IME dialog). begin returns 0 when
 * no window exists; poll returns 0 typing, 1 confirmed, 2 cancelled (UTF-8 text). */
int bbgpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8);
int bbgpu_text_input_poll(char *out_utf8, uint64_t size);
/* 1 while the in-game settings menu is open: the game's pad input is held neutral. */
int bbgpu_overlay_captures_input(void);
/* Keyboard and mouse: the pad controls, in the order of bbport.ini's key_* bindings. */
enum {
    BBGPU_PAD_CROSS, BBGPU_PAD_CIRCLE, BBGPU_PAD_SQUARE, BBGPU_PAD_TRIANGLE,
    BBGPU_PAD_L1, BBGPU_PAD_R1, BBGPU_PAD_L2, BBGPU_PAD_R2, BBGPU_PAD_L3, BBGPU_PAD_R3,
    BBGPU_PAD_OPTIONS, BBGPU_PAD_DPAD_UP, BBGPU_PAD_DPAD_DOWN, BBGPU_PAD_DPAD_LEFT,
    BBGPU_PAD_DPAD_RIGHT, BBGPU_PAD_TOUCHPAD_LEFT, BBGPU_PAD_TOUCHPAD_RIGHT,
    BBGPU_PAD_MOVE_UP, BBGPU_PAD_MOVE_DOWN, BBGPU_PAD_MOVE_LEFT, BBGPU_PAD_MOVE_RIGHT,
    BBGPU_PAD_CAMERA_UP, BBGPU_PAD_CAMERA_DOWN, BBGPU_PAD_CAMERA_LEFT, BBGPU_PAD_CAMERA_RIGHT,
    BBGPU_PAD_WALK, /* not a pad control: the movement keys push the stick only walk_scale far */
    BBGPU_PAD_CONTROL_COUNT
};
typedef struct {
    uint32_t held;            /* 1u << BBGPU_PAD_* for each control a binding holds */
    float mouse_dx, mouse_dy; /* mouse motion (counts) since the last call, while captured */
    int captured;             /* the mouse is captured for the camera (relative mode) */
    float sensitivity;        /* camera radians per mouse count */
    int invert_y;
    float walk_scale;         /* left stick deflection of the movement keys while walking */
    uint32_t probe_requests;  /* F7 presses so far (menu memory probe, runtime_menu.c) */
    uint32_t trace_requests;  /* F8 presses so far (menu call trace, runtime_menu.c) */
    float pointer_x, pointer_y; /* the free cursor in the window (points) */
    int window_w, window_h;   /* the window's size (points) */
    uint32_t free_buttons;    /* mouse buttons held while the mouse is free (bit SDL button - 1) */
} BbGpuKeyboardMouse;
/* Samples the keyboard and mouse bindings and takes the mouse motion (the pad thread). */
void bbgpu_keyboard_mouse(BbGpuKeyboardMouse *state);
/* The game shows a menu the mouse points into (runtime_menu.c): the window frees the mouse, and
 * the wheel and the other mouse buttons keep their bindings; left and right go to the menu.
 * x, y >= 0 (window points): the pointer is put there first, on the selected item. */
void bbgpu_menu_active(int active, float x, float y);
/* Number of symbols registered by the vendored libraries (diagnostics). */
unsigned bbgpu_symbol_count(void);
#ifdef __cplusplus
}
#endif
#endif
