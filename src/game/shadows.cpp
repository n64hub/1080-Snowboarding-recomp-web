// Rider shadow setting.
//
// The game draws the rider's shadow by flattening the rider model onto the
// ground plane under them, along the direction of the course's sun
// (bootup_069370 builds the matrix). With a low sun the shadow lands far from
// the rider, and in the air it slides across the slope with the rider's height.
// "Under Rider" flattens along the vertical instead, so the shadow stays
// directly beneath the rider. recomp/us.toml hooks the point where the sun
// direction has just been normalized.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <variant>

#include "recomp.h"
#include "recompui/config.h"

namespace {
enum class ShadowMode : uint32_t {
    Original = 0,
    UnderRider = 1,
};

constexpr const char* option_id = "rider_shadow";

float load_float(uint8_t* rdram, gpr addr) {
    union { int32_t i; float f; } u;
    u.i = MEM_W(0, addr);
    return u.f;
}

void store_float(uint8_t* rdram, gpr addr, float value) {
    union { int32_t i; float f; } u;
    u.f = value;
    MEM_W(0, addr) = u.i;
}

ShadowMode mode() {
    auto& config = recompui::config::get_graphics_config();
    if (config.has_option(option_id)) {
        return static_cast<ShadowMode>(std::get<uint32_t>(config.get_option_value(option_id)));
    }
    return ShadowMode::Original;
}
}

namespace game {
void add_shadow_option(recomp::config::Config& graphics) {
    graphics.add_enum_option(
        option_id,
        "Rider Shadow",
        "Where the rider's shadow falls. "
        "<recomp-color primary>Original</recomp-color> casts it along the sun like the N64, so with a low sun it can land far from the rider and jump around in the air. "
        "<recomp-color primary>Under Rider</recomp-color> keeps it directly beneath the rider.",
        {
            { ShadowMode::Original, "Original" },
            { ShadowMode::UnderRider, "Under Rider" },
        },
        ShadowMode::Original);
}
}

// Called with the normalized shadow direction (x, y, z floats) at $sp + 0x58.
extern "C" void game_shadow_direction(uint8_t* rdram, recomp_context* ctx) {
    if (mode() != ShadowMode::UnderRider) {
        return;
    }
    gpr dir = ctx->r29 + 0x58;
    // Keep the sign of the vertical part: flipping the direction would flip the
    // sign of w in the shadow matrix and clip the shadow away.
    float y = load_float(rdram, dir + 0x4);
    store_float(rdram, dir + 0x0, 0.0f);
    store_float(rdram, dir + 0x4, std::signbit(y) ? -1.0f : 1.0f);
    store_float(rdram, dir + 0x8, 0.0f);
}
