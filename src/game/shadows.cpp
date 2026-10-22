// Rider shadow setting.
//
// The game draws the rider's shadow by flattening the rider model onto the
// ground plane under them, along the direction of the course's sun
// (bootup_069370 builds the matrix). With a low sun the shadow lands far from
// the rider, and in the air it slides across the slope with the rider's height.
// "Under Rider" flattens along the vertical instead, so the shadow stays
// directly beneath the rider, and fades the shadow out with height (otherwise it lands on whatever is far below, even
// past a crest). recomp/us.toml has the hooks.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <unordered_map>
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
        "<recomp-color primary>Under Rider</recomp-color> keeps it directly beneath the rider and fades it out on big jumps.",
        {
            { ShadowMode::Original, "Original", "Original" },
            { ShadowMode::UnderRider, "UnderRider", "Under Rider" },
        },
        ShadowMode::Original);
}
}

namespace {
// The planes the game finds on the course are in the rider's coordinates times
// minus a scale the course sets (0.04 on the courses checked). It is only valid while riders update, so
// game_shadow_scale records it there for use at draw time.
std::atomic<float> world_scale{ 0.0f };
// Offsets in the rider object.
constexpr gpr rider_pos = 0xDC;          // x, y, z
constexpr gpr rider_ground_plane = 0x9D8;  // normal x, y, z and d, from a ray cast straight down

// Height of the rider above the ground plane under them, in course units.
float height_above_ground(uint8_t* rdram, gpr rider) {
    float k = -world_scale.load(std::memory_order_relaxed);
    gpr plane = rider + rider_ground_plane;
    float nx = load_float(rdram, plane), ny = load_float(rdram, plane + 4), nz = load_float(rdram, plane + 8), d = load_float(rdram, plane + 12);
    if (std::fabs(ny) < 1e-3f) {
        return 0.0f;
    }
    float px = load_float(rdram, rider + rider_pos), py = load_float(rdram, rider + rider_pos + 4), pz = load_float(rdram, rider + rider_pos + 8);
    return std::fabs((k * (nx * px + ny * py + nz * pz) + d) / ny);
}

// The shadow is fully dark up to fade_start above the ground and gone by fade_end.
// The rider's position is about 15 above the snow while riding; big jumps reach
// 120, and past a crest the ground under the rider can be 300 below.
constexpr float fade_start = 22.0f;
constexpr float fade_end = 50.0f;

// The plane under the rider can switch between nearby and far-off ground from
// one frame to the next (over a crest or a halfpipe wall), which made the
// shadow flash on and off. Each rider's fade therefore moves toward its target
// a step at a time: about a third of a second from fully dark to gone.
constexpr float fade_step = 0.1f;
struct RiderFade {
    float value = 1.0f;
    uint32_t last_call = 0;
};
std::unordered_map<uint32_t, RiderFade> rider_fades;
uint32_t fade_calls = 0;

// The shadow is flattened onto exactly the plane the game found under the
// rider. Easing that plane's tilt (v0.1.11) made the shadow sink into the
// snow in places and slide into place over a few frames, so it is not done.
}

// Called with the normalized shadow direction (x, y, z floats) at $sp + 0x58.
// The ground plane pointer is in the caller's argument slot at $sp + 0x74.
extern "C" void game_shadow_direction(uint8_t* rdram, recomp_context* ctx) {
    if (mode() != ShadowMode::UnderRider) {
        return;
    }
    gpr sp = ctx->r29;
    gpr dir = sp + 0x58;
    // Keep the sign of the vertical part: flipping the direction would flip the
    // sign of w in the shadow matrix and clip the shadow away.
    float y = load_float(rdram, dir + 0x4);
    store_float(rdram, dir + 0x0, 0.0f);
    store_float(rdram, dir + 0x4, std::signbit(y) ? -1.0f : 1.0f);
    store_float(rdram, dir + 0x8, 0.0f);
}

// Called with the course scale in $f6 while a rider checks how high it is above
// the ground.
extern "C" void game_shadow_scale(uint8_t* rdram, recomp_context* ctx) {
    world_scale.store(ctx->f6.fl, std::memory_order_relaxed);
}

// Called with the shadow's alpha in $f10, just before the game stores it
// ($s0 is the rider).
extern "C" void game_shadow_alpha(uint8_t* rdram, recomp_context* ctx) {
    if (mode() != ShadowMode::UnderRider) {
        return;
    }
    float h = height_above_ground(rdram, ctx->r16);
    float target = std::clamp((fade_end - h) / (fade_end - fade_start), 0.0f, 1.0f);
    fade_calls++;
    RiderFade& fade = rider_fades[static_cast<uint32_t>(ctx->r16)];
    if (fade.last_call == 0 || fade_calls - fade.last_call > 16) {
        fade.value = target;   // new rider, or one that was not drawn for a while
    }
    else {
        fade.value += std::clamp(target - fade.value, -fade_step, fade_step);
    }
    fade.last_call = fade_calls;
    ctx->f10.fl *= fade.value;
}
