#pragma once

#include "music_synth.h"
#include <cmath>

namespace launcher_band {

// Columns are poses; rows are musicians in cantina-player.tga.
enum Pose { ready, playing_low, playing_high, accent, bob, lowering, bored, blink };
enum Member { lead, horn, bass, keys, percussion, member_count };

struct Part {
    std::array<int, 5> channels;
    unsigned split_key, slot;
};

// Group all sixteen source channels into five visible parts. Primary voices come first.
inline constexpr std::array<Part, member_count> parts{{
    {{2, 7, 13, -1, -1}, 72, 2}, // Clarinet and alto reeds, centre stage.
    {{1, 11, 8, 10, -1}, 67, 1}, // Supporting brass and tenor reeds.
    {{6, 3, 15, 12, 14}, 43, 0}, // Synth bass, baritone, tuba, and trombones.
    {{0, 5, -1, -1, -1}, 64, 3}, // Piano and chord synth.
    {{9, 4, -1, -1, -1}, 40, 4}, // Drum kit and pitched percussion.
}};

inline launcher_music::NoteState part_note(const launcher_music::VisualState& music, unsigned member) {
    launcher_music::NoteState result;
    for (int channel : parts[member].channels) {
        if (channel < 0) continue;
        const auto& note = music.notes[channel];
        if (note.active && (!result.active || note.age < result.age)) result = note;
    }
    return result;
}

class Animation {
public:
    Pose update(const launcher_music::VisualState& music, bool playing, std::uint32_t now, unsigned member = lead) {
        if (playing != was_playing) {
            was_playing = playing;
            changed = now;
        }
        if (!playing) {
            if (performed && now - changed < 180) return lowering;
            // Preserve the lead's approved idle timing; stagger the others' blinks.
            return (now - changed + member * 613) % 3600 >= 3400 ? blink : bored;
        }
        const auto note = part_note(music, member);
        if (!note.active) {
            // Do not pump the horn down and up in every short staccato gap.
            if (member < keys && performed) {
                if (now - last_note < 300) return fingering;
                if (now - last_note < 480) return lowering;
            }
            return ready;
        }
        performed = true;
        last_note = now;
        fingering = note.key >= parts[member].split_key ? playing_high : playing_low;
        if (now - changed < 100) return ready;
        if (member < keys && note.age < launcher_music::sample_rate * 0.06f) {
            const auto onset = music.frame - note.age;
            if (!accented || onset - accent_onset >= launcher_music::sample_rate) {
                accent_onset = onset;
                accented = true;
            }
            if (onset == accent_onset) return accent;
        }
        // The arrangement is 270 BPM. Alternate half-time nods on its quarter-note grid.
        const double beat = std::fmod(music.frame / double(launcher_music::sample_rate) * 135.0 / 60.0 +
            (member % 2) * 0.5, 1.0);
        if (member == percussion && note.age < launcher_music::sample_rate * 0.045f)
            return note.key % 2 ? playing_high : playing_low;
        if (beat >= 0.45 && beat < 0.7) return bob;
        return member == percussion ? ready : fingering;
    }

private:
    bool was_playing = false, performed = false, accented = false;
    Pose fingering = playing_low;
    std::uint32_t changed = 0, last_note = 0, accent_onset = 0;
};

} // namespace launcher_band
