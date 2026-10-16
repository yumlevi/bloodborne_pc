// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_cursor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <SDL3/SDL.h>

namespace BbCursor {

namespace {

// Cursor height per 1080 window lines: a little taller than a desktop arrow, for the bevel.
constexpr float HeightAt1080 = 36.0f;
constexpr int MinHeight = 20, MaxHeight = 128;

SDL_Surface* master = nullptr; // RGBA32; nullptr when the art is missing
float hot_x = 0.0f, hot_y = 0.0f; // the tip, in master pixels
bool loaded = false;
SDL_Cursor* cursor = nullptr;
int cursor_height = 0;
std::atomic<bool> active{false};

std::string Path() {
    const char* dir = std::getenv("BB_ASSETS_DIR");
    return std::string{dir && dir[0] ? dir : "assets"} + "/cursor/arrow.png";
}

void Load() {
    loaded = true;
    const std::string path = Path();
    SDL_Surface* image = SDL_LoadPNG(path.c_str());
    if (!image) {
        std::printf("Cursor: %s not loaded (%s), the system cursor stays\n", path.c_str(), SDL_GetError());
        return;
    }
    master = SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(image);
    if (!master) {
        return;
    }
    // The hotspot is the arrow's tip: the leftmost opaque pixel of the topmost opaque row.
    for (int y = 0; y < master->h; ++y) {
        const auto* row = static_cast<const Uint8*>(master->pixels) + y * master->pitch;
        for (int x = 0; x < master->w; ++x) {
            if (row[x * 4 + 3] >= 128) {
                hot_x = x + 0.5f;
                hot_y = y + 0.5f;
                return;
            }
        }
    }
}

struct Tap {
    int source;
    float weight;
};

/// Per target pixel, the source pixels it covers and how much (area averaging).
std::vector<std::vector<Tap>> Taps(int source, int target) {
    std::vector<std::vector<Tap>> taps(target);
    const float scale = float(source) / float(target);
    for (int t = 0; t < target; ++t) {
        const float lo = t * scale, hi = (t + 1) * scale;
        for (int s = int(lo); s < source && s < hi; ++s) {
            const float w = std::min(hi, s + 1.0f) - std::max(lo, float(s));
            if (w > 0.0f) {
                taps[t].push_back({s, w / scale});
            }
        }
    }
    return taps;
}

/// The master scaled down to height lines. Premultiplied, so the transparent pixels' colour
/// does not bleed into the edge.
SDL_Surface* Scale(int height) {
    const int width = std::max(1, int(std::lround(float(master->w) * height / master->h)));
    SDL_Surface* out = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!out) {
        return nullptr;
    }
    const auto tx = Taps(master->w, width), ty = Taps(master->h, height);
    std::vector<float> rows(size_t(master->h) * width * 4); // scaled horizontally
    for (int y = 0; y < master->h; ++y) {
        const auto* in = static_cast<const Uint8*>(master->pixels) + y * master->pitch;
        for (int x = 0; x < width; ++x) {
            float* acc = &rows[(size_t(y) * width + x) * 4];
            for (const Tap& tap : tx[x]) {
                const Uint8* p = in + tap.source * 4;
                const float a = p[3] / 255.0f * tap.weight;
                acc[0] += p[0] * a;
                acc[1] += p[1] * a;
                acc[2] += p[2] * a;
                acc[3] += a;
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        auto* line = static_cast<Uint8*>(out->pixels) + y * out->pitch;
        for (int x = 0; x < width; ++x) {
            float acc[4] = {};
            for (const Tap& tap : ty[y]) {
                const float* p = &rows[(size_t(tap.source) * width + x) * 4];
                for (int c = 0; c < 4; ++c) {
                    acc[c] += p[c] * tap.weight;
                }
            }
            Uint8* q = line + x * 4;
            if (acc[3] < 1.0f / 512.0f) {
                q[0] = q[1] = q[2] = q[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                q[c] = Uint8(std::clamp(std::lround(acc[c] / acc[3]), 0L, 255L));
            }
            q[3] = Uint8(std::clamp(std::lround(acc[3] * 255.0f), 0L, 255L));
        }
    }
    return out;
}

} // namespace

void Update(SDL_Window* window) {
    if (!loaded) {
        Load();
    }
    int width = 0, height = 0;
    if (!master || !SDL_GetWindowSizeInPixels(window, &width, &height) || height <= 0) {
        return;
    }
    const int size = std::clamp(int(std::lround(height * HeightAt1080 / 1080.0f)), MinHeight, MaxHeight);
    if (size == cursor_height) {
        return;
    }
    cursor_height = size;
    SDL_Surface* image = Scale(size);
    if (!image) {
        return;
    }
    const float scale = float(size) / float(master->h);
    const int x = std::clamp(int(hot_x * scale), 0, image->w - 1);
    const int y = std::clamp(int(hot_y * scale), 0, image->h - 1);
    SDL_Cursor* next = SDL_CreateColorCursor(image, x, y);
    SDL_DestroySurface(image);
    if (!next) {
        std::printf("Cursor: no %dpx cursor (%s)\n", size, SDL_GetError());
        return;
    }
    SDL_SetCursor(next);
    if (cursor) {
        SDL_DestroyCursor(cursor);
    }
    cursor = next;
    active = true;
}

bool Active() {
    return active;
}

} // namespace BbCursor
