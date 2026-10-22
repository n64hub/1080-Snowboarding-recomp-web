// Rider shadow setting.
//
// The game draws the rider's shadow by flattening the rider model onto the
// ground plane under them, along the direction of the course's sun
// (bootup_069370 builds the matrix). With a low sun the shadow lands far from
// the rider, and in the air it slides across the slope with the rider's height.
// "Under Rider" flattens straight onto the ground instead (along its normal), so
// the shadow stays directly beneath the rider, and fades the shadow out with height (otherwise it lands on whatever is far below, even
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
// doesn't lose the shadow in the snow it lies on, then put back. A big lift
// (2 in v0.1.19) put the shadow above the board and let it show over crests, so
// the lift is small and the shadow is also pulled toward the camera for the
// depth test (game_shadow_matrix_built), which moves nothing on screen.
constexpr float shadow_lift = 0.25f;
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
    if (mode() != ShadowMode::UnderRider) {
        return;
    }

    gpr plane = static_cast<int32_t>(MEM_W(0x74, sp));
    float nx = load_float(rdram, plane), ny = load_float(rdram, plane + 4), nz = load_float(rdram, plane + 8), d = load_float(rdram, plane + 12);
    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-6f) {
        return;
    }

    // Flatten straight onto the ground plane, along its normal. Flattening along
    // the vertical (v0.1.10 to v0.1.20) stretched the shadow into long slivers on
    // steep walls and banks. The sign follows the game's own direction: flipping
    // it would flip the sign of w in the shadow matrix and clip the shadow away.
    gpr dir = sp + 0x58;
    float dx = load_float(rdram, dir), dy = load_float(rdram, dir + 0x4), dz = load_float(rdram, dir + 0x8);
    // (Taken from the vertical part, which is steady from frame to frame, unless the plane is close to a wall.)
    float facing = (std::fabs(ny) > 0.2f * len) ? ny * dy : nx * dx + ny * dy + nz * dz;
    float sign = facing < 0.0f ? -1.0f : 1.0f;
    store_float(rdram, dir + 0x0, sign * nx / len);
    store_float(rdram, dir + 0x4, sign * ny / len);
    store_float(rdram, dir + 0x8, sign * nz / len);

    // Raise the plane toward the rider. The plane is in the rider's
    // coordinates times the course scale, like in height_above_ground.
    gpr rider = plane - rider_ground_plane;   // the caller passes rider + 0x9D8
    float k = -world_scale.load(std::memory_order_relaxed);
    if (k == 0.0f) {
        return;
    }
    float px = load_float(rdram, rider + rider_pos), py = load_float(rdram, rider + rider_pos + 4), pz = load_float(rdram, rider + rider_pos + 8);
    float side = k * (nx * px + ny * py + nz * pz) + d;
    lifted_plane = plane;
    lifted_plane_d = MEM_W(12, plane);
    store_float(rdram, plane + 12, d - std::copysign(shadow_lift * len, side));
}

namespace {
// The game's global draw state (bootup .data + 0x3C168).
constexpr uint32_t draw_state_offset = 0x3C168;
constexpr uint32_t bootup_data_section = 4;
constexpr gpr draw_state_view_projection = 0x60;   // 4x4 floats, world to clip space

gpr draw_state() {
    return static_cast<int32_t>(section_addresses[bootup_data_section] + draw_state_offset);
}

// The shadow is drawn this fraction of the way toward the camera. Moving
// geometry along the lines of sight leaves it where it is on screen but nearer
// in the depth buffer, so bumps in the snow it lies on don't cut into it while
// hills in front of it still hide it. Too much and it covers the board.
constexpr float shadow_depth_pull = 0.01f;

// Position of the camera in world space: the point the view-projection matrix
// (row vectors) sends to x = y = w = 0. False if it can't be found.
bool camera_position(uint8_t* rdram, float eye[3]) {
    gpr vp = draw_state() + draw_state_view_projection;
    float m[4][4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            m[r][c] = load_float(rdram, vp + (r * 4 + c) * 4);
        }
    }
    // Solve e * A = b, where A has columns 0, 1 and 3 of the top 3x3 rows and b is minus row 3.
    const int cols[3] = { 0, 1, 3 };
    double a[3][3], b[3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            a[i][j] = m[i][cols[j]];
        }
        b[i] = -m[3][cols[i]];
    }
    // e_i: Cramer's rule on the transposed system sum_i e_i a[i][j] = b[j].
    auto det3 = [](double t[3][3]) {
        return t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) + t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0]);
    };
    double at[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            at[i][j] = a[j][i];
        }
    }
    double d = det3(at);
    if (!std::isfinite(d) || std::fabs(d) < 1e-20) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        double t[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                t[i][j] = (j == k) ? b[i] : at[i][j];
            }
        }
        eye[k] = static_cast<float>(det3(t) / d);
        if (!std::isfinite(eye[k])) {
            return false;
        }
    }
    return true;
}
}

// Called right after the shadow matrix is built ($sp + 0x1C points at it, 4x4 floats
// for row vectors, flattening world positions onto the ground plane).
extern "C" void game_shadow_matrix_built(uint8_t* rdram, recomp_context* ctx) {
    restore_lifted_plane(rdram);
    if (mode() != ShadowMode::UnderRider) {
        return;
    }
    float eye[3];
    if (!camera_position(rdram, eye)) {
        return;
    }
    // Follow the flattening with a scale toward the camera: p -> eye + s * (p - eye).
    // For homogeneous rows (xyz, w) that is (s * xyz + (1 - s) * w * eye, w).
    gpr matrix = static_cast<int32_t>(MEM_W(0x1C, ctx->r29));
    float s = 1.0f - shadow_depth_pull;
    for (int r = 0; r < 4; r++) {
        gpr row = matrix + r * 16;
        float w = load_float(rdram, row + 12);
        for (int c = 0; c < 3; c++) {
            store_float(rdram, row + c * 4, s * load_float(rdram, row + c * 4) + (1.0f - s) * w * eye[c]);
        }
    }
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
// The draw state's +0x254 -> +0x158 is the current display list builder, whose
// +0xC points at {commands, count}.

bool is_rdram_pointer(gpr p) {
    uint32_t u = static_cast<uint32_t>(p);
    return u >= 0x80000000u && u < 0x80800000u && (u & 3) == 0;
}

// Address of the {commands, count} pair, or 0.
gpr display_list_counter(uint8_t* rdram) {
    gpr state = static_cast<int32_t>(MEM_W(0x254, draw_state()));
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

// Called with a render mode the game just looked up in $v0 (when a course loads).
// Besides the shadow, the game draws a small picture of the board on the snow
// under each rider, with a blended mode that has no depth test (0x41C8), so it
// showed in front of hills the rider had gone behind. Under Rider gives it a
// depth test. (v0.1.17 did this believing it was the shadow; it was removed in
// v0.1.18.) The lookup happens when a course loads, so a change of setting
// applies to this from the next course.
extern "C" void game_board_mark_render_mode(uint8_t* rdram, recomp_context* ctx) {
    constexpr uint32_t board_mark_mode = 0x41C8;
    uint32_t v = static_cast<uint32_t>(ctx->r2);
    if ((v & 0xFFFF) != board_mark_mode || mode() != ShadowMode::UnderRider) {
        return;
    }
    // A decal: it lies on the snow, so it is drawn where it meets the snow in
    // front of it and hidden where something nearer covers it.
    constexpr uint32_t zmode_decal = 0xC00;
    v = (v & ~zmode_mask) | z_cmp | zmode_decal;
    ctx->r2 = static_cast<int32_t>(v);
}
