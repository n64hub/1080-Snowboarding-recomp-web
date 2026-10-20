#include "game_config.h"

#include <filesystem>
#include <variant>

#include "recompinput/input_mapping.h"
#include "recompinput/input_types.h"
#include "recompui/config.h"
#include "recompui/recompui.h"
#include "util/file.h"

namespace {
void set_control_descriptions() {
    using recompinput::GameInput;
    using recompinput::set_game_input_description;

    set_game_input_description(GameInput::Y_AXIS_POS, "Lean Forward / Menu Up");
    set_game_input_description(GameInput::Y_AXIS_NEG, "Lean Back / Menu Down");
    set_game_input_description(GameInput::X_AXIS_NEG, "Steer Left / Menu Left");
    set_game_input_description(GameInput::X_AXIS_POS, "Steer Right / Menu Right");

    set_game_input_description(GameInput::A, "Jump / Confirm");
    set_game_input_description(GameInput::B, "Grab / Cancel");
    set_game_input_description(GameInput::Z, "Trick");
    set_game_input_description(GameInput::L, "L");
    set_game_input_description(GameInput::R, "Trick");
    set_game_input_description(GameInput::START, "Pause");

    set_game_input_description(GameInput::C_UP, "C Up");
    set_game_input_description(GameInput::C_DOWN, "C Down");
    set_game_input_description(GameInput::C_LEFT, "C Left");
    set_game_input_description(GameInput::C_RIGHT, "C Right");
}
}

void game::init_config() {
    std::filesystem::path app_dir = recompui::file::get_app_folder_path();
    if (!app_dir.empty()) {
        std::filesystem::create_directories(app_dir);
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = false;
    general_options.has_mouse_sensitivity = false;
    recompui::config::create_general_tab(general_options);

    recompui::config::create_graphics_tab();

    set_control_descriptions();
    recompui::config::create_controls_tab();

    recompui::config::create_sound_tab();

    recompui::config::create_mods_tab();

    recompui::config::finalize();
}

int game::get_main_volume() {
    auto& config = recompui::config::get_sound_config();
    if (!config.has_option(recompui::config::sound::options::main_volume)) {
        return 100;
    }
    return static_cast<int>(std::get<double>(config.get_option_value(recompui::config::sound::options::main_volume)));
}
