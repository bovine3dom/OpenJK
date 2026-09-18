#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <vector>

namespace launcher_music {

constexpr unsigned sample_rate = 22050;
constexpr unsigned band_count = 16;

struct VisualState {
    std::uint32_t frame = 0;
    std::array<float, band_count> bands{};
    std::array<float, 16> instruments{};
};

class Synth {
public:
    // Load and validate the score before the audio callback starts.
    void load(std::istream& input);
    // Stereo signed PCM. No allocation, file access, or locks in this function.
    VisualState render(std::int16_t* output, std::size_t frames) noexcept;
    void reset() noexcept;
    std::uint32_t duration() const { return length; }
    std::size_t note_count() const { return notes.size(); }

private:
    struct Note {
        std::uint32_t start, duration;
        unsigned key, program, channel;
        float gain, step, left, right;
    };
    struct Voice {
        const Note* note = nullptr;
        std::uint32_t age = 0, noise = 1;
    };
    static constexpr unsigned max_voices = 128;
    std::vector<Note> notes;
    std::array<Voice, max_voices> voices{};
    std::uint32_t length = 0, position = 0;
    std::size_t next_note = 0, voice_count = 0;
};

} // namespace launcher_music
