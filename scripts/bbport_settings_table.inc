// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: every bbport.ini setting and its default, in one place. The game includes this file
// (gpu/shim/bbport_settings: the defaults are applied before bbport.ini is read, and the game
// effects list comes from here); the Linux launcher (launcher/bbport_launcher.py) and the Windows
// setup (tools/setup/BbportSetup.cs) read its lines. Keep one entry per line, with plain string
// literals.
//
// BB_SETTING(key, default): the default as bbport.ini writes it.
// BB_EFFECT(key, menu label, default 0/1): game effects, switched by patches at start
// (scripts/patches.py EFFECTS); the label is English (the menus translate it).

BB_SETTING("upscaler", "fsr3")
BB_SETTING("preset", "0")
BB_SETTING("sharpen", "1")
BB_SETTING("sharpness", "0.30")
BB_SETTING("jitter", "1")
BB_SETTING("reactive", "0")
BB_SETTING("object_motion", "1")
BB_SETTING("reactive_scale", "1.00")
BB_SETTING("reactive_threshold", "0.20")
BB_SETTING("reactive_max", "0.90")
BB_SETTING("debug_view", "0")
BB_SETTING("show_fps", "0")
BB_SETTING("menu_language", "en")
BB_SETTING("fsr4_auto_exposure", "1")
BB_SETTING("fsr4_invert_jitter", "0")
BB_SETTING("model_lod", "0")
BB_SETTING("output_res", "1920x1080")
BB_SETTING("fullscreen", "0")
BB_SETTING("live_resolution", "0")
// The confirm button (run_game.py): auto follows the dump's region as its consoles are set up
// (Japanese, Asian and Korean dumps confirm and act with Circle), circle or cross forces one.
BB_SETTING("enter_button", "auto")
// Keyboard and mouse (gpu/shim/bbport_input.h). mouse_camera: the captured mouse turns the
// camera directly (F1 releases it); sensitivity 1.00 is 0.001 radian per mouse count.
BB_SETTING("mouse_camera", "1")
BB_SETTING("mouse_sensitivity", "1.00")
BB_SETTING("mouse_invert_y", "0")
// While key_walk is held, the movement keys push the left stick this far (0.1-0.9): walking.
BB_SETTING("walk_stick", "0.40")
// key_*: the inputs that hold a pad control, comma-separated; any of them holds it. An input is
// a key (SDL's name in lower case, spaces as _: left_alt, space, return, up, f) or mouse_left,
// mouse_right, mouse_middle, mouse_x1, mouse_x2, wheel_up, wheel_down; shift+, ctrl+ and alt+
// (or key+) in front require that key too, and such a chord wins over the same input alone.
BB_SETTING("key_cross", "space")
BB_SETTING("key_circle", "left_shift")
BB_SETTING("key_square", "e")
BB_SETTING("key_triangle", "q")
BB_SETTING("key_l1", "1")
BB_SETTING("key_r1", "3,mouse_left")
BB_SETTING("key_l2", "r,mouse_right")
BB_SETTING("key_r2", "f,shift+mouse_left")
BB_SETTING("key_l3", "z")
BB_SETTING("key_r3", "c,mouse_middle,left_alt")
BB_SETTING("key_options", "return")
BB_SETTING("key_dpad_up", "i")
BB_SETTING("key_dpad_down", "k")
BB_SETTING("key_dpad_left", "j")
BB_SETTING("key_dpad_right", "l")
BB_SETTING("key_touchpad_left", "tab")
BB_SETTING("key_touchpad_right", "backspace")
BB_SETTING("key_move_up", "w")
BB_SETTING("key_move_down", "s")
BB_SETTING("key_move_left", "a")
BB_SETTING("key_move_right", "d")
BB_SETTING("key_camera_up", "up")
BB_SETTING("key_camera_down", "down")
BB_SETTING("key_camera_left", "left")
BB_SETTING("key_camera_right", "right")
BB_SETTING("key_walk", "left_ctrl")

BB_EFFECT("effect_chromatic_aberration", "Chromatic aberration", 1)
BB_EFFECT("effect_dof", "Depth of field (DoF)", 1)
BB_EFFECT("effect_motion_blur", "Motion blur", 1)
BB_EFFECT("effect_ssao", "SSAO ambient occlusion", 1)
BB_EFFECT("effect_game_aa", "The game's own anti-aliasing", 1)
BB_EFFECT("effect_dynamic_shadows", "Shadows from dynamic lights", 1)
BB_EFFECT("effect_ssr", "SSR reflections (not in the original game)", 0)
BB_EFFECT("skip_intro", "Skip intro videos at startup", 0)
BB_EFFECT("debug_camera", "Free camera (Cross + L3)", 0)
BB_EFFECT("debug_menu", "Debug menu (needs font files)", 0)
