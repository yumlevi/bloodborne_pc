#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"

#ifdef _WIN32
uint64_t host_monotonic_ns(void) {
    struct timespec now;
    timespec_get(&now,TIME_UTC);
    return (uint64_t)now.tv_sec*1000000000u+(uint64_t)now.tv_nsec;
}
#endif

static int capture;
int bbgpu_overlay_captures_input(void) { return capture; }
static int mouse_owned,mouse_dx,mouse_dy,mouse_wheel;
static uint32_t mouse_buttons;
int bbgpu_mouse_read(int32_t *dx,int32_t *dy,int32_t *wheel,uint32_t *buttons) {
    *dx=mouse_dx; *dy=mouse_dy; *wheel=mouse_wheel; *buttons=mouse_buttons;
    mouse_dx=mouse_dy=mouse_wheel=0;
    return mouse_owned;
}
#ifdef _WIN32
static void envset(const char *name,const char *value) { assert(_putenv_s(name,value)==0); }
#else
static void envset(const char *name,const char *value) { assert(setenv(name,value,1)==0); }
#endif
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
#ifdef _WIN32
    char path[]="bbport-pad-test.tmp";
    FILE *initial=fopen(path,"w");
    assert(initial);
    fclose(initial);
#else
    char path[]="/tmp/bbport-pad-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
#endif
    envset("BB_PAD_FILE",path);
    envset("BB_MOUSE_SENSITIVITY","2");
    envset("BB_MOUSE_INVERT_Y","1");
    envset("BB_MOUSE_LEFT","r1");
    envset("BB_MOUSE_MIDDLE","square");
    envset("BB_KEY_CROSS","5");
    envset("BB_KEY_TRIANGLE","0");
    envset("BB_PAD_SOUTH","triangle");
    envset("SDL_VIDEODRIVER","dummy");
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert(input.cross==SDL_SCANCODE_B && input.triangle==SDL_SCANCODE_UNKNOWN && input.mouse_invert_y);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471);
    assert(data.touches[0].id!=0);
    uint8_t first_touch_id=data.touches[0].id;
    usleep(2000);
    assert(pad_read_state(1,&data)==0 && data.touches[0].id==first_touch_id && data.touch_held_time>0);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440 &&
           data.touches[0].id==first_touch_id);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);
    inject(path,"touchpad_left");
    assert(pad_read_state(1,&data)==0 && data.touches[0].id!=first_touch_id);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.touch_count==0);
    mouse_owned=1; mouse_dx=10; mouse_dy=-4; mouse_buttons=1;
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & BTN_R1) && data.right_x==148 && data.right_y==136);
    mouse_buttons=0;

    /* Regression: an idle virtual controller listed before the real one must not steal all
     * actions. runtime_pad merges every connected gamepad instead of trusting ids[0]. */
    SDL_VirtualJoystickDesc idle_desc;
    SDL_INIT_INTERFACE(&idle_desc);
    idle_desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    idle_desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    idle_desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    idle_desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    idle_desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    idle_desc.name="bbport idle controller";
    idle_desc.vendor_id=0x1d50;
    idle_desc.product_id=0x6189;
    SDL_JoystickID idle_id=SDL_AttachVirtualJoystick(&idle_desc);
    assert(idle_id!=0);

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
    usleep(260000); /* runtime_pad rescans hot-plugged controllers every 250 ms */
    mouse_buttons=1u<<2;
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD) && (data.buttons & BTN_SQUARE));
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id!=0);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id!=0);
    assert(data.touches[0].id!=data.touches[1].id);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_TRIANGLE) && !(data.buttons & BTN_CROSS));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));
    capture=1;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_BACK,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==480);
    assert(data.buttons & BTN_TOUCHPAD);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_BACK,false));
    SDL_CloseJoystick(joystick);
    for (size_t i=0;i<gamepad_count;++i) SDL_CloseGamepad(gamepads[i]);
    gamepad_count=0;
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    assert(SDL_DetachVirtualJoystick(idle_id));
    SDL_Quit();
    unlink(path);
    puts("PASS: pad ABI, keyboard/mouse/controller rebinding, simultaneous gamepad, touch, overlay capture");
}
