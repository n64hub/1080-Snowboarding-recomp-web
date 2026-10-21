// Draw distance setting.
//
// The game limits how far you can see in three places, all in bootup.uso:
//  - The land object draws only a window of rows of the course grid around the
//    camera (by default 20 rows ahead) and stores the first and last row in the
//    camera at +0x10C and +0x110. Terrain and the trees baked into it pop in at
//    the edge of that window.
//  - The camera's far plane (per course, 16000 by default). Fog is placed
//    between the near and far planes, so moving the far plane also moves the fog.
//  - Positioned objects (other riders, the helicopter, balloons, effects) are
//    culled past per-class distances that the camera rescales every frame.
// recomp/us.toml hooks each place and calls the functions below.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <variant>

#include "recomp.h"
#include "recompui/config.h"

namespace {
enum class DrawDistance : uint32_t {
    Original = 0,
    Extended = 1,
    Maximum = 2,
};

constexpr const char* option_id = "draw_distance";
constexpr int32_t last_row = 127;  // the grid has 128 rows

std::atomic<uint32_t> current_level{ static_cast<uint32_t>(DrawDistance::Extended) };

DrawDistance level() {
    return static_cast<DrawDistance>(current_level.load(std::memory_order_relaxed));
}

void refresh_level() {
    auto& config = recompui::config::get_graphics_config();
    if (config.has_option(option_id)) {
        current_level.store(std::get<uint32_t>(config.get_option_value(option_id)), std::memory_order_relaxed);
    }
}
}

namespace game {
void add_draw_distance_option(recomp::config::Config& graphics) {
    graphics.add_enum_option(
        option_id,
        "Draw Distance",
        "How far ahead the course, trees and other riders are drawn. "
        "<recomp-color primary>Original</recomp-color> matches the N64. "
        "<recomp-color primary>Extended</recomp-color> draws about three times as far and pushes the fog back to match. "
        "<recomp-color primary>Maximum</recomp-color> draws the whole course with almost no fog.",
        {
            { DrawDistance::Original, "Original" },
            { DrawDistance::Extended, "Extended" },
            { DrawDistance::Maximum, "Maximum" },
        },
        DrawDistance::Extended);
}
}

// Called once per frame by the land object, after it has chosen its row window.
extern "C" void game_draw_rows(uint8_t* rdram, recomp_context* ctx) {
    refresh_level();

    // $a1 holds the camera the first row went to, $t5 the one the last row went to.
    gpr first_cam = ctx->r5;
    gpr last_cam = ctx->r13;
    int32_t first = MEM_W(0x10C, first_cam);
    int32_t last = MEM_W(0x110, last_cam);

    switch (level()) {
        case DrawDistance::Original:
            return;
        case DrawDistance::Extended: {
            // The default window reaches 20 rows ahead; add 40 more on each side.
            constexpr int32_t extra_rows = 40;
            first = std::max(first - extra_rows, 0);
            last = std::min(last + extra_rows, last_row);
            break;
        }
        case DrawDistance::Maximum:
            first = 0;
            last = last_row;
            break;
    }

    MEM_W(0x10C, first_cam) = first;
    MEM_W(0x110, last_cam) = last;
}

extern "C" float game_far_scale(void) {
    switch (level()) {
        case DrawDistance::Extended:
            return 3.0f;
        case DrawDistance::Maximum:
            return 8.0f;
        default:
            return 1.0f;
    }
}

extern "C" float game_cull_scale(void) {
    switch (level()) {
        case DrawDistance::Extended:
            return 3.0f;
        case DrawDistance::Maximum:
            return 8.0f;
        default:
            return 1.0f;
    }
}
