// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_input.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "../bbgpu.h"
#include "bbport_overlay.h"
#include "bbport_settings.h"

namespace BbInput {

namespace {

static_assert(BbSettings::BindingCount == BBGPU_PAD_CONTROL_COUNT, "key_* bindings and pad controls");

// Inputs: SDL scancodes, then the mouse buttons (SDL button - 1), the wheel and the
// either-side modifiers.
constexpr int MouseBase = SDL_SCANCODE_COUNT;
constexpr int MouseButtons = 8;
constexpr int WheelUp = MouseBase + MouseButtons, WheelDown = WheelUp + 1;
constexpr int AnyShift = WheelDown + 1, AnyCtrl = AnyShift + 1, AnyAlt = AnyCtrl + 1;
constexpr int InputCount = AnyAlt + 1;
constexpr int MaxModifiers = 3;
// A wheel step is one press of the control its binding names: held, then released, before the
// next queued step (several steps switch several items).
constexpr Uint64 WheelPressNs = 50'000'000, WheelGapNs = 40'000'000;
constexpr size_t WheelQueueMax = 6;
// A second input pressing a control another input already holds (Space while the side button
// sprints) presses it again: released this long first, as a quick double tap on a pad.
constexpr Uint64 RepressNs = 50'000'000;
// The device in use decides the free cursor: a pad's button, a stick pushed or a trigger pulled
// past this (of 32767; drift stays below) hides it; a key, a click, the wheel or a mouse move
// of MouseMoveMin points shows it. Motion this soon after the capture changes or the pointer is
// warped onto a menu's selection is SDL's, not the player's.
constexpr int PadAxisPress = 12000;
constexpr float MouseMoveMin = 2.0f;
constexpr Uint64 SyntheticMotionNs = 200'000'000;

struct Binding {
    int trigger;
    int modifiers[MaxModifiers];
    int modifier_count;
};

std::atomic<uint64_t> keys[(SDL_SCANCODE_COUNT + 63) / 64];
std::atomic<uint32_t> mouse_buttons{0}; // bit (SDL button - 1)
std::atomic<uint32_t> mouse_ignored{0}; // held when the capture began: ignored until released
std::atomic<bool> captured{false}, released{false};
std::atomic<bool> menu_active{false}; // runtime_menu.c: a menu takes the free mouse
std::atomic<bool> warp_pending{false};
std::atomic<float> warp_x{0.0f}, warp_y{0.0f}; // where the pointer starts in the menu (window points)
std::mutex wheel_mutex;
std::deque<uint32_t> wheel_queue; // controls of the wheel steps not pressed yet
uint32_t wheel_held = 0;          // controls of the step being pressed
Uint64 wheel_start = 0, wheel_free = 0;
std::mutex motion_mutex;
float motion_x = 0.0f, motion_y = 0.0f;
std::atomic<uint32_t> probe_requests{0}, trace_requests{0}; // F7, F8: menu probe and trace (runtime_menu.c)
std::atomic<float> pointer_x{0.0f}, pointer_y{0.0f};
std::atomic<int> window_w{0}, window_h{0};
// Window thread only: Observe() and UpdateCapture().
bool pad_in_use = false;     // the last input came from a pad: the cursor is hidden
bool cursor_hidden = false;  // SDL_HideCursor() is in effect
Uint64 motion_quiet_until = 0;

std::once_flag parsed;
std::vector<Binding> bindings[BBGPU_PAD_CONTROL_COUNT];

void SetBit(std::atomic<uint64_t>* bits, int index, bool on) {
    const uint64_t mask = uint64_t(1) << (index % 64);
    if (on) {
        bits[index / 64].fetch_or(mask);
    } else {
        bits[index / 64].fetch_and(~mask);
    }
}

void DropMotion() {
    std::scoped_lock lock{motion_mutex};
    motion_x = motion_y = 0.0f;
}

/// An input's name in bbport.ini; -1 when unknown.
int ParseInput(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    static const std::pair<const char*, int> named[] = {
        {"mouse_left", MouseBase + SDL_BUTTON_LEFT - 1},
        {"mouse_middle", MouseBase + SDL_BUTTON_MIDDLE - 1},
        {"mouse_right", MouseBase + SDL_BUTTON_RIGHT - 1},
        {"mouse_x1", MouseBase + SDL_BUTTON_X1 - 1},
        {"mouse_x2", MouseBase + SDL_BUTTON_X2 - 1},
        {"wheel_up", WheelUp}, {"wheel_down", WheelDown},
        {"shift", AnyShift}, {"ctrl", AnyCtrl}, {"alt", AnyAlt}};
    for (const auto& [text, input] : named) {
        if (name == text) {
            return input;
        }
    }
    std::replace(name.begin(), name.end(), '_', ' ');
    const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
    return code == SDL_SCANCODE_UNKNOWN ? -1 : int(code);
}

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    return first == std::string::npos ? std::string{} : text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

void Parse() {
    const auto& settings = BbSettings::Get();
    int total = 0;
    for (int control = 0; control < BBGPU_PAD_CONTROL_COUNT; ++control) {
        const std::string& list = settings.bindings[control];
        size_t start = 0;
        while (start <= list.size()) {
            const size_t comma = std::min(list.find(',', start), list.size());
            const std::string entry = Trim(list.substr(start, comma - start));
            start = comma + 1;
            if (entry.empty()) {
                continue;
            }
            std::vector<int> inputs;
            size_t part = 0;
            bool valid = true;
            while (part <= entry.size()) {
                const size_t plus = std::min(entry.find('+', part), entry.size());
                const std::string name = Trim(entry.substr(part, plus - part));
                part = plus + 1;
                const int input = ParseInput(name);
                if (input < 0) {
                    std::printf("Input: unknown key '%s' in %s\n", name.c_str(),
                                BbSettings::BindingKeys[control]);
                    valid = false;
                    break;
                }
                inputs.push_back(input);
            }
            if (!valid || inputs.size() > MaxModifiers + 1) {
                continue;
            }
            Binding binding{inputs.back(), {}, int(inputs.size()) - 1};
            std::copy(inputs.begin(), inputs.end() - 1, binding.modifiers);
            bindings[control].push_back(binding);
            ++total;
        }
    }
    std::printf("Input: %d keyboard and mouse bindings (bbport.ini key_*)\n", total);
}

/// Whether a key or mouse button is held; the wheel is never held (its steps are queued presses).
bool Down(int input) {
    if (input < SDL_SCANCODE_COUNT) {
        return (keys[input / 64].load(std::memory_order_relaxed) >> (input % 64)) & 1;
    }
    switch (input) {
    case AnyShift:
        return Down(SDL_SCANCODE_LSHIFT) || Down(SDL_SCANCODE_RSHIFT);
    case AnyCtrl:
        return Down(SDL_SCANCODE_LCTRL) || Down(SDL_SCANCODE_RCTRL);
    case AnyAlt:
        return Down(SDL_SCANCODE_LALT) || Down(SDL_SCANCODE_RALT);
    case WheelUp:
    case WheelDown:
        return false;
    default:
        break;
    }
    // The mouse acts in the game only while it is captured (a click that focuses the window,
    // or one on the free cursor after F1, is not an attack). In a menu the left and right
    // buttons point and click (runtime_menu.c); the others keep their bindings.
    if (!captured.load(std::memory_order_relaxed)) {
        const int button = input - MouseBase + 1;
        if (!menu_active.load(std::memory_order_relaxed) || button == SDL_BUTTON_LEFT || button == SDL_BUTTON_RIGHT) {
            return false;
        }
    }
    const uint32_t held = mouse_buttons.load(std::memory_order_relaxed) & ~mouse_ignored.load(std::memory_order_relaxed);
    return (held >> (input - MouseBase)) & 1;
}

/// The controls an input would press now: its bindings whose modifiers are held, and of those
/// the ones with the most modifiers (a chord wins over the same input alone).
uint32_t ControlsFor(int trigger) {
    int most = -1;
    uint32_t controls = 0;
    for (int control = 0; control < BBGPU_PAD_CONTROL_COUNT; ++control) {
        for (const Binding& b : bindings[control]) {
            bool on = b.trigger == trigger;
            for (int m = 0; on && m < b.modifier_count; ++m) {
                on = Down(b.modifiers[m]);
            }
            if (!on || b.modifier_count < most) {
                continue;
            }
            if (b.modifier_count > most) {
                most = b.modifier_count;
                controls = 0;
            }
            controls |= 1u << control;
        }
    }
    return controls;
}

void DropWheel() {
    std::scoped_lock lock{wheel_mutex};
    wheel_queue.clear();
    wheel_held = 0;
}

/// The controls of the wheel step being pressed: each queued step is held for WheelPressNs,
/// then released for WheelGapNs before the next (pad thread).
uint32_t WheelControls(Uint64 now) {
    std::scoped_lock lock{wheel_mutex};
    if (wheel_held && now >= wheel_start + WheelPressNs) {
        wheel_held = 0;
        wheel_free = wheel_start + WheelPressNs + WheelGapNs;
    }
    if (!wheel_held && now >= wheel_free && !wheel_queue.empty()) {
        wheel_held = wheel_queue.front();
        wheel_queue.pop_front();
        wheel_start = now;
    }
    return wheel_held;
}

void UsePad(bool pad) {
    if (pad != pad_in_use) {
        pad_in_use = pad;
        std::printf("Input: %s\n", pad ? "a pad is in use: the cursor hides" : "keyboard and mouse in use: the cursor shows");
    }
}

} // namespace

void Observe(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (event.key.scancode < SDL_SCANCODE_COUNT) {
            SetBit(keys, event.key.scancode, event.type == SDL_EVENT_KEY_DOWN);
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat) {
            const bool now_released = !released.load();
            released = now_released;
            std::printf("Input: mouse camera %s (F1)\n", now_released ? "released" : "captures the mouse again");
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            UsePad(false);
            if (event.key.key == SDLK_F7) {
                ++probe_requests;
            } else if (event.key.key == SDLK_F8) {
                ++trace_requests;
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button >= 1 && event.button.button <= MouseButtons) {
            const uint32_t bit = 1u << (event.button.button - 1);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                mouse_buttons.fetch_or(bit);
                UsePad(false);
            } else {
                mouse_buttons.fetch_and(~bit);
                mouse_ignored.fetch_and(~bit);
            }
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        // The step's control is decided now, with the modifiers held at this moment.
        const float y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        if (y != 0.0f) {
            UsePad(false);
        }
        if (y == 0.0f || !(captured || menu_active)) {
            break;
        }
        std::call_once(parsed, Parse);
        if (const uint32_t controls = ControlsFor(y > 0.0f ? WheelUp : WheelDown)) {
            std::scoped_lock lock{wheel_mutex};
            if (wheel_queue.size() < WheelQueueMax) {
                wheel_queue.push_back(controls);
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        pointer_x = event.motion.x;
        pointer_y = event.motion.y;
        if (captured) {
            std::scoped_lock lock{motion_mutex};
            motion_x += event.motion.xrel;
            motion_y += event.motion.yrel;
        }
        if (std::fabs(event.motion.xrel) + std::fabs(event.motion.yrel) >= MouseMoveMin &&
            SDL_GetTicksNS() >= motion_quiet_until) {
            UsePad(false);
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        UsePad(true);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (std::abs(int(event.gaxis.value)) >= PadAxisPress) {
            UsePad(true);
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Keys released elsewhere send no event here.
        for (auto& word : keys) {
            word = 0;
        }
        mouse_buttons = 0;
        mouse_ignored = 0;
        break;
    default:
        break;
    }
}

void UpdateCapture(SDL_Window* window, bool text_entry) {
    if (int w = 0, h = 0; SDL_GetWindowSize(window, &w, &h)) {
        window_w = w;
        window_h = h;
    }
    // The free cursor (menus, F1, the settings menu) is for the keyboard and mouse: while a pad
    // plays it stays hidden, until a key, a click, the wheel or a mouse move.
    if (pad_in_use != cursor_hidden) {
        cursor_hidden = pad_in_use;
        if (cursor_hidden) {
            SDL_HideCursor();
        } else {
            SDL_ShowCursor();
        }
    }
    const bool want = BbSettings::Get().mouse_camera && !released && !text_entry && !menu_active &&
                      !BbOverlay::CapturesInput() && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
    if (want == captured.load()) {
        return;
    }
    if (want) {
        mouse_ignored = mouse_buttons.load();
    }
    SDL_SetWindowRelativeMouseMode(window, want);
    DropMotion();
    DropWheel();
    captured = want;
    if (!want && warp_pending.exchange(false)) {
        SDL_WarpMouseInWindow(window, warp_x, warp_y);
    }
    motion_quiet_until = SDL_GetTicksNS() + SyntheticMotionNs;
}

bool Captured() {
    return captured;
}

} // namespace BbInput

extern "C" void bbgpu_keyboard_mouse(BbGpuKeyboardMouse* state) {
    using namespace BbInput;
    std::call_once(parsed, Parse);
    // Each held input presses the controls of its winning bindings (shift+mouse_left is R2
    // alone, not R1 as well); the wheel adds the step being pressed.
    const Uint64 now = SDL_GetTicksNS();
    std::array<uint8_t, BBGPU_PAD_CONTROL_COUNT> inputs{}; // inputs holding each control
    const auto add = [&](uint32_t controls) {
        for (int control = 0; control < BBGPU_PAD_CONTROL_COUNT; ++control) {
            inputs[control] += (controls >> control) & 1;
        }
    };
    add(WheelControls(now));
    std::array<bool, InputCount> seen{};
    for (int control = 0; control < BBGPU_PAD_CONTROL_COUNT; ++control) {
        for (const Binding& b : bindings[control]) {
            if (!seen[b.trigger]) {
                seen[b.trigger] = true;
                if (Down(b.trigger)) {
                    add(ControlsFor(b.trigger));
                }
            }
        }
    }
    // A control gaining an input while held is released for RepressNs, then pressed again
    // (pad thread only: the previous sample is this function's own state).
    static std::array<uint8_t, BBGPU_PAD_CONTROL_COUNT> previous{};
    static std::array<Uint64, BBGPU_PAD_CONTROL_COUNT> released_until{};
    uint32_t held = 0;
    for (int control = 0; control < BBGPU_PAD_CONTROL_COUNT; ++control) {
        if (previous[control] > 0 && inputs[control] > previous[control]) {
            released_until[control] = now + RepressNs;
        }
        previous[control] = inputs[control];
        if (inputs[control] > 0 && now >= released_until[control]) {
            held |= 1u << control;
        }
    }
    const auto& settings = BbSettings::Get();
    state->held = held;
    {
        std::scoped_lock lock{motion_mutex};
        state->mouse_dx = motion_x;
        state->mouse_dy = motion_y;
        motion_x = motion_y = 0.0f;
    }
    state->captured = captured ? 1 : 0;
    state->sensitivity = 0.001f * settings.mouse_sensitivity;
    state->invert_y = settings.mouse_invert_y ? 1 : 0;
    state->walk_scale = settings.walk_stick;
    state->probe_requests = probe_requests;
    state->trace_requests = trace_requests;
    state->free_buttons = captured ? 0 : mouse_buttons.load();
    state->pointer_x = pointer_x;
    state->pointer_y = pointer_y;
    state->window_w = window_w;
    state->window_h = window_h;
}

extern "C" void bbgpu_menu_active(int active, float x, float y) {
    using namespace BbInput;
    if (active && x >= 0.0f && y >= 0.0f) {
        warp_x = x;
        warp_y = y;
        pointer_x = x; // the pad thread sees it there at once (no motion: not a choice)
        pointer_y = y;
        warp_pending = true;
    }
    if (menu_active.exchange(active != 0) != (active != 0)) {
        std::printf("Input: %s\n", active ? "a menu takes the mouse" : "the menu gave the mouse back");
    }
}
