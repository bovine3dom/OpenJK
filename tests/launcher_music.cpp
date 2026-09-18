#include "music_player.h"
#include "band_animation.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace launcher_music;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void word(std::string& out, unsigned value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out += char(value >> shift);
}
std::string fixture(unsigned count = 3) {
    std::string out = "OJCHIP01";
    word(out, 4096);
    word(out, count);
    for (unsigned i = 0; i < count; ++i) {
        word(out, 128);
        word(out, 2048);
        out += char(48 + i % 32); // key
        out += char(200);        // gain
        out += char(i % 2 ? 32 : 0);
        out += char(i == 2 ? 9 : i % 16);
        out += char(i % 2 ? 255 : 0);
    }
    return out;
}
Synth load(const std::string& bytes) {
    std::istringstream input(bytes);
    Synth synth;
    synth.load(input);
    return synth;
}
void invalid(const std::string& bytes) {
    bool rejected = false;
    try { load(bytes); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "Invalid score was accepted");
}
void fixtures() {
    Synth empty;
    std::vector<std::int16_t> zero(256, 1);
    empty.render(zero.data(), 128);
    check(std::all_of(zero.begin(), zero.end(), [](auto x) { return x == 0; }), "Unloaded synth is not silent");
    invalid("");
    invalid("OJCHIP01");
    invalid(fixture().substr(0, 20));
    invalid(fixture() + "extra");
    invalid(fixture(129));
    auto bad = fixture();
    bad[16 + 11] = 16;
    invalid(bad);
    bad = fixture();
    bad[16 + 4] = bad[16 + 5] = char(255);
    invalid(bad);
    bad = fixture();
    bad[16 + 13] = 0;
    invalid(bad);

    auto synth = load(fixture());
    std::vector<std::int16_t> scratch(8192);
    check(!synth.render(scratch.data(), 128).notes[0].active, "Note telemetry started early");
    auto attack = synth.render(scratch.data(), 1);
    check(attack.frame == 129 && attack.notes[0].active && attack.notes[0].key == 48 && attack.notes[0].age == 1,
        "Note attack telemetry is incorrect");
    check(!synth.render(scratch.data(), 2047).notes[0].active, "Released note is still active");
    auto overlap = fixture(2);
    overlap[16 + 13] = char(192);
    overlap[16 + 13 + 11] = 0;
    auto layered = load(overlap);
    auto newest = layered.render(scratch.data(), 256);
    check(newest.notes[0].key == 49 && newest.notes[0].age == 64, "Telemetry did not select the newest voice");
    synth.reset();
    std::vector<std::int16_t> first(8192), second(8192), chunks(8192);
    const auto state = synth.render(first.data(), 4096);
    check(synth.render(second.data(), 4096).frame == 0, "The visual clock did not loop");
    check(first == second, "The loop does not repeat");
    synth.reset();
    for (unsigned frame = 0; frame < 4096; frame += 64) synth.render(chunks.data() + frame * 2, 64);
    check(first == chunks, "Audio depends on callback buffer size");
    check(std::all_of(first.begin(), first.begin() + 256, [](auto x) { return x == 0; }), "Note started too early");
    check(*std::max_element(first.begin(), first.end()) > 0 && *std::min_element(first.begin(), first.end()) < 0,
        "Rendered audio is silent or one-sided");
    bool stereo = false;
    for (unsigned i = 0; i < first.size(); i += 2) stereo |= first[i] != first[i + 1];
    check(stereo, "Stereo pan was lost");
    check(state.instruments[0] > 0 && state.instruments[1] > 0 && state.instruments[9] > 0,
        "Instrument meters are missing");
    check(state.bands[6] > 0 && state.bands[0] == 0, "Pitch-band meters are incorrect");
}
void animation() {
    using namespace launcher_band;
    Animation actor;
    VisualState music;
    check(actor.update(music, false, 1000) == bored, "Muted actor is not bored");
    check(actor.update(music, true, 1100) == ready, "Actor did not get ready");
    music.notes[2] = {69, 100, true};
    music.frame = sample_rate;
    check(actor.update(music, true, 1300) == accent, "Note onset did not lift the horn");
    music.notes[2].age = 2205;
    check(actor.update(music, true, 1300) == playing_low, "Low fingering is missing");
    music.notes[2].key = 74;
    check(actor.update(music, true, 1310) == playing_high, "Pitch did not change fingering");
    check(actor.update(music, true, 1320) == playing_high, "Animation advanced without the audio clock");
    music.notes[2].age = 100;
    music.frame = sample_rate + 5000;
    check(actor.update(music, true, 1320) == playing_high, "Horn accents are too frequent");
    music.notes[2].age = 2205;
    music.frame = sample_rate / 4;
    check(actor.update(music, true, 1320) == bob, "Half-time bob is missing");
    music.notes[2].active = false;
    check(actor.update(music, true, 1400) == playing_high, "Short rests pump the horn");
    check(actor.update(music, true, 1660) == lowering, "Actor did not lower after a longer rest");
    check(actor.update(music, false, 1700) == lowering, "Mute did not lower the horn");
    check(actor.update(music, false, 1900) == bored, "Mute did not settle into idle");
    check(actor.update(music, false, 5150) == blink, "Bored actor does not blink");
    check(actor.update(music, true, 5200) == ready, "Actor did not recover from mute");
    std::array<unsigned, 16> channels{};
    std::array<unsigned, member_count> slots{};
    for (unsigned member = 0; member < member_count; ++member) {
        ++slots[parts[member].slot];
        VisualState part;
        Animation player;
        player.update(part, true, 1000, member);
        for (int channel : parts[member].channels) {
            if (channel < 0) continue;
            ++channels[channel];
            part.notes[channel] = {60, 100, true};
            check(part_note(part, member).active, "Part misses a source channel");
            part.notes[channel].active = false;
        }
        check(!part_note(part, member).active, "Silent part has a note");
        part.notes[parts[member].channels[0]] = {60, 100, true};
        part.frame = sample_rate;
        check(player.update(part, true, 1300, member) != ready, "Member does not play its part");
        check(player.update(part, false, 1400, member) == lowering, "Member does not stop");
        check(player.update(part, false, 1600, member) == bored, "Member does not look bored");
    }
    for (auto count : channels) check(count == 1, "Channel assignment is not unique");
    for (auto count : slots) check(count == 1, "Stage slot is not unique");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Supply the checked-in score path");
        fixtures();
        animation();
        std::ifstream input(argv[1], std::ios::binary);
        Synth synth;
        synth.load(input);
        check(synth.note_count() == 8844, "Unexpected score note count");
        check(synth.duration() > sample_rate * 159 && synth.duration() < sample_rate * 161, "Unexpected duration");
        std::vector<std::int16_t> audio(synth.duration() * 2);
        const auto started = SDL_GetPerformanceCounter();
        synth.render(audio.data(), synth.duration());
        const double elapsed = double(SDL_GetPerformanceCounter() - started) / SDL_GetPerformanceFrequency();
        int peak = 0;
        for (auto sample : audio) peak = std::max(peak, std::abs(int(sample)));
        check(peak > 1000 && peak < 30000, "Audio is silent or clips");
        std::cout << "Rendered 160 seconds in " << elapsed << " seconds; peak " << peak << '\n';

        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        {
            Player player(argv[1]);
            check(player.toggle(), "Music did not start");
            SDL_Delay(200);
            const auto before = player.visual().frame;
            SDL_Delay(200); // No UI updates: the audio callback must continue independently.
            check(player.visual().frame > before && before > 0, "Audio clock needs UI updates");
            check(!player.toggle(), "Music did not stop");
            auto state = player.visual();
            check(state.frame == 0 && std::all_of(state.bands.begin(), state.bands.end(), [](float x) { return x == 0; }),
                "Muted meters are not clear");
            check(std::none_of(state.notes.begin(), state.notes.end(), [](const auto& note) { return note.active; }),
                "Muted note telemetry is not clear");
            check(player.toggle(), "Music did not restart");
            SDL_Delay(100);
            check(player.visual().frame > before, "Music did not resume");
        }
        SDL_Quit();
        std::cout << "PASS: score validation, synthesis, loop, stereo, meters, band animation, and independent audio callback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
