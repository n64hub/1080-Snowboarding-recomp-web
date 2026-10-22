// Field of view setting.
//
// The camera builds its projection in bootup_0634B0, passing the vertical field
// of view in degrees ($a1, as float bits) to its perspective function
// (bootup_073BA0). recomp/us.toml hooks the call and widens the angle here.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <variant>

#include "recomp.h"
#include "recompui/config.h"

namespace {
constexpr const char* option_id = "field_of_view";
constexpr double default_percent = 100.0;
constexpr float max_fov_degrees = 150.0f;

float fov_scale() {
    auto& config = recompui::config::get_graphics_config();
    if (!config.has_option(option_id)) {
        return 1.0f;
    }
    double percent = std::get<double>(config.get_option_value(option_id));
    return static_cast<float>(std::clamp(percent, 50.0, 200.0) / 100.0);
}
}

namespace game {
void add_fov_option(recomp::config::Config& graphics) {
    graphics.add_number_option(
        option_id,
        "Field of View",
        "How wide the camera sees, compared with the N64. "
        "<recomp-color primary>100%</recomp-color> matches the original. Higher values show more of the course around the rider.",
        100.0, 150.0, 5.0, 0, true, default_percent);
}
}

extern "C" void game_fov(recomp_context* ctx) {
    float scale = fov_scale();
    if (scale == 1.0f) {
        return;
    }
    uint32_t bits = static_cast<uint32_t>(ctx->r5);
    float fov;
    std::memcpy(&fov, &bits, sizeof(fov));
    // Widen the angle through its tangent so the change looks the same at any zoom.
    constexpr float deg_to_rad = 3.14159265f / 180.0f;
    float half = std::atan(std::tan(fov * 0.5f * deg_to_rad) * scale);
    fov = std::min(2.0f * half / deg_to_rad, max_fov_degrees);
    std::memcpy(&bits, &fov, sizeof(bits));
    ctx->r5 = static_cast<gpr>(static_cast<int32_t>(bits));
}
