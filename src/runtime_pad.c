/* libScePad on SDL3 gamepads, keyboard and relative mouse. SDL events are pumped
 * by the window thread (gpu/shim/window.cpp); here state is sampled and merged, so a gamepad
 * does not disable keyboard/mouse input. BB_KEY_*, BB_MOUSE_* and BB_PAD_* select the mappings.
 *
 * Default keyboard layout:
 *   WASD left stick, arrow keys right stick, Space Cross, LShift Circle,
 *   E Square, Q Triangle, 1 L1, 3 R1, R L2, F R2, Z L3, C R3,
 *   Enter Options, Tab left touchpad, Backspace right touchpad,
 *   IJKL d-pad (I up, K down, J left, L right).
 * Mouse: motion is the right stick; left/right/middle are R1/L1/R3, side buttons Square/Triangle,
 * wheel is d-pad up/down. F10 releases or captures the pointer. */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <sys/stat.h>

#define ERR_INVALID_ARG ((int32_t)0x80920001)
#define ERR_INVALID_HANDLE ((int32_t)0x80920003)
#define ERR_ALREADY_OPENED ((int32_t)0x80920004)
#define ERR_NOT_INITIALIZED ((int32_t)0x80920005)
#define PAD_HANDLE 1

enum {
    BTN_L3=0x2, BTN_R3=0x4, BTN_OPTIONS=0x8, BTN_UP=0x10, BTN_RIGHT=0x20, BTN_DOWN=0x40, BTN_LEFT=0x80,
    BTN_L2=0x100, BTN_R2=0x200, BTN_L1=0x400, BTN_R1=0x800, BTN_TRIANGLE=0x1000, BTN_CIRCLE=0x2000,
    BTN_CROSS=0x4000, BTN_SQUARE=0x8000, BTN_TOUCHPAD=0x100000,
};
typedef struct { uint16_t x, y; uint8_t id, reserve[3]; } PadTouch;
typedef struct {
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2, analog_padding[2];
    float orientation[4], acceleration[3], angular_velocity[3];
    uint8_t touch_count, touch_reserve[3];
    uint32_t touch_held_time;
    PadTouch touches[2];
    uint8_t connected, pad0[3];
    uint64_t timestamp;
    uint8_t extension[16];
    uint8_t connected_count, reserve[2], unique_length, unique[12];
} PadData;
typedef struct {
    float pixel_density; uint16_t resolution_x, resolution_y;
    uint8_t dead_zone_left, dead_zone_right, connection_type, connected_count;
    uint8_t connected, pad[3];
    int32_t device_class;
    uint8_t reserve[8];
} ControllerInfo;
_Static_assert(sizeof(PadData)==120,"OrbisPadData layout");
_Static_assert(sizeof(PadTouch)==8,"OrbisPadTouch layout");
_Static_assert(__builtin_offsetof(PadData,touches)==60,"OrbisPadData touch offset");
_Static_assert(__builtin_offsetof(PadData,timestamp)==80,"OrbisPadData timestamp offset");
_Static_assert(sizeof(ControllerInfo)==28,"OrbisPadControllerInformation layout");

static HostMutex lock=HOST_MUTEX_INIT;
static int initialized, opened, sdl_ready;
static SDL_Gamepad *gamepad;
#define MAX_GAMEPADS 8
static SDL_Gamepad *gamepads[MAX_GAMEPADS];
static size_t gamepad_count;
static size_t reads;
static uint8_t connected_count;
static float motion_orientation[4]={0.0f,0.0f,0.0f,1.0f};
static uint64_t motion_last_us;

typedef struct { uint32_t button; int8_t touch_side; } PadAction;
enum {
    PAD_SOUTH,PAD_EAST,PAD_WEST,PAD_NORTH,PAD_LEFT_SHOULDER,PAD_RIGHT_SHOULDER,
    PAD_LEFT_TRIGGER,PAD_RIGHT_TRIGGER,PAD_LEFT_STICK,PAD_RIGHT_STICK,PAD_START,
    PAD_BACK,PAD_TOUCHPAD,PAD_DPAD_UP,PAD_DPAD_DOWN,PAD_DPAD_LEFT,PAD_DPAD_RIGHT,
    PAD_BINDING_COUNT
};

typedef struct {
    SDL_Scancode move_up,move_down,move_left,move_right;
    SDL_Scancode look_up,look_down,look_left,look_right;
    SDL_Scancode cross,circle,square,triangle,l1,r1,l2,r2,l3,r3,options;
    SDL_Scancode dpad_up,dpad_down,dpad_left,dpad_right,touch_left,touch_right;
    uint32_t mouse[5],wheel_up,wheel_down;
    PadAction pad[PAD_BINDING_COUNT];
    float mouse_sensitivity;
    int mouse_invert_y;
} InputBindings;
static InputBindings input;
static int input_loaded;

static SDL_Scancode input_key(const char *name,SDL_Scancode fallback) {
    const char *value=getenv(name);
    if (!value || !*value) return fallback;
    char *end=NULL;
    long code=strtol(value,&end,10);
    /* SDL_SCANCODE_UNKNOWN (0) is intentionally accepted as "unbound". */
    return end!=value && !*end && code>=SDL_SCANCODE_UNKNOWN && code<SDL_SCANCODE_COUNT
           ? (SDL_Scancode)code : fallback;
}
static int parse_action(const char *value,uint32_t *button) {
    static const struct { const char *name; uint32_t button; } actions[]={
        {"none",0},{"cross",BTN_CROSS},{"circle",BTN_CIRCLE},{"square",BTN_SQUARE},
        {"triangle",BTN_TRIANGLE},{"l1",BTN_L1},{"r1",BTN_R1},{"l2",BTN_L2},
        {"r2",BTN_R2},{"l3",BTN_L3},{"r3",BTN_R3},{"options",BTN_OPTIONS},
        {"touchpad",BTN_TOUCHPAD},{"up",BTN_UP},{"down",BTN_DOWN},
        {"left",BTN_LEFT},{"right",BTN_RIGHT},
    };
    for (size_t i=0;i<sizeof(actions)/sizeof(*actions);++i)
        if (!strcmp(value,actions[i].name)) { *button=actions[i].button; return 1; }
    return 0;
}
static uint32_t input_action(const char *name,uint32_t fallback) {
    const char *value=getenv(name);
    uint32_t button;
    return value && *value && parse_action(value,&button) ? button : fallback;
}
static int parse_pad_action(const char *value,PadAction *result) {
    result->button=0; result->touch_side=-1;
    if (!strcmp(value,"touch_left")) { result->button=BTN_TOUCHPAD; result->touch_side=0; return 1; }
    if (!strcmp(value,"touch_right")) { result->button=BTN_TOUCHPAD; result->touch_side=1; return 1; }
    if (!strcmp(value,"touchpad")) { result->button=BTN_TOUCHPAD; result->touch_side=2; return 1; }
    return parse_action(value,&result->button);
}
static PadAction input_pad_action(const char *name,const char *fallback) {
    const char *value=getenv(name);
    PadAction result;
    if (value && *value && parse_pad_action(value,&result)) return result;
    parse_pad_action(fallback,&result);
    return result;
}
static void load_input(void) {
    if (input_loaded) return;
    input_loaded=1;
#define KEY(field,name,fallback) input.field=input_key("BB_KEY_" name,SDL_SCANCODE_##fallback)
    KEY(move_up,"MOVE_UP",W); KEY(move_down,"MOVE_DOWN",S);
    KEY(move_left,"MOVE_LEFT",A); KEY(move_right,"MOVE_RIGHT",D);
    KEY(look_up,"LOOK_UP",UP); KEY(look_down,"LOOK_DOWN",DOWN);
    KEY(look_left,"LOOK_LEFT",LEFT); KEY(look_right,"LOOK_RIGHT",RIGHT);
    KEY(cross,"CROSS",SPACE); KEY(circle,"CIRCLE",LSHIFT);
    KEY(square,"SQUARE",E); KEY(triangle,"TRIANGLE",Q);
    KEY(l1,"L1",1); KEY(r1,"R1",3); KEY(l2,"L2",R); KEY(r2,"R2",F);
    KEY(l3,"L3",Z); KEY(r3,"R3",C); KEY(options,"OPTIONS",RETURN);
    KEY(dpad_up,"DPAD_UP",I); KEY(dpad_down,"DPAD_DOWN",K);
    KEY(dpad_left,"DPAD_LEFT",J); KEY(dpad_right,"DPAD_RIGHT",L);
    KEY(touch_left,"TOUCH_LEFT",TAB); KEY(touch_right,"TOUCH_RIGHT",BACKSPACE);
#undef KEY
    input.mouse[0]=input_action("BB_MOUSE_LEFT",BTN_R1);
    input.mouse[1]=input_action("BB_MOUSE_RIGHT",BTN_L1);
    input.mouse[2]=input_action("BB_MOUSE_MIDDLE",BTN_R3);
    input.mouse[3]=input_action("BB_MOUSE_X1",BTN_SQUARE);
    input.mouse[4]=input_action("BB_MOUSE_X2",BTN_TRIANGLE);
    input.wheel_up=input_action("BB_MOUSE_WHEEL_UP",BTN_UP);
    input.wheel_down=input_action("BB_MOUSE_WHEEL_DOWN",BTN_DOWN);
#define PAD(field,name,fallback) input.pad[field]=input_pad_action("BB_PAD_" name,fallback)
    PAD(PAD_SOUTH,"SOUTH","cross"); PAD(PAD_EAST,"EAST","circle");
    PAD(PAD_WEST,"WEST","square"); PAD(PAD_NORTH,"NORTH","triangle");
    PAD(PAD_LEFT_SHOULDER,"LEFT_SHOULDER","l1");
    PAD(PAD_RIGHT_SHOULDER,"RIGHT_SHOULDER","r1");
    PAD(PAD_LEFT_TRIGGER,"LEFT_TRIGGER","l2"); PAD(PAD_RIGHT_TRIGGER,"RIGHT_TRIGGER","r2");
    PAD(PAD_LEFT_STICK,"LEFT_STICK","l3"); PAD(PAD_RIGHT_STICK,"RIGHT_STICK","r3");
    PAD(PAD_START,"START","options"); PAD(PAD_BACK,"BACK","touch_left");
    PAD(PAD_TOUCHPAD,"TOUCHPAD","touchpad");
    PAD(PAD_DPAD_UP,"DPAD_UP","up"); PAD(PAD_DPAD_DOWN,"DPAD_DOWN","down");
    PAD(PAD_DPAD_LEFT,"DPAD_LEFT","left"); PAD(PAD_DPAD_RIGHT,"DPAD_RIGHT","right");
#undef PAD
    const char *sensitivity=getenv("BB_MOUSE_SENSITIVITY");
    input.mouse_sensitivity=sensitivity ? strtof(sensitivity,NULL) : 3.0f;
    if (input.mouse_sensitivity<0.1f) input.mouse_sensitivity=0.1f;
    if (input.mouse_sensitivity>20.0f) input.mouse_sensitivity=20.0f;
    input.mouse_invert_y=getenv("BB_MOUSE_INVERT_Y") && getenv("BB_MOUSE_INVERT_Y")[0]=='1';
    printf("Runtime: keyboard/mouse input enabled (mouse sensitivity %.2f%s; F10 releases mouse)\n",
           input.mouse_sensitivity,input.mouse_invert_y ? ", inverted Y" : "");
}

static uint64_t now_us(void) { return host_monotonic_ns()/1000u; }
static uint8_t axis(int16_t v) { int x=(v+32768)>>8; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint8_t trigger(int16_t v) { int x=v>>7; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint16_t touch_axis(float v, int max) {
    return (uint16_t)(v<=0.0f ? 0 : v>=1.0f ? max : (int)(v*max+0.5f));
}
static void touch_click(PadData *d, int right) {
    d->buttons|=BTN_TOUCHPAD;
    d->touch_count=1;
    d->touches[0]=(PadTouch){.x=right ? 1440 : 480,.y=471,.id=0};
}

static void enable_motion(SDL_Gamepad *g) {
    if (SDL_GamepadHasSensor(g,SDL_SENSOR_ACCEL))
        SDL_SetGamepadSensorEnabled(g,SDL_SENSOR_ACCEL,true);
    if (SDL_GamepadHasSensor(g,SDL_SENSOR_GYRO))
        SDL_SetGamepadSensorEnabled(g,SDL_SENSOR_GYRO,true);
}

static int sample_motion(SDL_Gamepad *g,PadData *d) {
    float accel[3],gyro[3];
    const int has_accel=SDL_GamepadSensorEnabled(g,SDL_SENSOR_ACCEL) &&
        SDL_GetGamepadSensorData(g,SDL_SENSOR_ACCEL,accel,3);
    const int has_gyro=SDL_GamepadSensorEnabled(g,SDL_SENSOR_GYRO) &&
        SDL_GetGamepadSensorData(g,SDL_SENSOR_GYRO,gyro,3);
    if (!has_accel && !has_gyro) return 0;
    if (has_accel) for (int i=0;i<3;++i) d->acceleration[i]=accel[i]/9.80665f;
    if (has_gyro) {
        for (int i=0;i<3;++i) d->angular_velocity[i]=gyro[i];
        const uint64_t now=now_us();
        float dt=motion_last_us ? (float)(now-motion_last_us)/1000000.0f : 0.0f;
        motion_last_us=now;
        if (dt>0.0f && dt<0.1f) {
            const float x=motion_orientation[0],y=motion_orientation[1];
            const float z=motion_orientation[2],w=motion_orientation[3];
            const float gx=gyro[0],gy=gyro[1],gz=gyro[2];
            motion_orientation[0]+=0.5f*( w*gx+y*gz-z*gy)*dt;
            motion_orientation[1]+=0.5f*( w*gy+z*gx-x*gz)*dt;
            motion_orientation[2]+=0.5f*( w*gz+x*gy-y*gx)*dt;
            motion_orientation[3]+=0.5f*(-x*gx-y*gy-z*gz)*dt;
            const float n=sqrtf(motion_orientation[0]*motion_orientation[0]+
                motion_orientation[1]*motion_orientation[1]+motion_orientation[2]*motion_orientation[2]+
                motion_orientation[3]*motion_orientation[3]);
            if (n>0.0001f) for (int i=0;i<4;++i) motion_orientation[i]/=n;
        }
    }
    memcpy(d->orientation,motion_orientation,sizeof(motion_orientation));
    return 1;
}

static int gamepad_known(SDL_JoystickID id) {
    for (size_t i=0;i<gamepad_count;++i)
        if (SDL_GetGamepadID(gamepads[i])==id) return 1;
    return 0;
}

/* Keep every SDL gamepad open. Windows often exposes a dormant Steam/virtual controller before
 * the physical pad; sampling only ids[0] then makes both the intended pad and the old keyboard
 * fallback appear dead. Buttons from all pads are merged and the strongest stick wins. */
static SDL_Gamepad *current_gamepad(void) {
    static uint64_t last_scan;
    if (!sdl_ready) sdl_ready = SDL_WasInit(SDL_INIT_GAMEPAD) ? 1 : SDL_InitSubSystem(SDL_INIT_GAMEPAD) ? 1 : -1;
    if (sdl_ready<0) return NULL;
    for (size_t i=0;i<gamepad_count;) {
        if (SDL_GamepadConnected(gamepads[i])) { ++i; continue; }
        SDL_CloseGamepad(gamepads[i]);
        memmove(&gamepads[i],&gamepads[i+1],(gamepad_count-i-1)*sizeof(*gamepads));
        --gamepad_count;
    }
    const uint64_t now=now_us();
    if (!last_scan || now-last_scan>=250000) {
        last_scan=now;
        int count=0;
        SDL_JoystickID *ids=SDL_GetGamepads(&count);
        for (int i=0;ids && i<count && gamepad_count<MAX_GAMEPADS;++i) {
            if (gamepad_known(ids[i])) continue;
            SDL_Gamepad *opened_pad=SDL_OpenGamepad(ids[i]);
            if (opened_pad) {
                enable_motion(opened_pad);
                gamepads[gamepad_count++]=opened_pad;
                ++connected_count;
                printf("Runtime: gamepad connected [%zu/%d]: %s\n",gamepad_count,count,
                       SDL_GetGamepadName(opened_pad));
            }
        }
        SDL_free(ids);
    }
    gamepad=gamepad_count ? gamepads[0] : NULL;
    return gamepad;
}
static void merge_axis(uint8_t *target, int16_t raw) {
    const uint8_t value=axis(raw);
    if (abs((int)value-128)>abs((int)*target-128)) *target=value;
}
static void apply_pad_action(PadData *d,PadAction action,uint8_t strength,
                             int *forced_touch,int *automatic_touch) {
    d->buttons|=action.button;
    if ((action.button & BTN_L2) && strength>d->l2) d->l2=strength;
    if ((action.button & BTN_R2) && strength>d->r2) d->r2=strength;
    if (action.touch_side==0 || action.touch_side==1) *forced_touch=action.touch_side;
    else if (action.touch_side==2) *automatic_touch=1;
}
static void sample_host(PadData *d) {
    memset(d,0,sizeof(*d));
    d->left_x=d->left_y=d->right_x=d->right_y=128;
    d->orientation[3]=1.0f;
    d->connected=1; d->connected_count=connected_count ? connected_count : 1;
    d->timestamp=now_us();
    load_input();
    current_gamepad();
    if (bbgpu_overlay_captures_input()) return; /* settings menu open: neutral input */
    const bool *k=SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(NULL) : NULL;
    int forced_touch=-1,automatic_touch=0,motion_sampled=0;
    for (size_t pad_index=0;pad_index<gamepad_count;++pad_index) {
        SDL_Gamepad *g=gamepads[pad_index];
        static const struct { SDL_GamepadButton sdl; int binding; } map[]={
            {SDL_GAMEPAD_BUTTON_SOUTH,PAD_SOUTH}, {SDL_GAMEPAD_BUTTON_EAST,PAD_EAST},
            {SDL_GAMEPAD_BUTTON_WEST,PAD_WEST}, {SDL_GAMEPAD_BUTTON_NORTH,PAD_NORTH},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,PAD_LEFT_SHOULDER},
            {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,PAD_RIGHT_SHOULDER},
            {SDL_GAMEPAD_BUTTON_LEFT_STICK,PAD_LEFT_STICK},
            {SDL_GAMEPAD_BUTTON_RIGHT_STICK,PAD_RIGHT_STICK},
            {SDL_GAMEPAD_BUTTON_START,PAD_START}, {SDL_GAMEPAD_BUTTON_BACK,PAD_BACK},
            {SDL_GAMEPAD_BUTTON_TOUCHPAD,PAD_TOUCHPAD},
            {SDL_GAMEPAD_BUTTON_DPAD_UP,PAD_DPAD_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN,PAD_DPAD_DOWN},
            {SDL_GAMEPAD_BUTTON_DPAD_LEFT,PAD_DPAD_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,PAD_DPAD_RIGHT},
        };
        for (size_t i=0;i<sizeof(map)/sizeof(*map);++i)
            if (SDL_GetGamepadButton(g,map[i].sdl))
                apply_pad_action(d,input.pad[map[i].binding],255,&forced_touch,&automatic_touch);
        merge_axis(&d->left_x,SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTX));
        merge_axis(&d->left_y,SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTY));
        merge_axis(&d->right_x,SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTX));
        merge_axis(&d->right_y,SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTY));
        const uint8_t l2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        const uint8_t r2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        if (l2>30) apply_pad_action(d,input.pad[PAD_LEFT_TRIGGER],l2,&forced_touch,&automatic_touch);
        if (r2>30) apply_pad_action(d,input.pad[PAD_RIGHT_TRIGGER],r2,&forced_touch,&automatic_touch);
        if (SDL_GetNumGamepadTouchpads(g)>0) {
            const int fingers=SDL_GetNumGamepadTouchpadFingers(g,0);
            for (int finger=0;finger<fingers && d->touch_count<2;++finger) {
                bool down=false;
                float x=0, y=0;
                if (SDL_GetGamepadTouchpadFinger(g,0,finger,&down,&x,&y,NULL) && down) {
                    d->touches[d->touch_count++]=(PadTouch){.x=touch_axis(x,1919),
                        .y=touch_axis(y,942),.id=(uint8_t)finger};
                }
            }
        }
        if (!motion_sampled) motion_sampled=sample_motion(g,d);
    }
    /* A forced left/right mapping wins over physical touch coordinates. The generic touchpad
     * action keeps the real finger side and synthesizes the left side when none is available. */
    if (forced_touch>=0) touch_click(d,forced_touch);
    else if (automatic_touch) {
        d->buttons|=BTN_TOUCHPAD;
        if (!d->touch_count) touch_click(d,0);
    }
    if (k) {
#define PRESSED(field) k[input.field]
        if (PRESSED(cross)) d->buttons|=BTN_CROSS;
        if (PRESSED(circle)) d->buttons|=BTN_CIRCLE;
        if (PRESSED(square)) d->buttons|=BTN_SQUARE;
        if (PRESSED(triangle)) d->buttons|=BTN_TRIANGLE;
        if (PRESSED(l1)) d->buttons|=BTN_L1;
        if (PRESSED(r1)) d->buttons|=BTN_R1;
        if (PRESSED(l2)) d->buttons|=BTN_L2;
        if (PRESSED(r2)) d->buttons|=BTN_R2;
        if (PRESSED(l3)) d->buttons|=BTN_L3;
        if (PRESSED(r3)) d->buttons|=BTN_R3;
        if (PRESSED(options)) d->buttons|=BTN_OPTIONS;
        if (PRESSED(dpad_up)) d->buttons|=BTN_UP;
        if (PRESSED(dpad_down)) d->buttons|=BTN_DOWN;
        if (PRESSED(dpad_left)) d->buttons|=BTN_LEFT;
        if (PRESSED(dpad_right)) d->buttons|=BTN_RIGHT;
        if (PRESSED(touch_left)) touch_click(d,0);
        if (PRESSED(touch_right)) touch_click(d,1);
        if (PRESSED(move_left) || PRESSED(move_right))
            d->left_x=(uint8_t)(128-(PRESSED(move_left) ? 128 : 0)+(PRESSED(move_right) ? 127 : 0));
        if (PRESSED(move_up) || PRESSED(move_down))
            d->left_y=(uint8_t)(128-(PRESSED(move_up) ? 128 : 0)+(PRESSED(move_down) ? 127 : 0));
        if (PRESSED(look_left) || PRESSED(look_right))
            d->right_x=(uint8_t)(128-(PRESSED(look_left) ? 128 : 0)+(PRESSED(look_right) ? 127 : 0));
        if (PRESSED(look_up) || PRESSED(look_down))
            d->right_y=(uint8_t)(128-(PRESSED(look_up) ? 128 : 0)+(PRESSED(look_down) ? 127 : 0));
#undef PRESSED
    }
    int32_t mouse_x=0,mouse_y=0,wheel=0;
    uint32_t mouse_buttons=0;
    if (bbgpu_mouse_read(&mouse_x,&mouse_y,&wheel,&mouse_buttons)) {
        for (int i=0;i<5;++i) if (mouse_buttons & (1u<<i)) d->buttons|=input.mouse[i];
        if (wheel>0) d->buttons|=input.wheel_up;
        if (wheel<0) d->buttons|=input.wheel_down;
        if (mouse_x) {
            int value=128+(int)(mouse_x*input.mouse_sensitivity);
            d->right_x=(uint8_t)(value<0 ? 0 : value>255 ? 255 : value);
        }
        if (mouse_y) {
            int value=128+(int)(mouse_y*input.mouse_sensitivity)*(input.mouse_invert_y ? -1 : 1);
            d->right_y=(uint8_t)(value<0 ? 0 : value>255 ? 255 : value);
        }
    }
    if (d->buttons & BTN_L2) d->l2=255;
    if (d->buttons & BTN_R2) d->r2=255;
}

/* BB_PAD_FILE=<file>: scripted input for automated runs. The file holds whitespace-separated
 * tokens, re-read when it changes: button names (cross circle square triangle l1 r1 l2 r2 l3 r3
 * options touchpad touchpad_left touchpad_right up down left right) are held while listed;
 * touchpad defaults to a left-side click; lx= ly= rx= ry= (0..255) override
 * the sticks. An empty file releases everything. */
static struct { uint32_t buttons; int stick[4]; int touch_side; } injected={0,{-1,-1,-1,-1},-1};
static int replay_armed;      /* 1 while a BB_PAD_REPLAY recording plays, 2 once it ended */
static uint64_t replay_start; /* 0: (re)start at the next sample */
static void read_inject(void) {
    static const char *path; static int checked; static uint64_t last_check; static struct timespec mtime;
    if (!checked) { path=getenv("BB_PAD_FILE"); checked=1; }
    if (!path || !*path) return;
    uint64_t now=now_us();
    if (now-last_check<20000) return;
    last_check=now;
    struct stat st;
    if (stat(path,&st)!=0) return;
#ifdef _WIN32
    /* Whole-second times: the size tells writes within the same second apart. */
    if (st.st_mtime==mtime.tv_sec && st.st_size==mtime.tv_nsec) return;
    mtime.tv_sec=st.st_mtime; mtime.tv_nsec=(long)st.st_size;
#else
    if (st.st_mtim.tv_sec==mtime.tv_sec && st.st_mtim.tv_nsec==mtime.tv_nsec) return;
    mtime=st.st_mtim;
#endif
    FILE *f=fopen(path,"r");
    if (!f) return;
    static const struct { const char *name; uint32_t ps; } names[]={
        {"cross",BTN_CROSS}, {"circle",BTN_CIRCLE}, {"square",BTN_SQUARE}, {"triangle",BTN_TRIANGLE},
        {"l1",BTN_L1}, {"r1",BTN_R1}, {"l2",BTN_L2}, {"r2",BTN_R2}, {"l3",BTN_L3}, {"r3",BTN_R3},
        {"options",BTN_OPTIONS}, {"touchpad",BTN_TOUCHPAD},
        {"up",BTN_UP}, {"down",BTN_DOWN}, {"left",BTN_LEFT}, {"right",BTN_RIGHT},
    };
    static const char *sticks[]={"lx=","ly=","rx=","ry="};
    injected.buttons=0;
    injected.touch_side=-1;
    for (int i=0;i<4;++i) injected.stick[i]=-1;
    char token[64];
    while (fscanf(f,"%63s",token)==1) {
        if (!strcmp(token,"replay") && replay_armed!=1) { replay_armed=1; replay_start=0; } /* BB_PAD_REPLAY */
        if (!strcmp(token,"touchpad_left") || !strcmp(token,"touchpad_right")) {
            injected.buttons|=BTN_TOUCHPAD;
            injected.touch_side=!strcmp(token,"touchpad_right");
        }
        for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) if (!strcmp(token,names[i].name)) injected.buttons|=names[i].ps;
        for (int i=0;i<4;++i) if (!strncmp(token,sticks[i],3)) { int v=atoi(token+3); injected.stick[i]=v<0 ? 0 : v>255 ? 255 : v; }
    }
    fclose(f);
    printf("Runtime: pad file: buttons 0x%x sticks %d %d %d %d\n",injected.buttons,
           injected.stick[0],injected.stick[1],injected.stick[2],injected.stick[3]);
}
/* BB_PAD_RECORD=<file>: F9 starts and stops recording the pad state (gamepad or keyboard) with
 * the time since F9; BB_PAD_REPLAY=<file> plays such a recording back, started by the token
 * "replay" in BB_PAD_FILE (scripted tests repeat a route the player ran once). Lines: ms buttons
 * lx ly rx ry l2 r2, written when the state changes. */
typedef struct { uint32_t ms, buttons; uint8_t axes[4], l2, r2; } PadSample;
static FILE *record_file;
static uint64_t record_start;
static PadSample record_last;
static void record_sample(const PadData *d) {
    static const char *path; static int checked, f9_was_down;
    if (!checked) { path=getenv("BB_PAD_RECORD"); checked=1; }
    if (!path || !*path || !sdl_ready) return;
    const bool *k=SDL_GetKeyboardState(NULL);
    const int f9=k && k[SDL_SCANCODE_F9];
    if (f9 && !f9_was_down) {
        if (record_file) {
            fclose(record_file); record_file=NULL;
            printf("Runtime: pad recording stopped (%s)\n",path);
        } else if ((record_file=fopen(path,"w"))) {
            record_start=now_us();
            memset(&record_last,0xff,sizeof(record_last));
            printf("Runtime: pad recording started (%s, F9 stops)\n",path);
        }
    }
    f9_was_down=f9;
    if (!record_file) return;
    PadSample s={(uint32_t)((now_us()-record_start)/1000),d->buttons,
                 {d->left_x,d->left_y,d->right_x,d->right_y},d->l2,d->r2};
    if (s.buttons==record_last.buttons && !memcmp(s.axes,record_last.axes,4) &&
        s.l2==record_last.l2 && s.r2==record_last.r2) return;
    record_last=s;
    fprintf(record_file,"%u %u %u %u %u %u %u %u\n",s.ms,s.buttons,s.axes[0],s.axes[1],s.axes[2],
            s.axes[3],s.l2,s.r2);
    fflush(record_file);
}
static PadSample *replay; static size_t replay_count, replay_next;
static void replay_sample(PadData *d) {
    if (!replay_armed) return;
    if (!replay_start) {
        static int loaded;
        if (!loaded) {
            loaded=1;
            const char *path=getenv("BB_PAD_REPLAY");
            FILE *f=path ? fopen(path,"r") : NULL;
            PadSample s; unsigned v[8]; size_t cap=0;
            while (f && fscanf(f,"%u %u %u %u %u %u %u %u",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7])==8) {
                s=(PadSample){v[0],v[1],{(uint8_t)v[2],(uint8_t)v[3],(uint8_t)v[4],(uint8_t)v[5]},(uint8_t)v[6],(uint8_t)v[7]};
                if (replay_count==cap && !(replay=realloc(replay,(cap=cap ? cap*2 : 1024)*sizeof(*replay)))) break;
                replay[replay_count++]=s;
            }
            if (f) fclose(f);
            printf("Runtime: pad replay of %zu samples from %s\n",replay_count,path ? path : "(unset)");
        }
        replay_start=now_us();
        replay_next=0;
    }
    const uint32_t ms=(uint32_t)((now_us()-replay_start)/1000);
    while (replay_next<replay_count && replay[replay_next].ms<=ms) ++replay_next;
    if (!replay_next) return;
    if (replay_next==replay_count && ms>replay[replay_count-1].ms+500) {
        if (replay_armed==1) { puts("Runtime: pad replay finished"); replay_armed=2; }
        return;
    }
    const PadSample *s=&replay[replay_next-1];
    d->buttons=s->buttons;
    d->left_x=s->axes[0]; d->left_y=s->axes[1]; d->right_x=s->axes[2]; d->right_y=s->axes[3];
    d->l2=s->l2; d->r2=s->r2;
}
/* DualShock 4 touch samples keep a non-zero id for the lifetime of each contact and report
 * how long the oldest contact has been held. Bloodborne ignores synthetic touchpad presses
 * whose id and hold time stay zero, which made the gesture/personal-effects menus unreliable
 * for Tab, Back/Select and remapped touchpad buttons. */
static void touch_ids(PadData *d) {
    static uint8_t next_id=1, ids[2];
    static int down[2];
    static uint64_t since;
    for (int i=0;i<2;++i) {
        const int now=i<d->touch_count;
        if (now && !down[i]) {
            ids[i]=next_id;
            next_id=next_id==127 ? 1 : (uint8_t)(next_id+1);
        }
        down[i]=now;
        if (now) d->touches[i].id=ids[i];
    }
    if (!d->touch_count) since=0;
    else if (!since) since=d->timestamp;
    d->touch_held_time=d->touch_count ? (uint32_t)(d->timestamp-since) : 0;
}
static void sample(PadData *d) {
    sample_host(d);
    if (bbgpu_overlay_captures_input()) {
        touch_ids(d); /* also releases the previous synthetic contact */
        return;
    }
    record_sample(d);
    read_inject();
    replay_sample(d);
    d->buttons|=injected.buttons;
    if (injected.touch_side>=0) touch_click(d,injected.touch_side);
    else if ((d->buttons & BTN_TOUCHPAD) && !d->touch_count) touch_click(d,0);
    if (injected.buttons & BTN_L2) d->l2=255;
    if (injected.buttons & BTN_R2) d->r2=255;
    uint8_t *axes[4]={&d->left_x,&d->left_y,&d->right_x,&d->right_y};
    for (int i=0;i<4;++i) if (injected.stick[i]>=0) *axes[i]=(uint8_t)injected.stick[i];
    touch_ids(d);
}

static ABI int32_t pad_init(void) { host_lock(&lock); initialized=1; host_unlock(&lock); return 0; }
static ABI int32_t pad_open(int32_t user, int32_t type, int32_t index, const void *param) {
    (void)param;
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (user!=1) return ERR_INVALID_ARG;
    if (type!=0 && type!=2) return ERR_INVALID_ARG; /* standard / special port */
    if (index) return ERR_INVALID_ARG;
    host_lock(&lock);
    int already=opened; opened=1;
    host_unlock(&lock);
    if (already) return ERR_ALREADY_OPENED;
    puts("Runtime: pad opened for user 1 (SDL gamepad or keyboard)");
    return PAD_HANDLE;
}
static ABI int32_t pad_close(int32_t handle) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    opened=0; return 0;
}
static ABI int32_t pad_read_state(int32_t handle, PadData *data) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data) return ERR_INVALID_ARG;
    host_lock(&lock);
    sample(data); ++reads;
    host_unlock(&lock);
    return 0;
}
/* Buffered read: the port samples once per call, so one entry is returned. */
static ABI int32_t pad_read(int32_t handle, PadData *data, int32_t count) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data || count<1 || count>64) return ERR_INVALID_ARG;
    pad_read_state(handle,data);
    return 1;
}
static ABI int32_t pad_info(int32_t handle, ControllerInfo *info) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!info) return ERR_INVALID_ARG;
    memset(info,0,sizeof(*info));
    info->pixel_density=44.86f; info->resolution_x=1920; info->resolution_y=943;
    info->dead_zone_left=info->dead_zone_right=2;
    info->connection_type=0; info->connected=1; info->device_class=0;
    host_lock(&lock);
    current_gamepad();
    info->connected_count=connected_count ? connected_count : 1;
    host_unlock(&lock);
    return 0;
}
static ABI int32_t pad_vibration(int32_t handle, const uint8_t *param) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!param) return ERR_INVALID_ARG;
    host_lock(&lock);
    SDL_Gamepad *g=current_gamepad();
    if (g) SDL_RumbleGamepad(g,(uint16_t)(param[0]*257),(uint16_t)(param[1]*257),1000);
    host_unlock(&lock);
    return 0;
}
static ABI int32_t pad_ok_handle(int32_t handle) { return handle==PAD_HANDLE && opened ? 0 : ERR_INVALID_HANDLE; }
static ABI int32_t pad_ok_handle_flag(int32_t handle, uint8_t flag) { (void)flag; return pad_ok_handle(handle); }
static ABI int32_t pad_reset_orientation(int32_t handle) {
    if (pad_ok_handle(handle)) return ERR_INVALID_HANDLE;
    host_lock(&lock);
    motion_orientation[0]=motion_orientation[1]=motion_orientation[2]=0.0f;
    motion_orientation[3]=1.0f;
    motion_last_us=0;
    host_unlock(&lock);
    return 0;
}

static const RuntimeExport exports[]={
    {"scePadInit",pad_init}, {"scePadOpen",pad_open}, {"scePadClose",pad_close},
    {"scePadReadState",pad_read_state}, {"scePadRead",pad_read},
    {"scePadGetControllerInformation",pad_info}, {"scePadSetVibration",pad_vibration},
    {"scePadResetOrientation",pad_reset_orientation},
    {"scePadSetAngularVelocityDeadbandState",pad_ok_handle_flag}, {"scePadSetTiltCorrectionState",pad_ok_handle_flag},
    {"scePadSetMotionSensorState",pad_ok_handle_flag},
};
uintptr_t runtime_pad_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_pad_report(void) { printf("Runtime: pad reads=%zu, gamepad=%s\n",reads,gamepad ? SDL_GetGamepadName(gamepad) : "none"); }
