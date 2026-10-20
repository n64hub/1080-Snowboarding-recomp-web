#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#include <string_view>

namespace game {
    constexpr std::u8string_view program_id = u8"1080SnowboardingRecompiled";
    constexpr std::string_view program_name = "1080 Snowboarding: Recompiled";

    // Builds the settings menu tabs and loads saved settings.
    void init_config();

    // Main volume from the sound tab, 0 to 100.
    int get_main_volume();
}

#endif
