// bbport: the game window. Created by the VideoOut driver on first open; the
// event pump runs on the port's window thread (see window.cpp).
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include "common/types.h"

struct SDL_Window;

namespace Frontend {

enum class WindowSystemType : u8 { Headless, Windows, X11, Wayland, Metal };

struct WindowSystemInfo {
    void* display_connection = nullptr;
    void* render_surface = nullptr;
    float render_surface_scale = 1.0f;
    WindowSystemType type = WindowSystemType::Headless;
};

class WindowSDL {
public:
    WindowSDL(s32 width, s32 height, const char* title);
    ~WindowSDL();
    s32 GetWidth() const { return width.load(std::memory_order_relaxed); }
    s32 GetHeight() const { return height.load(std::memory_order_relaxed); }
    SDL_Window* GetSDLWindow() const { return window; }
    WindowSystemInfo GetWindowInfo() const { return window_info; }
    bool IsOpen() const { return is_open.load(std::memory_order_relaxed); }
    /// Processes pending window events. Returns false once the user closed the window.
    bool PollEvents();
    /// Keyboard text entry for the system IME dialog; typed text shows in the title bar.
    void BeginTextInput(const std::string& initial, const std::string& prompt);
    /// 0 while typing, 1 confirmed (Enter), 2 cancelled (Escape); text is UTF-8.
    int PollTextInput(std::string& text);
    /// Mouse look (bbport addition): motion accumulated since the previous call, the buttons
    /// held (bit 0 left, 1 middle, 2 right, 3 X1, 4 X2) and the wheel ticks; 1 while the
    /// relative capture is active. The settings menu releases the cursor, so it returns 0 then.
    int ConsumeMouse(float& dx, float& dy, unsigned& buttons, float& wheel);

private:
    std::atomic<s32> width, height;
    std::atomic<bool> is_open{true};
    std::atomic<bool> mouse_look{true};      ///< BB_MOUSE_LOOK=0 turns the mouse look off
    std::atomic<bool> mouse_relative{false}; ///< applied SDL relative-mouse state
    std::atomic<float> mouse_dx{0.0f}, mouse_dy{0.0f}, mouse_wheel{0.0f};
    std::atomic<unsigned> mouse_buttons{0};
    bool window_focused{true};
    std::mutex text_mutex;
    bool text_requested{}, text_active{};
    int text_state{};
    std::string text, text_prompt, base_title;
    void UpdateTextTitle();
    void UpdateMouseCapture();
    SDL_Window* window{};
    WindowSystemInfo window_info{};
};

} // namespace Frontend
