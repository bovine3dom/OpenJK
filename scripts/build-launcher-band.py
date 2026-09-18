#!/usr/bin/env python3
"""Build the original 32-by-32 horn-player sprite sheet. No reference image is read."""

from pathlib import Path
import struct

POSES = ("ready", "playing_low", "playing_high", "accent", "bob", "lowering", "bored", "blink")

PALETTE = {
    ".": (0, 0, 0, 0),
    "o": (8, 12, 25, 255),       # Outline
    "d": (18, 27, 44, 255),     # Uniform
    "m": (35, 49, 66, 255),
    "s": (56, 74, 85, 255),
    "h": (130, 126, 108, 255),  # Skin shadow
    "k": (177, 170, 141, 255),
    "c": (216, 204, 168, 255),
    "l": (243, 230, 192, 255),
    "e": (12, 16, 27, 255),     # Eyes
    "g": (77, 91, 101, 255),
    "b": (131, 98, 54, 255),    # Brass
    "t": (192, 151, 83, 255),
    "y": (233, 194, 120, 255),
}

HEAD = [
    ".....oooooo.....",
    "...oocccccloo...",
    "..okcclllllcco..",
    ".okcccllllllcco.",
    "ohkcccllllllcco.",
    "ohkccccllllcccko",
    "ohkcccccccccccko",
    "ohkkccccccccckho",
    ".okkeeegcccgeeo.",
    ".okeeeeecckeeeo.",
    "..okeekkcckeeo..",
    "...ohkkckckho...",
    "....ohkcchho....",
    ".....ohchoo.....",
    "......ooo.......",
]
BODY = [
    ".....oommmoo.....",
    "...oomsmmddmoo...",
    "..omsdoddodddmo..",
    ".omsddododdddmo..",
    ".omdddodddddddmo.",
    ".omdddmdddddddmo.",
    ".omdddmdddddddmo.",
    "..omddmddddddmo..",
    "...omdmddddddo...",
    "...omdmddddddo...",
    "....odddddddo....",
    "....odddodddo....",
    "....oddo.oddo....",
    "....oddo.oddo....",
    "...ooddo.oddmoo..",
    "...ooooo.oooooo..",
]


def sprite(pose):
    pixels = [["."] * 32 for _ in range(32)]

    def put(x, y, colour):
        if not 0 <= x < 32 or not 0 <= y < 32:
            raise ValueError("A sprite pixel is outside its tile")
        pixels[y][x] = colour

    def layer(rows, x, y):
        for j, row in enumerate(rows):
            for i, colour in enumerate(row):
                if colour != ".":
                    put(x + i, y + j, colour)

    def line(x0, y0, x1, y1, colour):
        steps = max(abs(x1 - x0), abs(y1 - y0))
        for i in range(steps + 1):
            t = i / max(steps, 1)
            put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), colour)

    sad = pose in (6, 7)
    dip = pose == 4
    layer(BODY, 6, 16)
    if sad:
        # Lower shoulders, a bowed head, and heavy eyelids.
        line(11, 17, 17, 19, "m")
        layer(HEAD, 7, 3)
        line(10, 11, 13, 11, "k")
        line(18, 11, 20, 11, "k")
        if pose == 7:
            line(10, 12, 13, 12, "h")
            line(18, 12, 20, 12, "h")
        # The horn hangs at the side instead of remaining at the mouth.
        line(20, 21, 23, 25, "o")
        line(21, 21, 24, 25, "m")
        layer(["ck", "hc"], 23, 24)
        mouth, bell = (24, 24), (26, 30)
    else:
        layer(HEAD, 8, 1 + int(dip))
        if pose in (0, 5):
            mouth, bell = (21, 19 if pose == 5 else 18), (26, 29)
        elif pose == 3:
            mouth, bell = (17, 14), (28, 22)
        else:
            mouth, bell = (17, 14 + int(dip)), (25, 26)

    # A narrow reed tube with a flared bell. All of it stays inside 32 by 32.
    x0, y0 = mouth
    x1, y1 = bell
    for offset, colour in ((-1, "o"), (0, "b"), (1, "t"), (2, "o")):
        line(x0 + offset, y0, x1 + offset, y1, colour)
    line(x0, y0, x1, y1 - 1, "y")
    layer(["obtyo", "obboo"], x1 - 1, y1)
    if not sad:
        # The near arm crosses the jacket and supports the lower keys.
        line(10, 20 + int(dip), 15, 24 + int(dip), "o")
        line(10, 19 + int(dip), 15, 23 + int(dip), "s")
        line(15, 23 + int(dip), x1 - 1, y1 - 3, "m")
        layer(["cck", "hch"], x1 - 2, y1 - 4)
        layer(["lc", "kh"], x0 + 1, y0 + 3)
        # Two fingerings, not a whole-body flash on every note.
        for offset in (5, 7):
            t = offset / max(y1 - y0, 1)
            x = round(x0 + (x1 - x0) * min(t, 1))
            put(x, min(y0 + offset, y1 - 1), "c" if pose == 2 else "b")
    else:
        # An idle hand on the hip and one loose grip on the horn.
        layer(["ck", "hh"], 12, 25)
        layer(["ck", "hh"], 24, 25)
    return pixels


def build():
    sprites = [sprite(pose) for pose in range(len(POSES))]
    width, height = 32 * len(sprites), 32
    header = b"\x00\x00\x02" + bytes(9) + struct.pack("<HHBB", width, height, 32, 0x28)
    body = bytearray()
    for y in range(height):
        for tile in sprites:
            for colour in tile[y]:
                r, g, b, a = PALETTE[colour]
                body.extend((b, g, r, a))
    return header + body


if __name__ == "__main__":
    path = Path(__file__).resolve().parent.parent / "launcher/ui/cantina-player.tga"
    path.write_bytes(build())
