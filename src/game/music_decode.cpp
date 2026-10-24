// Audio file decoders for the custom music folder (music.cpp).
// Ogg Vorbis comes from stb_vorbis (in RT64's contrib folder), MP3 and FLAC
// from dr_libs (lib/dr_libs), and WAV is read here.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "../../lib/rt64/src/contrib/stb/stb_vorbis.c"
#undef L
#undef C
#undef R

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../../lib/dr_libs/dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "../../lib/dr_libs/dr_flac.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "music_decode.h"

namespace {
std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file{ path, std::ios::binary };
    if (!file) {
        return {};
    }
    return std::vector<uint8_t>{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

uint32_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

// Interleaved samples with any channel count to stereo.
void to_stereo(const int16_t* in, size_t frames, uint32_t channels, std::vector<int16_t>& out) {
    out.resize(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        int16_t l = in[i * channels];
        int16_t r = channels > 1 ? in[i * channels + 1] : l;
        out[i * 2 + 0] = l;
        out[i * 2 + 1] = r;
    }
}

bool decode_wav(const std::vector<uint8_t>& data, game::music::Pcm& pcm) {
    if (data.size() < 12 || memcmp(data.data(), "RIFF", 4) != 0 || memcmp(data.data() + 8, "WAVE", 4) != 0) {
        return false;
    }
    uint32_t format = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t* samples = nullptr;
    size_t sample_bytes = 0;
    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const uint8_t* chunk = data.data() + pos;
        size_t size = le32(chunk + 4);
        size_t available = std::min(size, data.size() - pos - 8);
        if (memcmp(chunk, "fmt ", 4) == 0 && available >= 16) {
            format = le16(chunk + 8);
            channels = le16(chunk + 10);
            rate = le32(chunk + 12);
            bits = le16(chunk + 22);
            if (format == 0xFFFE && available >= 26) {
                format = le16(chunk + 32);  // WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            samples = chunk + 8;
            sample_bytes = available;
        }
        pos += 8 + size + (size & 1);
    }
    if (samples == nullptr || channels == 0 || rate == 0) {
        return false;
    }

    size_t frames;
    std::vector<int16_t> interleaved;
    if (format == 1 && bits == 16) {
        frames = sample_bytes / (2 * channels);
        interleaved.resize(frames * channels);
        for (size_t i = 0; i < interleaved.size(); i++) {
            interleaved[i] = static_cast<int16_t>(le16(samples + i * 2));
        }
    } else if (format == 1 && bits == 24) {
        frames = sample_bytes / (3 * channels);
        interleaved.resize(frames * channels);
        for (size_t i = 0; i < interleaved.size(); i++) {
            interleaved[i] = static_cast<int16_t>(le16(samples + i * 3 + 1));
        }
    } else if (format == 1 && bits == 8) {
        frames = sample_bytes / channels;
        interleaved.resize(frames * channels);
        for (size_t i = 0; i < interleaved.size(); i++) {
            interleaved[i] = static_cast<int16_t>((int(samples[i]) - 128) << 8);
        }
    } else if (format == 3 && bits == 32) {
        frames = sample_bytes / (4 * channels);
        interleaved.resize(frames * channels);
        for (size_t i = 0; i < interleaved.size(); i++) {
            uint32_t bits32 = le32(samples + i * 4);
            float f;
            memcpy(&f, &bits32, sizeof(f));
            f = std::max(-1.0f, std::min(1.0f, f));
            interleaved[i] = static_cast<int16_t>(f * 32767.0f);
        }
    } else {
        return false;
    }
    pcm.rate = rate;
    to_stereo(interleaved.data(), frames, channels, pcm.samples);
    return true;
}
}

bool game::music::decode_file(const std::filesystem::path& path, Pcm& pcm) {
    std::vector<uint8_t> data = read_file(path);
    if (data.empty()) {
        return false;
    }
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    if (ext == ".wav") {
        return decode_wav(data, pcm);
    }
    if (ext == ".ogg") {
        int channels = 0, rate = 0;
        short* output = nullptr;
        int frames = stb_vorbis_decode_memory(data.data(), static_cast<int>(data.size()), &channels, &rate, &output);
        if (frames <= 0 || output == nullptr) {
            return false;
        }
        pcm.rate = static_cast<uint32_t>(rate);
        to_stereo(output, static_cast<size_t>(frames), static_cast<uint32_t>(channels), pcm.samples);
        free(output);
        return true;
    }
    if (ext == ".mp3") {
        drmp3_config config{};
        drmp3_uint64 frames = 0;
        drmp3_int16* output = drmp3_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &config, &frames, nullptr);
        if (output == nullptr || frames == 0) {
            return false;
        }
        pcm.rate = config.sampleRate;
        to_stereo(output, static_cast<size_t>(frames), config.channels, pcm.samples);
        drmp3_free(output, nullptr);
        return true;
    }
    if (ext == ".flac") {
        unsigned int channels = 0, rate = 0;
        drflac_uint64 frames = 0;
        drflac_int16* output = drflac_open_memory_and_read_pcm_frames_s16(data.data(), data.size(), &channels, &rate, &frames, nullptr);
        if (output == nullptr || frames == 0) {
            return false;
        }
        pcm.rate = rate;
        to_stereo(output, static_cast<size_t>(frames), channels, pcm.samples);
        drflac_free(output, nullptr);
        return true;
    }
    return false;
}
