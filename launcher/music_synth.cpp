#include "music_synth.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace launcher_music {
namespace {
unsigned byte(std::istream& input) {
    const auto value = input.get();
    if (value == std::char_traits<char>::eof()) throw std::runtime_error("Truncated music score.");
    return static_cast<unsigned char>(value);
}
std::uint32_t word(std::istream& input) {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) result |= byte(input) << shift;
    return result;
}
float waveform(unsigned program, float phase) {
    const unsigned table = program >= 32 && program < 40 ? 2 : (program / 8) % 4;
    const unsigned index = static_cast<unsigned>(phase) & 255;
    switch (table) {
    case 0: return index < 64 ? 1.5f : -0.5f;
    case 1: return index < 128 ? 1.f : -1.f;
    case 2: return 1.f - 4.f * std::abs(index / 256.f - 0.5f);
    default: return index < 32 ? 1.75f : -0.25f;
    }
}
} // namespace

void Synth::load(std::istream& input) {
    char magic[8];
    if (!input.read(magic, sizeof(magic)) || std::string(magic, sizeof(magic)) != "OJCHIP01")
        throw std::runtime_error("Invalid music score header.");
    const auto duration = word(input), count = word(input);
    if (!duration || duration > sample_rate * 600 || !count || count > 100000)
        throw std::runtime_error("Music score exceeds the supported limits.");
    std::vector<Note> loaded;
    std::vector<std::pair<std::uint32_t, int>> edges;
    loaded.reserve(count);
    edges.reserve(count * 2);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto start = word(input), frames = word(input);
        const auto key = byte(input), gain = byte(input), program = byte(input), channel = byte(input), pan = byte(input);
        if (!frames || start >= duration || frames > duration - start || key > 127 || program > 127 || channel > 15 ||
            (!loaded.empty() && start < loaded.back().start))
            throw std::runtime_error("Invalid music score note.");
        const float level = gain / 255.f;
        loaded.push_back({start, frames, key, program, channel, level,
            440.f * std::pow(2.f, (int(key) - 69) / 12.f) * 256.f / sample_rate,
            std::sqrt(1.f - pan / 255.f) * level, std::sqrt(pan / 255.f) * level});
        edges.emplace_back(start, 1);
        edges.emplace_back(start + frames, -1);
    }
    if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Unexpected music score data.");
    std::sort(edges.begin(), edges.end());
    int active = 0;
    for (const auto& edge : edges) {
        active += edge.second;
        if (active > int(max_voices)) throw std::runtime_error("Too many simultaneous music voices.");
    }
    notes = std::move(loaded);
    length = duration;
    reset();
}

void Synth::reset() noexcept {
    position = 0;
    next_note = voice_count = 0;
}

VisualState Synth::render(std::int16_t* output, std::size_t frames) noexcept {
    VisualState visual;
    visual.frame = length ? position % length : 0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (length && position == length) reset();
        while (next_note < notes.size() && notes[next_note].start == position) {
            voices[voice_count++] = {&notes[next_note++], 0, 1};
        }
        float left = 0, right = 0;
        for (std::size_t v = 0; v < voice_count;) {
            auto& voice = voices[v];
            const auto& note = *voice.note;
            const float age = float(voice.age);
            float envelope = std::min({1.f, age / 80.f, (note.duration - age) / 550.f});
            float sound;
            if (note.channel == 9) {
                voice.noise = voice.noise * 1664525u + 1013904223u;
                const float decay = 1.f - age / note.duration;
                sound = note.key == 35 || note.key == 36 ? std::sin(age * 6.283185307f * 70.f / sample_rate) :
                    (voice.noise & 0x80000000u ? 1.f : -1.f);
                sound *= decay * decay * decay;
            } else {
                sound = waveform(note.program, age * note.step);
                if (note.program < 16) envelope *= 0.35f + 0.65f * std::exp(-age / (sample_rate * 0.18f));
            }
            const float sample = sound * envelope;
            left += sample * note.left;
            right += sample * note.right;
            const float energy = sample * sample * note.gain * note.gain;
            visual.bands[std::min(band_count - 1, note.key / 8)] += energy;
            visual.instruments[note.channel] += energy;
            if (++voice.age == note.duration) voices[v] = voices[--voice_count];
            else ++v;
        }
        // Fixed gain and a soft limiter keep dense chords below the PCM limit.
        output[frame * 2] = static_cast<std::int16_t>(std::tanh(left * 0.07f) * 30000.f);
        output[frame * 2 + 1] = static_cast<std::int16_t>(std::tanh(right * 0.07f) * 30000.f);
        if (length) ++position;
    }
    if (frames) {
        for (float& band : visual.bands) band = std::min(1.f, std::sqrt(band / frames) * 0.7f);
        for (float& instrument : visual.instruments) instrument = std::min(1.f, std::sqrt(instrument / frames));
    }
    return visual;
}

} // namespace launcher_music
