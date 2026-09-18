#include "music_player.h"

#include <fstream>
#include <stdexcept>

namespace launcher_music {

bool Player::toggle() {
    if (device) { stop(); return false; }
    if (!synth.duration()) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open music score: " + path.u8string());
        synth.load(input);
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_AudioSpec spec{};
    spec.freq = sample_rate;
    spec.format = AUDIO_S16SYS;
    spec.channels = 2;
    spec.samples = 512;
    spec.callback = callback;
    spec.userdata = this;
    device = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (!device) throw std::runtime_error(SDL_GetError());
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void Player::stop() {
    if (device) SDL_CloseAudioDevice(device); // Wait for the callback before clearing its state.
    device = 0;
    previous = {};
    publish(previous);
}

void Player::publish(const VisualState& state) {
    ++sequence;
    frame = state.frame;
    for (unsigned i = 0; i < band_count; ++i) bands[i] = state.bands[i];
    for (unsigned i = 0; i < 16; ++i) instruments[i] = state.instruments[i];
    ++sequence;
}

void SDLCALL Player::callback(void* userdata, Uint8* buffer, int bytes) {
    auto& player = *static_cast<Player*>(userdata);
    // Publish the prior buffer, not the notes that we are about to send to the device.
    // SDL does not expose the hardware playback cursor; this is a one-buffer estimate.
    player.publish(player.previous);
    SDL_memset(buffer, 0, bytes);
    player.previous = player.synth.render(reinterpret_cast<std::int16_t*>(buffer), bytes / (2 * sizeof(std::int16_t)));
}

VisualState Player::visual() const {
    VisualState result;
    for (;;) {
        const auto before = sequence.load();
        if (before & 1) continue;
        result.frame = frame.load();
        for (unsigned i = 0; i < band_count; ++i) result.bands[i] = bands[i].load();
        for (unsigned i = 0; i < 16; ++i) result.instruments[i] = instruments[i].load();
        if (before == sequence.load()) return result;
    }
}

static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free,
    "Music visual state needs lock-free atomics.");

} // namespace launcher_music
