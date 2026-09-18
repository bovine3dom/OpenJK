#pragma once

#include "music_synth.h"
#include <SDL.h>
#include <atomic>
#include <filesystem>
#include <utility>

namespace launcher_music {

class Player {
public:
    explicit Player(std::filesystem::path path) : path(std::move(path)) {}
    ~Player() { stop(); }
    bool toggle();
    void stop();
    VisualState visual() const;

private:
    void publish(const VisualState& state);
    static void SDLCALL callback(void* userdata, Uint8* buffer, int bytes);
    std::filesystem::path path;
    SDL_AudioDeviceID device = 0;
    Synth synth;
    VisualState previous;
    std::atomic<unsigned> sequence{0}, frame{0};
    std::array<std::atomic<float>, band_count> bands{};
    std::array<std::atomic<float>, 16> instruments{};
    std::array<std::atomic<unsigned>, 16> note_keys{}, note_ages{};
};

} // namespace launcher_music
