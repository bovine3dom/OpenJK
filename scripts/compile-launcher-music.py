#!/usr/bin/env python3
"""Convert a Standard MIDI File to the launcher's compact chiptune score."""

import argparse
from collections import defaultdict
from pathlib import Path
import struct

RATE = 22050


def read_score(data):
    """Read MIDI format 0 or 1. Return notes with times in seconds."""
    if data[:4] != b"MThd" or len(data) < 14:
        raise ValueError("Missing MIDI header")
    size, format_id, tracks, division = struct.unpack_from(">IHHH", data, 4)
    if size < 6 or format_id not in (0, 1) or not division or division & 0x8000:
        raise ValueError("Use MIDI format 0 or 1 with a ticks-per-quarter-note time base")
    offset = 8 + size
    events = []
    for track in range(tracks):
        if data[offset:offset + 4] != b"MTrk":
            raise ValueError("Missing MIDI track")
        length = struct.unpack_from(">I", data, offset + 4)[0]
        stream = data[offset + 8:offset + 8 + length]
        if len(stream) != length:
            raise ValueError("Truncated MIDI track")
        offset += 8 + length
        position = tick = running = 0

        def byte():
            nonlocal position
            if position >= len(stream):
                raise ValueError("Truncated MIDI event")
            result = stream[position]
            position += 1
            return result

        def variable():
            value = 0
            for _ in range(4):
                part = byte()
                value = (value << 7) | (part & 127)
                if part < 128:
                    return value
            raise ValueError("Invalid MIDI variable-length value")

        while position < len(stream):
            tick += variable()
            status = byte()
            if status < 128:
                if not running:
                    raise ValueError("Missing MIDI running status")
                position -= 1
                status = running
            if status == 0xff:
                kind = byte()
                payload = bytes(byte() for _ in range(variable()))
                if kind == 0x51:
                    if len(payload) != 3 or int.from_bytes(payload, "big") == 0:
                        raise ValueError("Invalid MIDI tempo")
                    events.append((tick, track, status, kind, int.from_bytes(payload, "big")))
                continue
            if status in (0xf0, 0xf7):
                for _ in range(variable()):
                    byte()
                running = 0
                continue
            if not 0x80 <= status < 0xf0:
                raise ValueError("Unsupported MIDI event")
            running = status
            a = byte()
            b = 0 if status & 0xf0 in (0xc0, 0xd0) else byte()
            if a > 127 or b > 127:
                raise ValueError("Invalid MIDI data byte")
            events.append((tick, track, status, a, b))

    # Stable order preserves each track's events at the same tick.
    events.sort(key=lambda event: event[:2])
    programs = [0] * 16
    volume = [100] * 16
    expression = [127] * 16
    pan = [64] * 16
    sustain = [False] * 16
    active = defaultdict(list)
    held = [[] for _ in range(16)]
    notes = []
    tick = 0
    seconds = 0.0
    tempo = 500000

    def finish(note, end):
        if end > note[0]:
            notes.append((note[0], end, *note[1:]))

    for next_tick, _, status, a, b in events:
        seconds += (next_tick - tick) * tempo / (division * 1000000)
        tick = next_tick
        channel, kind = status & 15, status & 0xf0
        if status == 0xff:
            tempo = b
        elif kind == 0xc0:
            programs[channel] = a
        elif kind == 0xb0:
            if a == 7:
                volume[channel] = b
            elif a == 11:
                expression[channel] = b
            elif a == 10:
                pan[channel] = b
            elif a == 64:
                sustain[channel] = b >= 64
                if not sustain[channel]:
                    for note in held[channel]:
                        finish(note, seconds)
                    held[channel].clear()
        elif kind == 0x90 and b:
            active[channel, a].append((seconds, a, b * volume[channel] * expression[channel] / 127**3,
                                       programs[channel], channel, pan[channel] / 127))
        elif kind == 0x80 or (kind == 0x90 and not b):
            voices = active[channel, a]
            if voices:
                note = voices.pop(0)
                if sustain[channel] and channel != 9:
                    held[channel].append(note)
                else:
                    finish(note, seconds)
    for voices in (*active.values(), *held):
        for note in voices:
            finish(note, seconds)
    if not notes:
        raise ValueError("The MIDI file has no notes")
    return notes


def compile_score(notes):
    """Store sample times and voice controls. Do not store MIDI tracks or metadata."""
    frames = int((max(note[1] for note in notes) + 0.08) * RATE)
    if not 0 < frames <= RATE * 600 or not 0 < len(notes) <= 100000:
        raise ValueError("Music exceeds the score limits")
    result = bytearray(struct.pack("<8sII", b"OJCHIP01", frames, len(notes)))
    for start, end, key, gain, program, channel, pan in sorted(notes, key=lambda note: note[0]):
        duration = min(end - start, 0.18) if channel == 9 else end - start
        count = max(1, int((duration + 0.025) * RATE))
        result.extend(struct.pack("<IIBBBBB", int(start * RATE), count, key,
                                  round(gain * 255), program, channel, round(pan * 255)))
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("midi", type=Path)
    parser.add_argument("score", type=Path)
    args = parser.parse_args()
    args.score.parent.mkdir(parents=True, exist_ok=True)
    args.score.write_bytes(compile_score(read_score(args.midi.read_bytes())))


if __name__ == "__main__":
    main()
