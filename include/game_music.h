#pragma once

#include <cstddef>
#include <cstdint>

namespace recomp::config { class Config; }

// Custom music folder (src/game/music.cpp).
namespace game::music {
// Adds the Custom Music options to the sound tab.
void add_options(recomp::config::Config& sound);

// Gives the module RDRAM once the game has started.
void set_rdram(uint8_t* rdram);

// Runs every VI: follows the game's music player, mutes it and picks the file
// when a replacement exists.
void frame();

// Adds the playing file to interleaved stereo samples at the given rate.
void mix(float* stereo, size_t frames, uint32_t rate, float scale);
}
