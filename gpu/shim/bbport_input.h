// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: keyboard and mouse for the game (bbport.ini key_* bindings and mouse_camera). The
// window thread observes SDL's events and decides the mouse capture; the pad thread samples the
// bindings and takes the mouse motion through bbgpu_keyboard_mouse (runtime_pad.c), and the
// camera follows the captured mouse (runtime_camera.c).
#pragma once

union SDL_Event;
struct SDL_Window;

namespace BbInput {

/// Every window event, before the menu or the text entry may take it (window thread).
void Observe(const SDL_Event& event);
/// Relative mouse mode while the camera may use the mouse: mouse_camera on, not released with
/// F1, the window focused, neither the menu nor a text entry open (window thread, every pump).
void UpdateCapture(SDL_Window* window, bool text_entry);
/// The mouse is captured for the camera.
bool Captured();

} // namespace BbInput
