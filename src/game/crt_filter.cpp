// CRT Filter setting.
//
// Draws the game as if on a CRT TV. The effect lives in RT64's final screen
// pass (VideoInterfacePS.hlsl, from patches/runtime/rt64-camera-cuts.patch);
// this file adds the option and passes the choice to RT64 every frame.

#include <cstdint>
#include <variant>

#include "game_config.h"
#include "recompui/config.h"

extern "C" void rt64_set_crt_filter(uint32_t mode);

namespace {
enum class CrtFilter : uint32_t {
    Off = 0,
    Scanlines = 1,
    Crt = 2,
};

constexpr const char* option_id = "crt_filter";
}

namespace game {
void add_crt_filter_option(recomp::config::Config& graphics) {
    graphics.add_enum_option(
        option_id,
        "CRT Filter",
        "Makes the picture look like it is on an old TV. "
        "<recomp-color primary>Off</recomp-color> (the default) shows the plain picture. "
        "<recomp-color primary>Scanlines</recomp-color> adds the dark lines between the TV's 240 picture lines. "
        "<recomp-color primary>CRT</recomp-color> also curves the screen, adds the TV's fine red, green and blue stripes and darkens the corners. "
        "It looks best at 1080p or higher.",
        {
            { CrtFilter::Off, "Off", "Off" },
            { CrtFilter::Scanlines, "Scanlines", "Scanlines" },
            { CrtFilter::Crt, "CRT", "CRT" },
        },
        CrtFilter::Off);
}

void update_crt_filter() {
    auto& config = recompui::config::get_graphics_config();
    uint32_t mode = 0;
    if (config.has_option(option_id)) {
        mode = std::get<uint32_t>(config.get_option_value(option_id));
    }
    rt64_set_crt_filter(mode);
}
}
