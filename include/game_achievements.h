#pragma once

#include <cstdint>
#include <string>
#include <vector>

// RetroAchievements support, built on the official rcheevos library.
//
// The player signs in with their RetroAchievements account; only the session
// token the server returns is stored, never the password. Achievement
// definitions are downloaded from RetroAchievements at runtime and never ship
// with the game. Until RetroAchievements approves this program, unlocks are
// kept on this PC only: rcheevos runs in spectator mode, so nothing is
// submitted to the site.
namespace game::achievements {
    enum class SignInState {
        SignedOut,
        SigningIn,
        SignedIn,
    };

    struct Entry {
        uint32_t id = 0;
        std::string title;
        std::string description;
        uint32_t points = 0;
        bool unlocked = false;          // on the site already, or earned here
        bool unlocked_here = false;     // earned in this program (kept locally)
    };

    struct Snapshot {
        uint32_t version = 0;           // changes whenever anything below changes
        SignInState state = SignInState::SignedOut;
        std::string username;
        std::string message;            // last error or status line, may be empty
        std::string game_title;         // empty until the game's set is loaded
        bool game_has_set = false;
        std::vector<Entry> entries;
    };

    // Creates the client and signs in with a saved token, if there is one.
    // Call once after the config path is registered.
    void init();

    // Called each launcher frame (UI thread): loads the game's set once the
    // player is signed in and a ROM is available.
    void launcher_tick();

    // Called once per VI (60 Hz) while the game runs.
    void frame();

    // Called once when RDRAM exists (from the boot hook).
    void set_rdram(uint8_t* rdram);

    void sign_in(const std::string& username, const std::string& password);
    void sign_out();

    Snapshot snapshot();

    // Creates the unlock pop-up. Call on the UI thread once the UI exists.
    void init_toasts();
}
