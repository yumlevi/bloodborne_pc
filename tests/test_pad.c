#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"

static int capture;
int bbgpu_overlay_captures_input(void) { return capture; }
static BbGpuKeyboardMouse keyboard;
static int camera_stick=-1;
static float camera_dx;
void bbgpu_keyboard_mouse(BbGpuKeyboardMouse *state) { *state=keyboard; keyboard.mouse_dx=keyboard.mouse_dy=0; }
static unsigned menu_probes, menu_traces;
static uint32_t menu_buttons;
static uint32_t menu_adds;
uint32_t runtime_menu_update(unsigned probe_requests, unsigned trace_requests, float pointer_x, float pointer_y,
                             int window_w, int window_h, uint32_t free_buttons, uint32_t buttons, uint8_t left_x,
                             uint8_t left_y) {
    (void)pointer_x; (void)pointer_y; (void)window_w; (void)window_h; (void)free_buttons; (void)left_x; (void)left_y;
    menu_probes=probe_requests; menu_traces=trace_requests; menu_buttons=buttons;
    return menu_adds;
}
void runtime_camera_update(float dx, float dy, int captured, float sensitivity, int invert_y, int stick_camera) {
    (void)dy; (void)captured; (void)sensitivity; (void)invert_y;
    camera_dx=dx; camera_stick=stick_camera;
}
static int ime_open;
int runtime_ime_active(void) { return ime_open; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    usleep(25000);
}

int main(void) {
    char path[]="/tmp/bbport-pad-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    setenv("BB_PAD_FILE",path,1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);
    /* Keyboard and mouse: bindings add buttons and replace a stick; the mouse turns the camera
     * until the right stick is used. */
    keyboard.held=(1u<<BBGPU_PAD_R3)|(1u<<BBGPU_PAD_L2)|(1u<<BBGPU_PAD_MOVE_UP)|(1u<<BBGPU_PAD_MOVE_RIGHT);
    keyboard.mouse_dx=12.0f;
    keyboard.captured=1;
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_R3|BTN_L2))==(BTN_R3|BTN_L2) && data.l2==255);
    assert(data.left_x==255 && data.left_y==0 && data.right_x==128);
    assert(camera_dx==12.0f && camera_stick==0);
    keyboard.held=1u<<BBGPU_PAD_CAMERA_LEFT;
    assert(pad_read_state(1,&data)==0 && data.right_x==0 && camera_stick==1);
    /* Walking: the movement keys push the stick walk_scale far, diagonals to the same length. */
    keyboard.held=(1u<<BBGPU_PAD_MOVE_UP)|(1u<<BBGPU_PAD_WALK);
    keyboard.walk_scale=0.4f;
    assert(pad_read_state(1,&data)==0 && data.left_x==128 && data.left_y==128-51);
    keyboard.held|=1u<<BBGPU_PAD_MOVE_RIGHT;
    assert(pad_read_state(1,&data)==0 && data.left_x==128+36 && data.left_y==128-36);
    memset(&keyboard,0,sizeof(keyboard));
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.right_x==128 && menu_probes==0);
    /* The menu probe and trace see the F7/F8 presses and the buttons the pad reports. */
    keyboard.probe_requests=1;
    keyboard.trace_requests=2;
    keyboard.held=1u<<BBGPU_PAD_DPAD_DOWN;
    assert(pad_read_state(1,&data)==0 && menu_probes==1 && menu_traces==2 && menu_buttons==BTN_DOWN);
    keyboard.held=0;
    /* The buttons the mouse adds in a menu reach the game. */
    menu_adds=BTN_CIRCLE;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_CIRCLE));
    menu_adds=0;
    /* The game's text dialog: neutral input while it is open, then the press that confirmed the
     * text is held back until every button is up. */
    keyboard.held=1u<<BBGPU_PAD_CROSS;
    ime_open=1;
    assert(pad_read_state(1,&data)==0 && data.buttons==0);
    ime_open=0;
    assert(pad_read_state(1,&data)==0 && data.buttons==0);
    keyboard.held=0;
    assert(pad_read_state(1,&data)==0 && data.buttons==0);
    keyboard.held=1u<<BBGPU_PAD_CROSS;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_CROSS));
    keyboard.held=0;

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="bbport test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id==0);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id==1);
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==480);
    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    puts("PASS: pad ABI, debug camera chord, left/right clicks, keyboard and mouse bindings, text dialog, "
         "SDL touch coordinates, overlay capture");
}
