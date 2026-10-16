# Assets

Art the port loads at run time, from `assets/` in the working directory (`BB_ASSETS_DIR`:
another folder). Packaged builds copy this folder (packaging/default.nix).

| File | Used by | Notes |
|---|---|---|
| `cursor/arrow.webp` | `tools/make_cursor.py` | Source art for the mouse cursor (supplied by in0brain). |
| `cursor/arrow.png` | `gpu/shim/bbport_cursor.cpp` | Master cursor, 256 px tall: alpha cleaned and cropped from the source. The game scales it to the window (36 px per 1080 lines). The hotspot is the arrow's tip, the leftmost opaque pixel of the topmost opaque row. |

After changing the source art, rebuild the PNG (needs `dwebp` from libwebp; in MSYS2 it is part
of CLANG64):

```
python tools/make_cursor.py
```
