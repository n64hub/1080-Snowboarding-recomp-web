#ifndef GAME_LAUNCHER_H
#define GAME_LAUNCHER_H

#include <vector>

#include "librecomp/game.hpp"

namespace recompui { class LauncherMenu; }

// The game list from main.cpp.
extern std::vector<recomp::GameEntry> supported_games;

namespace game {
    // Replaces the frontend's plain launcher with the 1080 one (launcher.cpp).
    void register_launcher();

    // Runs every launcher frame (achievements sign-in and page refresh).
    void launcher_update(recompui::LauncherMenu* menu);
}

#endif
