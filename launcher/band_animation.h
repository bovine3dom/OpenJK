#pragma once

#include "music_synth.h"
#include <cmath>

namespace launcher_band {

// These indices match the eight 32-by-32 tiles in cantina-player.tga.
enum Pose { ready, playing_low, playing_high, accent, bob, lowering, bored, blink };

class Animation {
public:
    Pose update(const launcher_music::VisualState& music, bool playing, std::uint32_t now) {
        if (playing != was_playing) {
            was_playing = playing;
            changed = now;
        }
        if (!playing) {
            if (performed && now - changed < 180) return lowering;
            return (now - changed) % 3600 >= 3400 ? blink : bored;
        }
        // The current arrangement has a fixed 270 BPM tempo. Use a gentle half-time bob.
        // Both this phase and note age come from audio, never the UI frame rate.
        const auto& lead = music.notes[2]; // Zero-based channel 2: the clarinet melody.
        if (!lead.active) {
            if (performed && now - last_note < 140) return lowering;
            return ready;
        }
        performed = true;
        last_note = now;
        if (now - changed < 100) return ready;
        if (lead.age < launcher_music::sample_rate * 0.06f) {
            const auto onset = music.frame - lead.age;
            if (!accented || onset - accent_onset >= launcher_music::sample_rate / 2) {
                accent_onset = onset;
                accented = true;
            }
            if (onset == accent_onset) return accent;
        }
        const double beat = std::fmod(music.frame / double(launcher_music::sample_rate) * 135.0 / 60.0, 1.0);
        if (beat >= 0.45 && beat < 0.7) return bob;
        return lead.key >= 72 ? playing_high : playing_low;
    }

private:
    bool was_playing = false, performed = false, accented = false;
    std::uint32_t changed = 0, last_note = 0, accent_onset = 0;
};

} // namespace launcher_band
