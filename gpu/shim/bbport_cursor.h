// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the mouse cursor over the game window. The free cursor (F1, the settings menu,
// mouse_camera off) shows assets/cursor/arrow.png (BB_ASSETS_DIR: another assets folder),
// scaled to the window's height, as the system's hardware cursor.
#pragma once

struct SDL_Window;

namespace BbCursor {

/// Loads the art once and sets a cursor sized for the window (window thread, every pump).
void Update(SDL_Window* window);
/// The art's cursor is set: ImGui need not draw its own.
bool Active();

} // namespace BbCursor
