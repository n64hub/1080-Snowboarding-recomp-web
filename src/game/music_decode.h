#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace game::music {
// Decoded audio: interleaved 16-bit stereo.
struct Pcm {
    uint32_t rate = 0;
    std::vector<int16_t> samples;
};

// Decodes a .wav, .ogg, .mp3 or .flac file. Returns false if it can't.
bool decode_file(const std::filesystem::path& path, Pcm& pcm);
}
