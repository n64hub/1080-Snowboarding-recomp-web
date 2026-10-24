// HD Textures: the bundled texture pack and the option that turns it on.
//
// Release builds ship the pack in a "textures" folder next to the program. The
// pack itself is made from the game's own textures, so it is never part of
// this repository; tools/build-windows.sh copies it in when HD_TEXTURE_PACK is
// set. At startup the pack is copied into the mods folder (where the mod
// system finds it), and the HD Board Textures option enables or disables it. Without
// a bundled pack the option does nothing.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <variant>

#include <SDL.h>

#include "librecomp/config.hpp"
#include "librecomp/mods.hpp"
#include "recompui/config.h"
#include "util/file.h"

#include "game_config.h"

namespace {
constexpr const char* option_id = "hd_textures";
constexpr const char* mod_id = "hd_textures";
constexpr const char* pack_name = "1080-hd-textures.rtz";

enum class HdTextures : uint32_t {
    Off = 0,
    On = 1,
};

bool pack_available = false;

bool wanted() {
    auto& config = recompui::config::get_graphics_config();
    if (!config.has_option(option_id)) {
        return true;
    }
    return static_cast<HdTextures>(std::get<uint32_t>(config.get_option_value(option_id))) == HdTextures::On;
}
}

namespace game {
void add_hd_textures_option(recomp::config::Config& graphics) {
    graphics.add_enum_option(
        option_id,
        "HD Board Textures",
        "Sharper, 4x upscaled snowboard graphics with smooth edges. "
        "Everything else keeps its original look. "
        "<recomp-color primary>Off</recomp-color> shows the original N64 boards.",
        {
            { HdTextures::Off, "Off", "Off" },
            { HdTextures::On, "On", "On" },
        },
        HdTextures::On);
}

void install_hd_textures() {
    std::error_code ec;
    std::filesystem::path source = recompui::file::get_program_path() / "textures" / pack_name;
    if (!std::filesystem::is_regular_file(source, ec)) {
        // Started from another working directory: look next to the program.
        if (char* base = SDL_GetBasePath()) {
            source = std::filesystem::u8path(base) / "textures" / pack_name;
            SDL_free(base);
        }
    }
    std::filesystem::path app = recompui::file::get_app_folder_path();
    if (app.empty() || !std::filesystem::is_regular_file(source, ec)) {
        return;
    }
    std::filesystem::path mods = app / "mods";
    std::filesystem::path dest = mods / pack_name;
    std::filesystem::create_directories(mods, ec);
    // Copy when missing or when a new build ships a different pack.
    if (!std::filesystem::is_regular_file(dest, ec) ||
        std::filesystem::file_size(dest, ec) != std::filesystem::file_size(source, ec)) {
        std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            fprintf(stderr, "[hd_textures] could not install the texture pack: %s\n", ec.message().c_str());
            return;
        }
    }
    pack_available = true;
}

// Runs from the launcher and every VI: applies the option once mods have been
// scanned, and again whenever it changes.
void update_hd_textures() {
    if (!pack_available) {
        return;
    }
    static int applied = -1;
    int want = wanted() ? 1 : 0;
    if (want != applied) {
        applied = want;
        recomp::mods::enable_mod(mod_id, want == 1);
    }
}
}
