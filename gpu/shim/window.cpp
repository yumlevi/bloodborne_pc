// bbport: SDL3 window for the Vulkan swapchain (X11, Wayland or Win32).
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "sdl_window.h"
#include "bbport_overlay.h"
#include "bbport_settings.h"

namespace Frontend {

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    // Gamepads are sampled by runtime_pad.c; their events are pumped here with the window's.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        UNREACHABLE_MSG("Failed to initialize SDL video: {}", SDL_GetError());
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width_);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height_);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    const char* fullscreen = std::getenv("BB_FULLSCREEN");
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
                           fullscreen ? fullscreen[0] == '1' : BbSettings::Get().fullscreen.load());
    base_title = title;
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    ASSERT_MSG(window, "Failed to create window: {}", SDL_GetError());
    // bbport: mouse look (runtime_pad.c). BB_MOUSE_LOOK=0 disables it.
    if (const char* look = std::getenv("BB_MOUSE_LOOK"); look && look[0] == '0') {
        mouse_look = false;
    }

    const char* driver = SDL_GetCurrentVideoDriver();
    const SDL_PropertiesID wp = SDL_GetWindowProperties(window);
#ifdef _WIN32
    if (driver && !std::strcmp(driver, "windows")) {
        window_info.type = WindowSystemType::Windows;
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    } else
#endif
    if (driver && !std::strcmp(driver, "x11")) {
        window_info.type = WindowSystemType::X11;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        window_info.render_surface = reinterpret_cast<void*>(SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    } else if (driver && !std::strcmp(driver, "wayland")) {
        window_info.type = WindowSystemType::Wayland;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        UNREACHABLE_MSG("Unsupported SDL video driver {}", driver ? driver : "(none)");
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    LOG_INFO(Frontend, "Window {}x{} on {}", w, h, driver);
}

WindowSDL::~WindowSDL() {
    SDL_DestroyWindow(window);
}

void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 0;
    text_requested = true;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    const std::string title = text_active ? base_title + " \u2014 " + text_prompt + ": " + text + "_  (Enter = OK, Esc = cancel)"
                                          : base_title;
    SDL_SetWindowTitle(window, title.c_str());
}

bool WindowSDL::PollEvents() {
    {
        std::scoped_lock lock{text_mutex};
        if (text_requested) { // SDL text input must be toggled from the window thread
            text_requested = false;
            text_active = true;
            SDL_StartTextInput(window);
            UpdateTextTitle();
        }
    }
    if (!text_active) {
        BbOverlay::UpdateTextInput(window);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (text_active && (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN)) {
            std::scoped_lock lock{text_mutex};
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                text += event.text.text;
            } else if (event.key.key == SDLK_BACKSPACE && !text.empty()) {
                size_t cut = text.size() - 1; // drop one UTF-8 code point
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                text.erase(cut);
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                text_state = event.key.key == SDLK_ESCAPE ? 2 : 1;
                text_active = false;
                SDL_StopTextInput(window);
            }
            UpdateTextTitle();
            continue;
        }
        if (BbOverlay::HandleEvent(event)) {
            continue;
        }
        switch (event.type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            width = w;
            height = h;
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            // F11: borderless fullscreen at the desktop size, or back to the window.
            if (event.key.key == SDLK_F11 && !event.key.repeat) {
                SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
            }
            break;
        // bbport: mouse look and mouse buttons for the pad (runtime_pad.c).
        case SDL_EVENT_MOUSE_MOTION:
            if (mouse_relative.load(std::memory_order_relaxed)) {
                mouse_dx.store(mouse_dx.load(std::memory_order_relaxed) + event.motion.xrel,
                               std::memory_order_relaxed);
                mouse_dy.store(mouse_dy.load(std::memory_order_relaxed) + event.motion.yrel,
                               std::memory_order_relaxed);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button >= 1 && event.button.button <= 5) {
                mouse_buttons.fetch_or(1u << (event.button.button - 1));
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button >= 1 && event.button.button <= 5) {
                mouse_buttons.fetch_and(~(1u << (event.button.button - 1)));
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            mouse_wheel.store(mouse_wheel.load(std::memory_order_relaxed) + event.wheel.y,
                              std::memory_order_relaxed);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            window_focused = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            window_focused = false;
            break;
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            is_open = false;
            break;
        default:
            break;
        }
    }
    UpdateMouseCapture();
    return is_open;
}

// bbport: relative mouse capture (unbounded camera motion, cursor hidden). The settings menu,
// the IME dialog and a lost window focus need the absolute cursor, so they release it.
void WindowSDL::UpdateMouseCapture() {
    const bool want = mouse_look.load(std::memory_order_relaxed) && window_focused && !text_active &&
                      !BbOverlay::CapturesInput();
    const bool have = mouse_relative.load(std::memory_order_relaxed);
    if (want == have || !window) {
        return;
    }
    mouse_relative.store(want, std::memory_order_relaxed);
    mouse_dx.store(0.0f, std::memory_order_relaxed);
    mouse_dy.store(0.0f, std::memory_order_relaxed);
    mouse_wheel.store(0.0f, std::memory_order_relaxed);
    if (!SDL_SetWindowRelativeMouseMode(window, want)) {
        LOG_WARNING(Frontend, "Relative mouse mode {} failed: {}", want ? "on" : "off",
                    SDL_GetError());
    }
}

int WindowSDL::ConsumeMouse(float& dx, float& dy, unsigned& buttons, float& wheel) {
    dx = mouse_dx.exchange(0.0f, std::memory_order_relaxed);
    dy = mouse_dy.exchange(0.0f, std::memory_order_relaxed);
    wheel = mouse_wheel.exchange(0.0f, std::memory_order_relaxed);
    buttons = mouse_buttons.load(std::memory_order_relaxed);
    return mouse_relative.load(std::memory_order_relaxed) ? 1 : 0;
}

} // namespace Frontend
