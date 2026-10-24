// Custom music: replaces the game's songs with audio files from a "music" folder.
//
// The game's sound engine (the same family as Super Mario 64's) runs several
// sequence players. Player 1 plays the music; player 0 and 2 play sound
// effects and ambience. Each player starts with a flags word (bit 31 enabled,
// bit 29 muted, bit 26 recalculate volume), then its sequence number in the top
// byte of the next word. With mute behavior "soften" (the game's setting) a
// muted player plays on, scaled by its mute volume at +0x2C, so muting it with
// a scale of 0 silences the song while the sequence keeps running and the game
// stays in step with it.
//
// When a file named after the playing song's number exists (for example
// music/03.ogg), the game's song is muted that way and the file plays instead,
// looping, until the game stops or changes the song. Fades follow the game's
// own fade volume (+0x1C), measured against its value when the song started.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>

#include <SDL.h>

#include "recomp.h"
#include "recompui/config.h"
#include "util/file.h"
#include "librecomp/config.hpp"

#include "game_music.h"
#include "music_decode.h"

namespace {
constexpr const char* option_id = "custom_music";
constexpr const char* volume_option_id = "custom_music_volume";

enum class CustomMusic : uint32_t {
    Off = 0,
    On = 1,
};

constexpr uint32_t bootup_data_section = 4;
constexpr uint32_t players_offset = 0x372C0;  // bootup .data
constexpr uint32_t player_size = 0x160;
constexpr uint32_t music_player = 1;
constexpr uint32_t flag_enabled = 0x80000000u;
constexpr uint32_t flag_muted = 0x20000000u;
constexpr uint32_t flag_recalculate = 0x04000000u;
constexpr gpr fade_volume_offset = 0x1C;
constexpr gpr mute_scale_offset = 0x2C;

std::atomic<uint8_t*> rdram_base{ nullptr };

struct Track {
    int song = -1;
    game::music::Pcm pcm;
};

// Shared between the game's VI thread (which picks the song) and the audio
// thread (which mixes it).
std::mutex mutex;
std::shared_ptr<Track> playing;   // null when no file is playing
double play_position = 0.0;       // in source frames
float play_gain = 0.0f;
int wanted_song = -1;             // song whose file should be playing
std::list<std::shared_ptr<Track>> cache;  // recently decoded songs, newest first
constexpr size_t cache_limit = 3;

// VI thread state.
bool we_muted = false;
uint32_t saved_mute_scale = 0;
bool saved_muted = false;
int active_song = -1;
float start_fade = 1.0f;

CustomMusic setting() {
    auto& config = recompui::config::get_sound_config();
    if (!config.has_option(option_id)) {
        return CustomMusic::On;
    }
    return static_cast<CustomMusic>(std::get<uint32_t>(config.get_option_value(option_id)));
}

float volume_setting() {
    auto& config = recompui::config::get_sound_config();
    if (!config.has_option(volume_option_id)) {
        return 1.0f;
    }
    return static_cast<float>(std::get<double>(config.get_option_value(volume_option_id)) / 100.0);
}

std::vector<std::filesystem::path> music_folders() {
    std::vector<std::filesystem::path> folders;
    std::filesystem::path app = recompui::file::get_app_folder_path();
    if (!app.empty()) {
        folders.push_back(app / "music");
    }
    if (char* base = SDL_GetBasePath()) {
        folders.push_back(std::filesystem::u8path(base) / "music");
        SDL_free(base);
    }
    return folders;
}

// The file for a song: music/NN.ext with NN the song number (two digits or
// plain), in the app data folder or next to the program.
std::filesystem::path find_song_file(int song) {
    static const char* extensions[] = { ".ogg", ".flac", ".mp3", ".wav" };
    char two_digits[8];
    snprintf(two_digits, sizeof(two_digits), "%02d", song);
    std::string names[] = { two_digits, std::to_string(song) };
    for (const auto& folder : music_folders()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec)) {
            continue;
        }
        for (const auto& name : names) {
            for (const char* ext : extensions) {
                std::filesystem::path path = folder / (name + ext);
                if (std::filesystem::is_regular_file(path, ec)) {
                    return path;
                }
            }
        }
    }
    return {};
}

void start_loading(int song, std::filesystem::path path) {
    {
        std::lock_guard lock{ mutex };
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if ((*it)->song == song) {
                playing = *it;
                play_position = 0.0;
                cache.splice(cache.begin(), cache, it);
                return;
            }
        }
    }
    std::thread{ [song, path]() {
        auto track = std::make_shared<Track>();
        track->song = song;
        bool ok = game::music::decode_file(path, track->pcm) && !track->pcm.samples.empty() && track->pcm.rate != 0;
        if (ok) {
            fprintf(stderr, "[music] song %02d: playing %s\n", song, path.u8string().c_str());
        } else {
            fprintf(stderr, "[music] song %02d: could not read %s\n", song, path.u8string().c_str());
        }
        std::lock_guard lock{ mutex };
        if (ok) {
            cache.push_front(track);
            if (cache.size() > cache_limit) {
                cache.pop_back();
            }
            if (wanted_song == song) {
                playing = track;
                play_position = 0.0;
            }
        }
    } }.detach();
}

void stop_playing() {
    std::lock_guard lock{ mutex };
    playing.reset();
    wanted_song = -1;
}

float read_float(uint8_t* rdram, gpr address) {
    uint32_t bits = MEM_W(0, address);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
}

void game::music::add_options(recomp::config::Config& sound) {
    sound.add_enum_option(
        option_id,
        "Custom Music",
        "Plays your own audio files in place of the game's songs. Put them in a folder named "
        "<recomp-color primary>music</recomp-color> next to the program, named by song number "
        "(for example <recomp-color primary>03.ogg</recomp-color>). Ogg, FLAC, MP3 and WAV work. "
        "Songs without a file play as normal. The log lists each song number as it starts.",
        {
            { CustomMusic::Off, "Off" },
            { CustomMusic::On, "On" },
        },
        CustomMusic::On);
    sound.add_percent_number_option(
        volume_option_id,
        "Custom Music Volume",
        "Volume of your own music files compared with the game's sounds.",
        100.0);
}

void game::music::set_rdram(uint8_t* rdram) {
    rdram_base.store(rdram);
}

void game::music::frame() {
    uint8_t* rdram = rdram_base.load();
    if (rdram == nullptr || section_addresses[bootup_data_section] == 0) {
        return;
    }
    gpr player = static_cast<int32_t>(section_addresses[bootup_data_section] + players_offset + music_player * player_size);
    uint32_t flags = MEM_W(0, player);
    int song = static_cast<int>(MEM_W(4, player) >> 24);
    bool enabled = (flags & flag_enabled) != 0;

    if (enabled && song != active_song) {
        fprintf(stderr, "[music] song %02d started\n", song);
    }

    std::filesystem::path file;
    bool replace = false;
    if (enabled && setting() == CustomMusic::On) {
        if (song == active_song && we_muted) {
            replace = true;
        } else {
            file = find_song_file(song);
            replace = !file.empty();
        }
    }

    if (!replace) {
        if (we_muted) {
            uint32_t now = MEM_W(0, player);
            if (!saved_muted) {
                now &= ~flag_muted;
            }
            MEM_W(0, player) = now | flag_recalculate;
            MEM_W(mute_scale_offset, player) = saved_mute_scale;
            we_muted = false;
            stop_playing();
        }
        active_song = enabled ? song : -1;
        return;
    }

    if (!we_muted || song != active_song) {
        if (!we_muted) {
            saved_mute_scale = MEM_W(mute_scale_offset, player);
            saved_muted = (flags & flag_muted) != 0;
        }
        we_muted = true;
        start_fade = std::max(read_float(rdram, player + fade_volume_offset), 0.01f);
        {
            std::lock_guard lock{ mutex };
            playing.reset();
            wanted_song = song;
        }
        start_loading(song, file);
    }
    active_song = song;

    MEM_W(0, player) = MEM_W(0, player) | flag_muted | flag_recalculate;
    MEM_W(mute_scale_offset, player) = 0;

    float fade = std::clamp(read_float(rdram, player + fade_volume_offset) / start_fade, 0.0f, 1.0f);
    std::lock_guard lock{ mutex };
    play_gain = fade * volume_setting();
}

void game::music::mix(float* stereo, size_t frames, uint32_t rate, float scale) {
    std::lock_guard lock{ mutex };
    if (!playing || rate == 0) {
        return;
    }
    const std::vector<int16_t>& samples = playing->pcm.samples;
    const size_t source_frames = samples.size() / 2;
    if (source_frames < 2) {
        return;
    }
    const double step = static_cast<double>(playing->pcm.rate) / rate;
    const float gain = play_gain * scale / 32768.0f;
    for (size_t i = 0; i < frames; i++) {
        size_t index = static_cast<size_t>(play_position);
        float t = static_cast<float>(play_position - index);
        size_t next = (index + 1) % source_frames;
        float l = samples[index * 2] + (samples[next * 2] - samples[index * 2]) * t;
        float r = samples[index * 2 + 1] + (samples[next * 2 + 1] - samples[index * 2 + 1]) * t;
        stereo[i * 2 + 0] += l * gain;
        stereo[i * 2 + 1] += r * gain;
        play_position += step;
        if (play_position >= source_frames) {
            play_position -= source_frames;  // loop
        }
    }
}
