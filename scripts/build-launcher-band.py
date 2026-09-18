#!/usr/bin/env python3
"""Build the original 32-by-32 band sprites and props. No reference image is read."""

from pathlib import Path
import struct

POSES = ("ready", "playing_low", "playing_high", "accent", "bob", "lowering", "bored", "blink")
MEMBERS = ("lead", "horn", "bass", "keys", "percussion")

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


class Canvas:
    def __init__(self):
        self.pixels = [["."] * 32 for _ in range(32)]

    def put(self, x, y, colour):
        if not 0 <= x < 32 or not 0 <= y < 32:
            raise ValueError("A sprite pixel is outside its tile")
        self.pixels[y][x] = colour

    def layer(self, rows, x, y):
        for j, row in enumerate(rows):
            for i, colour in enumerate(row):
                if colour != ".":
                    self.put(x + i, y + j, colour)

    def line(self, x0, y0, x1, y1, colour):
        steps = max(abs(x1 - x0), abs(y1 - y0))
        for i in range(steps + 1):
            t = i / max(steps, 1)
            self.put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), colour)


def sprite(pose, member=0):
    canvas = Canvas()
    put, layer, line = canvas.put, canvas.layer, canvas.line
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
        if member < 3:
            # Keep the lead player's approved mute poses unchanged.
            line(20, 21, 23, 25, "o")
            line(21, 21, 24, 25, "m")
            layer(["ck", "hc"], 23, 24)
        mouth, bell = (24, 24), (26, 30)
    else:
        layer(HEAD, (8, 9, 7, 8, 8)[member], 1 + int(dip))
        if pose in (0, 5):
            mouth, bell = (21, 19 if pose == 5 else 18), (26, 29)
        else:
            # One pixel of lift, not a large swing across the body.
            mouth, bell = (17, 14 + int(dip)), (25, 25 if pose == 3 else 26)

    if member >= 3:
        if sad or pose == 5:
            line(9, 20, 11, 25, "m")
            line(22, 20, 23, 25, "s")
            layer(["ck", "hh"], 10, 25)
            layer(["ck", "hh"], 22, 25)
            if member == 4:
                line(10, 26, 7, 28, "t")
                line(23, 26, 26, 28, "t")
        else:
            left = int(pose in (1, 3))
            right = int(pose in (2, 3))
            line(9, 19, 11, 22, "s")
            line(22, 19, 21, 22, "m")
            layer(["cck", "hch"], 10, 22 + left)
            layer(["kcc", "hch"], 20, 22 + right)
            if member == 3:
                put(11, 24 + left, "c")
                put(22, 24 + right, "c")
            else:
                line(11, 23 + left, 7, 24 + left, "y")
                line(22, 23 + right, 26, 23 + right, "t")
        # Face the right-side players toward the centre of the band.
        return [row[::-1] for row in canvas.pixels]

    if member == 1 and not sad:
        bell = (26, bell[1])
    if member == 2:
        mouth, bell = ((24, 20), (25, 30)) if sad else ((17, mouth[1]), (20, 28 if pose == 3 else 29))

    # A narrow reed tube with a flared bell. All of it stays inside 32 by 32.
    x0, y0 = mouth
    x1, y1 = bell
    for offset, colour in ((-1, "o"), (0, "b"), (1, "t"), (2, "o")):
        line(x0 + offset, y0, x1 + offset, y1, colour)
    line(x0, y0, x1, y1 - 1, "y")
    if member == 0:
        layer(["obtyo", "obboo"], x1 - 1, y1)
    elif member == 1:
        layer([".oytyo", "obttbo", ".obbo."], x1 - 2, y1 - 1)
    else:
        for y in range(y0 + 2, y1 - 1, 2):
            x = round(x0 + (x1 - x0) * (y - y0) / (y1 - y0))
            line(x - 1, y, x + 2, y, "s")
        layer(["ottty", "obttb", ".ooo."], x1 - 1, y1 - 1)
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
    return canvas.pixels


def prop(member):
    canvas = Canvas()
    layer, line, put = canvas.layer, canvas.line, canvas.put
    if member == 3:
        # A small keyboard with a metal case and fixed status lights.
        layer(["oooooooooooooooooooooooooooo",
               "osmmmbmybmymmmmmmmmmmmmmmmmso",
               "oblclclclclclclclclclclclclbo",
               "obcclccclcclccclcclccclcclcbo",
               "obbbbbbbbbbbbbbbbbbbbbbbbbbo",
               ".oooooooooooooooooooooooooo."], 2, 24)
        line(6, 30, 6, 31, "s")
        line(25, 30, 25, 31, "m")
    elif member == 4:
        # Two drum pads and a small cymbal, separate from hands and sticks.
        line(4, 21, 4, 30, "m")
        layer([".otttyo.", "otyyytoo", ".oooooo."], 1, 19)
        layer([".otttttto.", "otccccctto", "obbbbbbbbo", ".obbbbbo.."], 4, 25)
        layer([".otttttttto.", "otccccccctto", "obbbbbbbbbbo", ".obbbbbbbo.."], 17, 24)
        for x in (7, 11, 20, 25):
            line(x, 29, x, 31, "m")
        put(5, 31, "o")
    return [row[::-1] for row in canvas.pixels] if member >= 3 else canvas.pixels


def build():
    # Rows 0-4: characters. Row 5: fixed props. Pad to a power-of-two texture.
    blank = Canvas().pixels
    rows = [[sprite(pose, member) for pose in range(len(POSES))] for member in range(len(MEMBERS))]
    rows += [[prop(member) for member in range(len(MEMBERS))] + [blank] * 3, [blank] * 8, [blank] * 8]
    header = b"\x00\x00\x02" + bytes(9) + struct.pack("<HHBB", 256, 256, 32, 0x28)
    body = bytearray()
    for row in rows:
        for y in range(32):
            for tile in row:
                for colour in tile[y]:
                    r, g, b, a = PALETTE[colour]
                    body.extend((b, g, r, a))
    return header + body


if __name__ == "__main__":
    path = Path(__file__).resolve().parent.parent / "launcher/ui/cantina-player.tga"
    path.write_bytes(build())
