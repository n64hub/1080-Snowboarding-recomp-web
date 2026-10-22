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

// While the game builds the shadow matrix, the ground plane under the rider is
// raised a little toward the rider so the depth test (game_shadow_draw_end)
// doesn't lose the shadow in the snow it lies on, then put back.
// 0.5 still let bumps in the snow next to the triangle under the rider cut
// pieces out of the shadow; 2 (the rider's middle is about 15 up) does not.
constexpr float shadow_lift = 2.0f;
gpr lifted_plane = 0;
uint32_t lifted_plane_d = 0;

void restore_lifted_plane(uint8_t* rdram) {
    if (lifted_plane != 0) {
        MEM_W(12, lifted_plane) = lifted_plane_d;
        lifted_plane = 0;
    }
}
}

// Called with the normalized shadow direction (x, y, z floats) at $sp + 0x58.
// The ground plane pointer is in the caller's argument slot at $sp + 0x74.
extern "C" void game_shadow_direction(uint8_t* rdram, recomp_context* ctx) {
    restore_lifted_plane(rdram);
    gpr sp = ctx->r29;
    if (mode() == ShadowMode::UnderRider) {
        gpr dir = sp + 0x58;
        // Keep the sign of the vertical part: flipping the direction would flip the
        // sign of w in the shadow matrix and clip the shadow away.
        float y = load_float(rdram, dir + 0x4);
        store_float(rdram, dir + 0x0, 0.0f);
        store_float(rdram, dir + 0x4, std::signbit(y) ? -1.0f : 1.0f);
        store_float(rdram, dir + 0x8, 0.0f);
    }
    if (mode() != ShadowMode::UnderRider) {
        return;
    }

    // Raise the plane toward the rider. The plane is in the rider's
    // coordinates times the course scale, like in height_above_ground.
    gpr plane = static_cast<int32_t>(MEM_W(0x74, sp));
    gpr rider = plane - rider_ground_plane;   // the caller passes rider + 0x9D8
    float k = -world_scale.load(std::memory_order_relaxed);
    float nx = load_float(rdram, plane), ny = load_float(rdram, plane + 4), nz = load_float(rdram, plane + 8), d = load_float(rdram, plane + 12);
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (k == 0.0f || len < 1e-6f) {
        return;
    }
    float px = load_float(rdram, rider + rider_pos), py = load_float(rdram, rider + rider_pos + 4), pz = load_float(rdram, rider + rider_pos + 8);
    float side = k * (nx * px + ny * py + nz * pz) + d;
    lifted_plane = plane;
    lifted_plane_d = MEM_W(12, plane);
    store_float(rdram, plane + 12, d - std::copysign(shadow_lift * len, side));
}

// Called right after the shadow matrix is built.
extern "C" void game_shadow_matrix_built(uint8_t* rdram, recomp_context* ctx) {
    restore_lifted_plane(rdram);
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

namespace {
// The game's global draw state (bootup .data + 0x3C168). Its +0x254 -> +0x158
// is the current display list builder, whose +0xC points at {commands, count}.
constexpr uint32_t draw_state_offset = 0x3C168;
constexpr uint32_t bootup_data_section = 4;

bool is_rdram_pointer(gpr p) {
    uint32_t u = static_cast<uint32_t>(p);
    return u >= 0x80000000u && u < 0x80800000u && (u & 3) == 0;
}

// Address of the {commands, count} pair, or 0.
gpr display_list_counter(uint8_t* rdram) {
    gpr state = static_cast<int32_t>(MEM_W(0x254, static_cast<gpr>(static_cast<int32_t>(section_addresses[bootup_data_section] + draw_state_offset))));
    if (!is_rdram_pointer(state)) {
        return 0;
    }
    gpr builder = static_cast<int32_t>(MEM_W(0x158, state));
    if (!is_rdram_pointer(builder)) {
        return 0;
    }
    gpr counter = static_cast<int32_t>(MEM_W(0xC, builder));
    return is_rdram_pointer(counter) ? counter : 0;
}

gpr shadow_counter = 0;
uint32_t shadow_first_command = 0;

// The shadow's material display lists set blended render modes with no depth
// test, so the shadow showed through hills in front of the rider. Under Rider
// adds a depth test; the original modes are remembered so Original gets them back.
constexpr uint32_t set_render_mode = 0xB900031D;   // G_SETOTHERMODE_L, shift 3, 29 bits
constexpr uint32_t z_cmp = 0x10;
constexpr uint32_t z_upd = 0x20;
constexpr uint32_t zmode_mask = 0xC00;
constexpr uint32_t zmode_xlu = 0x800;
constexpr uint32_t depth_test_bits = z_cmp | zmode_xlu;
std::unordered_map<uint32_t, uint32_t> original_render_modes;   // command address -> mode
}

// Called just before the rider's shadow is set up and drawn.
extern "C" void game_shadow_draw_begin(uint8_t* rdram, recomp_context* ctx) {
    shadow_counter = display_list_counter(rdram);
    shadow_first_command = shadow_counter ? MEM_W(4, shadow_counter) : 0;
}

// Called once the shadow's commands are in the display list. The shadow is the
// rider's model flattened onto the ground, drawn through small material display
// lists of its own (one plain, one textured for the board); Under Rider switches
// their render modes to depth-tested ones (and back for Original). The material is only read when
// the frame is drawn, so changing it here takes effect this frame.
extern "C" void game_shadow_draw_end(uint8_t* rdram, recomp_context* ctx) {
    gpr counter = shadow_counter;
    shadow_counter = 0;
    if (counter == 0 || counter != display_list_counter(rdram)) {
        return;
    }
    gpr commands = static_cast<int32_t>(MEM_W(0, counter));
    uint32_t end = MEM_W(4, counter);
    if (!is_rdram_pointer(commands) || end < shadow_first_command || end - shadow_first_command > 64) {
        return;
    }
    bool depth_test = (mode() == ShadowMode::UnderRider);
    for (uint32_t i = shadow_first_command; i < end; i++) {
        gpr command = commands + i * 8;
        uint32_t op = MEM_W(0, command) >> 24;
        if (op != 0x06) {   // G_DL
            continue;
        }
        gpr sub = static_cast<int32_t>(0x80000000u | (MEM_W(4, command) & 0x00FFFFFF));
        for (uint32_t j = 0; j < 12; j++) {
            gpr sub_command = sub + j * 8;
            uint32_t w0 = MEM_W(0, sub_command);
            if ((w0 >> 24) == 0xB8 || (w0 >> 24) == 0x04) {   // G_ENDDL, or vertices (geometry, not a material)
                break;
            }
            if (w0 != set_render_mode) {
                continue;
            }
            uint32_t address = static_cast<uint32_t>(sub_command);
            uint32_t current = MEM_W(4, sub_command);
            auto it = original_render_modes.find(address);
            if (it == original_render_modes.end() || (current != it->second && current != (it->second | depth_test_bits))) {
                if ((current & (z_cmp | z_upd | zmode_mask)) != 0) {
                    break;   // already depth tested: not one of the shadow's materials
                }
                it = original_render_modes.insert_or_assign(address, current).first;
            }
            MEM_W(4, sub_command) = depth_test ? (it->second | depth_test_bits) : it->second;
            break;
        }
    }
}
