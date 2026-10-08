#!/usr/bin/env python3
"""Generates every graphic and table of Cometa as C arrays (src/assets.c/.h).

All art is original: hand-drawn ASCII sprites plus procedural backgrounds.
Colors follow the free "Sweetie 16" palette by GrafxKid.
Run: python3 tools/make_assets.py  (writes src/assets.c, src/assets.h, build/preview_*.png)
"""
import math
import os
import random

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
BUILD = os.path.join(ROOT, "build")
os.makedirs(BUILD, exist_ok=True)


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def bgr555(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


# ---------------------------------------------------------------------------
# Palettes
# ---------------------------------------------------------------------------
# Sprite palette: index -> (char, color). Index 0 is transparent.
SWEETIE = [
    (".", "#000000"),
    ("k", "#1a1c2c"), ("p", "#5d275d"), ("r", "#b13e53"), ("o", "#ef7d57"),
    ("y", "#ffcd75"), ("l", "#a7f070"), ("g", "#38b764"), ("n", "#29366f"),
    ("b", "#3b5dc9"), ("c", "#41a6f6"), ("a", "#73eff7"), ("w", "#f4f4f4"),
    ("s", "#94b0c2"), ("m", "#566c86"), ("d", "#333c57"),
]
CH = {c: i for i, (c, _) in enumerate(SWEETIE)}
OBJ_PAL0 = [rgb(h) for _, h in SWEETIE]
# Hit flash: every color pushed 60% toward white (a full white silhouette flickers too hard).
OBJ_PAL1 = [OBJ_PAL0[0]] + [tuple(int(c + (255 - c) * 0.6) for c in col) for col in OBJ_PAL0[1:]]
# Cold variant for later stages: warm colors become blues.
swap = {"p": "n", "r": "b", "o": "c", "y": "a", "l": "a", "g": "c"}
OBJ_PAL2 = [rgb(dict(SWEETIE)[swap.get(c, c)]) if i else OBJ_PAL0[0] for i, (c, _) in enumerate(SWEETIE)]
# Toxic variant: warm colors become greens.
swap = {"p": "n", "r": "g", "o": "l", "y": "w", "b": "g", "c": "l"}
OBJ_PAL3 = [rgb(dict(SWEETIE)[swap.get(c, c)]) if i else OBJ_PAL0[0] for i, (c, _) in enumerate(SWEETIE)]

BACKDROP = rgb("#0b0d1f")
SHADOW = rgb("#1a1c2c")
HUD_BANKS = [rgb("#f4f4f4"), rgb("#ffcd75"), rgb("#73eff7"), rgb("#ef7d57")]

STAR_PAL = [BACKDROP, rgb("#29366f"), rgb("#566c86"), rgb("#94b0c2"), rgb("#f4f4f4"),
            rgb("#73eff7"), rgb("#ffcd75")] + [BACKDROP] * 9

NEB_VARIANTS = [
    ["#120f2b", "#1c1642", "#2a1d5c", "#43246e", "#5d275d", "#8a2f6e", "#b13e53", "#ef7d57", "#ffcd75"],
    ["#0b1a24", "#0f2a33", "#154045", "#1f5b55", "#257179", "#2f9476", "#38b764", "#a7f070", "#f4f4c0"],
    ["#1d0d14", "#2d1220", "#45152a", "#651a2e", "#8a2433", "#b13e3e", "#d8603f", "#ef9a57", "#ffd98a"],
]
PLANET = ["#29366f", "#3b5dc9", "#41a6f6", "#73eff7", "#257179", "#0e1430"]  # idx 10..15


def neb_palette(v):
    return [BACKDROP] + [rgb(h) for h in NEB_VARIANTS[v]] + [rgb(h) for h in PLANET]


LOGO_PAL = [BACKDROP, rgb("#1a1c2c"), rgb("#ffffff"), rgb("#ffe9a8"), rgb("#ffcd75"),
            rgb("#ef7d57"), rgb("#b13e53"), rgb("#5d275d"), rgb("#2a1d5c"), rgb("#73eff7")] + [BACKDROP] * 6


# ---------------------------------------------------------------------------
# Tile helpers
# ---------------------------------------------------------------------------
def to_tiles(img):
    """img: 2D numpy array of palette indices (h, w multiples of 8) -> list of tiles (32 bytes)."""
    h, w = img.shape
    tiles = []
    for ty in range(h // 8):
        for tx in range(w // 8):
            t = img[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            data = bytearray()
            for row in t:
                for x in range(0, 8, 2):
                    data.append((int(row[x]) & 15) | ((int(row[x + 1]) & 15) << 4))
            tiles.append(bytes(data))
    return tiles


def parse(rows):
    return np.array([[CH[c] for c in r] for r in rows], dtype=np.uint8)


def outline(img, color=CH["k"]):
    """Adds a 1px outline around opaque pixels (4-neighborhood) inside the frame."""
    out = img.copy()
    h, w = img.shape
    for y in range(h):
        for x in range(w):
            if img[y, x] == 0:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and img[ny, nx] not in (0, color):
                        out[y, x] = color
                        break
    return out


def preview(images, name, scale=6, pal=OBJ_PAL0, cols=8):
    cell = max(max(i.shape) for i in images) + 2
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell * scale, rows * cell * scale), (40, 44, 52))
    for n, im in enumerate(images):
        h, w = im.shape
        rgbimg = Image.new("RGB", (w, h))
        px = rgbimg.load()
        for y in range(h):
            for x in range(w):
                v = int(im[y, x])
                px[x, y] = pal[v] if v else (40, 44, 52)
        rgbimg = rgbimg.resize((w * scale, h * scale), Image.NEAREST)
        sheet.paste(rgbimg, ((n % cols) * cell * scale + scale, (n // cols) * cell * scale + scale))
    sheet.save(os.path.join(BUILD, name))


# ---------------------------------------------------------------------------
# Sprites (hand-drawn)
# ---------------------------------------------------------------------------
PLAYER_TOP = [
    "................................",
    ".........kkkk...................",
    ".........kwssk..................",
    "..........kssmkk................",
    "...........kdmmmkkkkk...........",
    "..........kkddmmssssskkkkk......",
    "........kkmmmmssssswwwsssskkk...",
    "......kkmmsssskkaaacbbssssssskkk",
]
FLAME_A = [
    "........",
    "........",
    "........",
    "........",
    "........",
    "..kk....",
    ".kroyk..",
    "kyywyk..",
]
FLAME_B = [
    "........",
    "........",
    "........",
    "........",
    "........",
    "........",
    "..koyk..",
    ".kwwyk..",
]


def shade_bottom(row):
    return row.replace("w", "s").replace("s", "m", 0)


def player(flame):
    rows = [list(r) for r in PLAYER_TOP]
    for y, fr in enumerate(flame):
        for x, c in enumerate(fr):
            if c != ".":
                rows[y][x] = c
    top = ["".join(r) for r in rows]
    bottom = []
    for i, r in enumerate(reversed(top)):
        if i >= 1:  # darker underside for volume
            r = r.replace("w", "s")
            if i >= 2:
                r = r.replace("s", "m")
            if i >= 4:
                r = r.replace("m", "d")
        bottom.append(r)
    return parse(top + bottom)


SHOT = parse([
    "........",
    "........",
    "...aaa..",
    ".caawwww",
    ".caawwww",
    "...aaa..",
    "........",
    "........",
])
EBULLET_A = parse([
    "........",
    "..rrrr..",
    ".roooor.",
    ".rowwor.",
    ".rowwor.",
    ".roooor.",
    "..rrrr..",
    "........",
])
EBULLET_B = parse([
    "........",
    "..pppp..",
    ".prrrrp.",
    ".rroorr.",
    ".rroorr.",
    ".prrrrp.",
    "..pppp..",
    "........",
])
DRONE_A = parse([
    "................",
    ".....kkkkkk.....",
    "...kkmssssmkk...",
    "..kmsswwwssmmk..",
    ".kmsswwssssmmdk.",
    ".kdmmssssmmmddk.",
    "kkkkkkkkkkkkkkkk",
    "kyoorrrrrrrroowk",
    "kwyoorrrrrroorrk",
    "kkkkkkkkkkkkkkkk",
    ".kddmmmmmmmmddk.",
    "..kddmmmmmmddk..",
    "...kkddddddkk...",
    ".....kkkkkk.....",
    "................",
    "................",
])
DRONE_B = parse([
    "................",
    ".....kkkkkk.....",
    "...kkmssssmkk...",
    "..kmsswwwssmmk..",
    ".kmsswwssssmmdk.",
    ".kdmmssssmmmddk.",
    "kkkkkkkkkkkkkkkk",
    "krrrroowyoorrrrk",
    "krrroowwyyoorrrk",
    "kkkkkkkkkkkkkkkk",
    ".kddmmmmmmmmddk.",
    "..kddmmmmmmddk..",
    "...kkddddddkk...",
    ".....kkkkkk.....",
    "................",
    "................",
])
DIVER_A = parse([
    "................",
    "...........kkk..",
    ".........kkpppk.",
    ".......kkpprrpk.",
    ".....kkpprrrrpk.",
    "...kkpprroorrpkk",
    "..kpprroooyyrppk",
    ".kprrooyywwyrrpk",
    ".kprrooyywwyrrpk",
    "..kpprroooyyrppk",
    "...kkpprroorrpkk",
    ".....kkpprrrrpk.",
    ".......kkpprrpk.",
    ".........kkpppk.",
    "...........kkk..",
    "................",
])
DIVER_B = parse([
    "................",
    "...........kkk..",
    ".........kkpppk.",
    ".......kkpprrpk.",
    ".....kkpprrrrpka",
    "...kkpprroorrpka",
    "..kpprroooyyrppk",
    ".kprrooyywwyrrpk",
    ".kprrooyywwyrrpk",
    "..kpprroooyyrppk",
    "...kkpprroorrpka",
    ".....kkpprrrrpka",
    ".......kkpprrpk.",
    ".........kkpppk.",
    "...........kkk..",
    "................",
])
POWER_A = parse([
    "................",
    "................",
    "...kkkkkkkkkk...",
    "..kaaaaaaaaaak..",
    ".kcaawwwwaaaack.",
    ".kccbwwbbwwcbbk.",
    ".kcbbwwbbwwbbbk.",
    ".kcbbwwwwwbbbbk.",
    ".kcbbwwbbbbbbbk.",
    ".kcbbwwbbbbbbnk.",
    "..knnnnnnnnnnk..",
    "...kkkkkkkkkk...",
    "................",
    "................",
    "................",
    "................",
])
POWER_B = parse([
    "................",
    "................",
    "...kkkkkkkkkk...",
    "..kwwwwwwwwwwk..",
    ".kaawwwwwwaaaak.",
    ".kaacwwccwwaack.",
    ".kaccwwccwwccck.",
    ".kaccwwwwwccccck",
    ".kaccwwcccccccck",
    ".kaccwwcccccccbk",
    "..kbbbbbbbbbbk..",
    "...kkkkkkkkkk...",
    "................",
    "................",
    "................",
    "................",
])
COMET = parse([
    "................",
    "................",
    "................",
    "................",
    "..........kkk...",
    ".......nbbcawk..",
    "....nnbbcaawwwk.",
    "nnbbbccaaawwwwk.",
    "...nnbbccaawwwk.",
    "......nbbcaawk..",
    "..........kkk...",
    "................",
    "................",
    "................",
    "................",
    "................",
])


# ---------------------------------------------------------------------------
# Sprites (procedural)
# ---------------------------------------------------------------------------
def heavy():
    w = h = 32
    img = np.zeros((h, w), np.uint8)
    cx, cy, rx, ry = 18, 16, 11.5, 10.5
    for y in range(h):
        for x in range(w):
            nx, ny = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            d = nx * nx + ny * ny
            if d <= 1:
                light = -0.6 * nx - 0.8 * ny + 0.3 * math.sqrt(max(0, 1 - d))
                img[y, x] = CH["s"] if light > 0.55 else CH["m"] if light > -0.05 else CH["d"]
    # armor band and rivets
    for x in range(7, 30):
        for y in (14, 15, 16, 17):
            if img[y, x]:
                img[y, x] = CH["d"]
        if x % 4 == 0 and img[15, x]:
            img[15, x] = CH["s"]
    # cannon
    for y in range(13, 19):
        for x in range(1, 9):
            img[y, x] = CH["d"] if y in (13, 18) else CH["m"]
    for y in range(14, 18):
        img[y, 1] = CH["r"]
        img[y, 2] = CH["o"]
    # red eye
    for (x, y, c) in [(17, 9, "r"), (18, 9, "r"), (16, 10, "r"), (17, 10, "o"), (18, 10, "y"), (19, 10, "r"),
                      (17, 11, "r"), (18, 11, "o"), (16, 11, "r"), (19, 11, "r")]:
        img[y, x] = CH[c]
    # engine glow at the back
    for y in range(12, 21):
        img[y, 29] = CH["o"]
        img[y, 30] = CH["y"] if 14 <= y <= 18 else CH["o"]
    # lights
    for (x, y) in [(12, 22), (16, 23), (20, 23), (24, 22)]:
        img[y, x] = CH["a"]
    return outline(img)


def boss():
    w = h = 64
    img = np.zeros((h, w), np.uint8)

    def ell(cx, cy, rx, ry, fn):
        for y in range(h):
            for x in range(w):
                nx, ny = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                d = nx * nx + ny * ny
                if d <= 1:
                    fn(x, y, nx, ny, d)

    def hull(x, y, nx, ny, d):
        light = -0.5 * nx - 0.85 * ny + 0.35 * math.sqrt(max(0, 1 - d))
        img[y, x] = CH["s"] if light > 0.6 else CH["m"] if light > 0.0 else CH["d"]

    # fins (behind the hull)
    def poly(points, c):
        from PIL import ImageDraw
        layer = Image.new("L", (w, h), 0)
        ImageDraw.Draw(layer).polygon(points, fill=1)
        m = np.array(layer, dtype=bool)
        img[m] = CH[c]

    poly([(30, 6), (54, 2), (58, 8), (46, 22), (34, 20)], "p")
    poly([(30, 57), (54, 61), (58, 55), (46, 41), (34, 43)], "p")
    for x in range(36, 56):
        y = int(6 + (x - 36) * 0.15)
        img[y, x] = CH["r"]
        img[63 - y, x] = CH["r"]
    # prongs
    poly([(6, 18), (30, 22), (30, 28), (12, 26)], "d")
    poly([(6, 45), (30, 41), (30, 35), (12, 37)], "d")
    for x in range(8, 28):
        img[int(18.5 + (x - 6) * 4 / 24), x] = CH["m"]
    # hull
    ell(38, 32, 22, 19, hull)
    # plates
    for y in range(16, 49):
        for x in (32, 44):
            if img[y, x] in (CH["s"], CH["m"], CH["d"]):
                img[y, x] = CH["k"] if img[y, x] != CH["s"] else CH["m"]
    # core
    ell(26, 32, 8.5, 8.5, lambda x, y, nx, ny, d: img.__setitem__((y, x), CH["k"]))
    ell(26, 32, 7, 7, lambda x, y, nx, ny, d: img.__setitem__(
        (y, x), CH["w"] if d < 0.12 else CH["y"] if d < 0.32 else CH["o"] if d < 0.6 else CH["r"]))
    # cannons
    for (y0, y1) in ((12, 15), (48, 51)):
        for y in range(y0, y1 + 1):
            for x in range(14, 30):
                img[y, x] = CH["d"] if y in (y0, y1) else CH["m"]
            img[y, 14] = CH["r"]
    # lights
    for (x, y) in [(40, 20), (46, 22), (50, 26), (40, 44), (46, 42), (50, 38), (54, 32)]:
        img[y, x] = CH["a"]
    # engines
    for y in range(22, 43):
        img[y, 59] = CH["o"]
        img[y, 60] = CH["y"] if 26 <= y <= 38 else CH["o"]
        if 29 <= y <= 35:
            img[y, 61] = CH["w"]
    return outline(img)


def rock(seed):
    rnd = random.Random(seed)
    w = h = 16
    img = np.zeros((h, w), np.uint8)
    bumps = [rnd.uniform(-0.9, 0.9) for _ in range(8)]
    for y in range(h):
        for x in range(w):
            dx, dy = x + 0.5 - 8, y + 0.5 - 8
            ang = math.atan2(dy, dx)
            r = 6.2 + sum(b * math.cos((i + 1) * ang + i) for i, b in enumerate(bumps[:3])) * 0.6
            dist = math.hypot(dx, dy)
            if dist <= r:
                nz = math.sqrt(max(0.0, 1 - (dist / r) ** 2))
                light = (-dx * 0.6 - dy * 0.7) / r + nz * 0.5
                img[y, x] = CH["s"] if light > 0.75 else CH["m"] if light > 0.2 else CH["d"]
    for _ in range(3):
        cx, cy = rnd.randint(5, 10), rnd.randint(5, 10)
        if img[cy, cx]:
            img[cy, cx] = CH["k"]
            if img[cy - 1, cx - 1]:
                img[cy - 1, cx - 1] = CH["d"]
    return outline(img)


def explosion(size, frames, seed):
    rnd = random.Random(seed)
    out = []
    half = size / 2
    sparks = [(rnd.uniform(0, 2 * math.pi), rnd.uniform(0.5, 1.0)) for _ in range(10)]
    noise = np.array([[rnd.random() for _ in range(size)] for _ in range(size)])
    for f in range(frames):
        t = f / (frames - 1)
        img = np.zeros((size, size), np.uint8)
        radius = half * (0.35 + 0.65 * min(1, t * 1.6))
        for y in range(size):
            for x in range(size):
                d = math.hypot(x + 0.5 - half, y + 0.5 - half) / radius
                n = noise[y, x]
                if d > 1:
                    continue
                if t < 0.2:
                    c = "w" if d < 0.5 else "y" if d < 0.8 else "o"
                elif t < 0.45:
                    c = "y" if d < 0.35 else "o" if d < 0.7 else "r"
                    if n < 0.15 and d > 0.5:
                        c = "y"
                elif t < 0.7:
                    if d < 0.45 and n < 0.6:
                        continue
                    c = "o" if d < 0.6 else "r" if d < 0.85 else "p"
                    if n < 0.25:
                        continue
                else:
                    if n < 0.55 + (t - 0.7):
                        continue
                    c = "p" if d < 0.8 else "d"
                img[y, x] = CH[c]
        for ang, sp in sparks:
            r = half * (0.3 + 0.9 * t) * sp
            x, y = int(half + math.cos(ang) * r), int(half + math.sin(ang) * r)
            if 0 <= x < size and 0 <= y < size and t < 0.85:
                img[y, x] = CH["y" if t < 0.5 else "o"]
        out.append(img)
    return out


# ---------------------------------------------------------------------------
# Font, HUD and logo (BG0)
# ---------------------------------------------------------------------------
GLYPHS = {
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "C": [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    "D": ["###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "G": [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"],
    "H": ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "I": [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "J": ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "M": ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    "N": ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "Q": [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "V": ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    "X": ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    "Y": ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    "Z": ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    ":": [".....", "..#..", "..#..", ".....", "..#..", "..#..", "....."],
    "-": [".....", ".....", ".....", ".###.", ".....", ".....", "....."],
    ".": [".....", ".....", ".....", ".....", ".....", ".##..", ".##.."],
    "!": ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
    "/": ["....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."],
    "x": [".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "....."],
    "+": [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."],
}
FONT_CHARS = " " + "".join(GLYPHS.keys())


def glyph_tile(ch):
    img = np.zeros((8, 8), np.uint8)
    if ch == " ":
        return img
    g = GLYPHS[ch]
    for y, row in enumerate(g):
        for x, c in enumerate(row):
            if c == "#":
                if img[y + 1, x + 2] == 0:
                    img[y + 1, x + 2] = 2  # shadow
                img[y, x + 1] = 1
    return img


LIFE_ICON = parse([
    "........",
    "..kk....",
    "kkmskk..",
    "kyssaakk",
    "kyssaakk",
    "kkmdkk..",
    "..kk....",
    "........",
])

BAR = {
    "left": parse(["........", "kkkkkkkk", "kwwwwwww", "kwrrrrrr", "kwrrrrrr", "kkkkkkkk", "........", "........"]),
    "full": parse(["........", "kkkkkkkk", "wwwwwwww", "oyyoyyoy", "rrorrorr", "kkkkkkkk", "........", "........"]),
    "empty": parse(["........", "kkkkkkkk", "dddddddd", "kkkkkkkk", "kkkkkkkk", "kkkkkkkk", "........", "........"]),
    "right": parse(["........", "kkkkkkk.", "wwwwwwk.", "rrrrrrk.", "rrrrrrk.", "kkkkkkk.", "........", "........"]),
}


def logo_image(text="COMETA"):
    scale = 3
    gw, gh = 5 * scale, 7 * scale
    spacing = 3
    width = len(text) * (gw + spacing) - spacing + 4
    height = gh + 6
    w8 = (width + 7) // 8 * 8
    h8 = (height + 7) // 8 * 8
    body = np.zeros((h8, w8), np.uint8)
    ox, oy = (w8 - width) // 2 + 1, 1
    for i, ch in enumerate(text):
        for y, row in enumerate(GLYPHS[ch]):
            for x, c in enumerate(row):
                if c == "#":
                    x0 = ox + i * (gw + spacing) + x * scale
                    y0 = oy + y * scale
                    body[y0:y0 + scale, x0:x0 + scale] = 1
    img = np.zeros_like(body)
    ramp = [3, 4, 4, 5, 5, 6, 6, 7]
    ys = np.where(body.any(axis=1))[0]
    top, bottom = ys.min(), ys.max()
    for y in range(h8):
        for x in range(w8):
            if body[y, x]:
                t = (y - top) / max(1, bottom - top)
                c = ramp[min(len(ramp) - 1, int(t * len(ramp)))]
                if y == top or (y > 0 and not body[y - 1, x]):
                    c = 2  # highlight on top edges
                img[y, x] = c
    # drop shadow (2px down-right) and outline
    shadow = np.zeros_like(body)
    for y in range(h8):
        for x in range(w8):
            if body[y, x]:
                for dy, dx in ((2, 1), (2, 2), (1, 2)):
                    if y + dy < h8 and x + dx < w8 and not body[y + dy, x + dx]:
                        shadow[y + dy, x + dx] = 1
    img[(shadow == 1) & (img == 0)] = 8
    out = img.copy()
    for y in range(h8):
        for x in range(w8):
            if img[y, x] == 0:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w8 and 0 <= ny < h8 and body[ny, nx]:
                        out[y, x] = 1
                        break
    return out


# ---------------------------------------------------------------------------
# Backgrounds
# ---------------------------------------------------------------------------
def star_tiles():
    rnd = random.Random(7)
    tiles = [np.zeros((8, 8), np.uint8)]
    far = []
    for _ in range(8):  # far: one dim pixel
        t = np.zeros((8, 8), np.uint8)
        t[rnd.randrange(8), rnd.randrange(8)] = rnd.choice([1, 1, 2])
        tiles.append(t)
        far.append(len(tiles) - 1)
    near = []
    for i in range(10):  # near: brighter, some crosses, some twinkle (index 5)
        t = np.zeros((8, 8), np.uint8)
        x, y = rnd.randrange(1, 7), rnd.randrange(1, 7)
        if i < 3:
            t[y, x] = 5
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                t[y + dy, x + dx] = 2
        else:
            t[y, x] = rnd.choice([3, 3, 4, 6])
        tiles.append(t)
        near.append(len(tiles) - 1)
    rnd = random.Random(11)
    far_map = [[far[rnd.randrange(len(far))] if rnd.random() < 0.33 else 0 for _ in range(32)] for _ in range(32)]
    near_map = [[near[rnd.randrange(len(near))] if rnd.random() < 0.07 else 0 for _ in range(32)] for _ in range(32)]
    return tiles, far_map, near_map


def tileable_noise(w, h, cell, rnd):
    gw, gh = w // cell, h // cell + 2
    grid = np.array([[rnd.random() for _ in range(gw)] for _ in range(gh)])
    out = np.zeros((h, w))
    for y in range(h):
        gy = y / cell
        y0 = int(gy)
        fy = gy - y0
        fy = fy * fy * (3 - 2 * fy)
        for x in range(w):
            gx = x / cell
            x0 = int(gx)
            fx = gx - x0
            fx = fx * fx * (3 - 2 * fx)
            a = grid[y0, x0 % gw]
            b = grid[y0, (x0 + 1) % gw]
            c = grid[y0 + 1, x0 % gw]
            d = grid[y0 + 1, (x0 + 1) % gw]
            out[y, x] = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
    return out


BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0


def nebula():
    w, h = 256, 160
    rnd = random.Random(3)
    n = sum(tileable_noise(w, h, c, rnd) * wt for c, wt in ((64, 0.5), (32, 0.27), (16, 0.15), (8, 0.08)))
    n = (n - n.min()) / (n.max() - n.min())
    img = np.zeros((h, w), np.uint8)
    for y in range(h):
        band = 0.55 + 0.45 * math.cos((y - 70) / 160 * math.pi * 1.6)
        for x in range(w):
            d = (n[y, x] - 0.45) * 2.6 * band
            if d <= 0:
                continue
            d = min(1.0, d)
            p = d * 8.0 + BAYER[y % 4, x % 4] - 0.5
            idx = int(max(0, min(8, round(p))))
            img[y, x] = 1 + idx if idx > 0 or d > 0.04 else 0
    # planet (bottom right, wraps horizontally)
    cx, cy, r = 196, 214, 82
    for y in range(h):
        for x in range(w):
            dx = (x - cx + 128) % 256 - 128
            dy = y - cy
            dist = math.hypot(dx, dy)
            if dist <= r:
                nx, ny = dx / r, dy / r
                nz = math.sqrt(max(0.0, 1 - nx * nx - ny * ny))
                light = -0.55 * nx - 0.65 * ny + 0.5 * nz
                band = math.sin((y + 5 * math.sin(x / 19.0)) / 4.2) + 0.4 * math.sin(y / 1.7 + x / 31.0)
                base = 11 if band > 0.45 else 14 if band > -0.2 else 10
                if light < 0.05 + BAYER[y % 4, x % 4] * 0.25:
                    base = 15
                elif light > 0.86 + BAYER[y % 4, x % 4] * 0.08:
                    base = 12
                if r - dist < 1.6 and light > -0.1:
                    base = 13
                img[y, x] = base
    # small moon (top left)
    mx, my, mr = 52, 30, 9
    for y in range(h):
        for x in range(w):
            dist = math.hypot(x - mx, y - my)
            if dist <= mr:
                dx, dy = (x - mx) / mr, (y - my) / mr
                light = -0.7 * dx - 0.6 * dy + 0.4 * math.sqrt(max(0, 1 - dx * dx - dy * dy))
                img[y, x] = 9 if light > 0.6 else 8 if light > 0.15 else 7 if light > -0.2 else 5
    return img


# ---------------------------------------------------------------------------
# Tables
# ---------------------------------------------------------------------------
def note_freq(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


# ---------------------------------------------------------------------------
# Emit C
# ---------------------------------------------------------------------------
def c_u32_array(name, tiles_bytes):
    data = b"".join(tiles_bytes)
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    lines = []
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{wd:08X}" for wd in words[i:i + 8]) + ",")
    return f"const u32 {name}[{len(words)}] = {{\n" + "\n".join(lines) + "\n};\n", len(words)


def c_u16_array(name, values, per_line=16):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(f"0x{v:04X}" for v in values[i:i + per_line]) + ",")
    return f"const u16 {name}[{len(values)}] = {{\n" + "\n".join(lines) + "\n};\n"


def main():
    c = ['// Generated by tools/make_assets.py. Do not edit.', '#include "assets.h"', ""]
    hdr = ["// Generated by tools/make_assets.py. Do not edit.", "#ifndef ASSETS_H", "#define ASSETS_H",
           '#include "gba.h"', ""]

    # --- OBJ sprites -------------------------------------------------------
    sprites = []  # (name, list of frames)
    sprites.append(("PLAYER", [player(FLAME_A), player(FLAME_B)]))
    sprites.append(("SHOT", [SHOT]))
    sprites.append(("EBULLET", [EBULLET_A, EBULLET_B]))
    sprites.append(("DRONE", [DRONE_A, DRONE_B]))
    sprites.append(("DIVER", [DIVER_A, DIVER_B]))
    sprites.append(("ROCK", [rock(1), rock(2)]))
    sprites.append(("POWER", [POWER_A, POWER_B]))
    sprites.append(("COMET", [COMET]))
    sprites.append(("HEAVY", [heavy()]))
    sprites.append(("EXPL", explosion(16, 6, 5)))
    sprites.append(("BIGEXPL", explosion(32, 6, 9)))
    sprites.append(("BOSS", [boss()]))

    obj_tiles = []
    for name, frames in sprites:
        first = len(obj_tiles)
        per = len(to_tiles(frames[0]))
        for f in frames:
            obj_tiles += to_tiles(f)
        hdr.append(f"#define SPR_{name} {first}")
        hdr.append(f"#define SPR_{name}_TILES {per}")
        hdr.append(f"#define SPR_{name}_FRAMES {len(frames)}")
    text, n = c_u32_array("obj_tiles", obj_tiles)
    c.append(text)
    hdr.append(f"extern const u32 obj_tiles[{n}];")
    all_frames = [f for _, fr in sprites for f in fr]
    preview(all_frames, "preview_sprites.png", scale=5)

    obj_pal = [bgr555(x) for x in OBJ_PAL0 + OBJ_PAL1 + OBJ_PAL2 + OBJ_PAL3]
    c.append(c_u16_array("obj_palette", obj_pal))
    hdr.append(f"extern const u16 obj_palette[{len(obj_pal)}];")

    # --- BG0: font, HUD, logo ------------------------------------------------
    bg0 = [np.zeros((8, 8), np.uint8)]
    font_index = {}
    for ch in FONT_CHARS:
        if ch == " ":
            font_index[ch] = 0
            continue
        font_index[ch] = len(bg0)
        bg0.append(glyph_tile(ch))
    hdr.append(f"#define TILE_LIFE {len(bg0)}")
    bg0.append(LIFE_ICON)
    for k in ("left", "full", "empty", "right"):
        hdr.append(f"#define TILE_BAR_{k.upper()} {len(bg0)}")
        bg0.append(BAR[k])
    logo = logo_image()
    lt = to_tiles(logo)
    logo_w, logo_h = logo.shape[1] // 8, logo.shape[0] // 8
    hdr.append(f"#define TILE_LOGO {len(bg0)}")
    hdr.append(f"#define LOGO_W {logo_w}")
    hdr.append(f"#define LOGO_H {logo_h}")
    bg0_bytes = [b for t in bg0 for b in to_tiles(t)] + lt
    text, n = c_u32_array("bg0_tiles", bg0_bytes)
    c.append(text)
    hdr.append(f"extern const u32 bg0_tiles[{n}];")
    # font lookup: ASCII 32..127 -> tile
    lookup = [font_index.get(chr(i), 0) for i in range(32, 128)]
    c.append("const u8 font_map[96] = {\n    " + ", ".join(str(v) for v in lookup) + "\n};\n")
    hdr.append("extern const u8 font_map[96];")
    preview([glyph_tile(ch) for ch in FONT_CHARS] + [LIFE_ICON] + list(BAR.values()), "preview_font.png",
            pal=[(0, 0, 0), (244, 244, 244), (26, 28, 44)] + OBJ_PAL0[3:], cols=16)
    preview([logo], "preview_logo.png", scale=4, pal=LOGO_PAL, cols=1)

    # --- BG1/BG3: stars -------------------------------------------------------
    stars, far_map, near_map = star_tiles()
    text, n = c_u32_array("star_tiles", [b for t in stars for b in to_tiles(t)])
    c.append(text)
    hdr.append(f"extern const u32 star_tiles[{n}];")
    STAR_BANK = 4
    c.append(c_u16_array("far_map", [v | (STAR_BANK << 12) if v else 0 for row in far_map for v in row]))
    c.append(c_u16_array("near_map", [v | (STAR_BANK << 12) if v else 0 for row in near_map for v in row]))
    hdr.append("extern const u16 far_map[1024];")
    hdr.append("extern const u16 near_map[1024];")

    # --- BG2: nebula ------------------------------------------------------------
    neb = nebula()
    neb_tiles = []
    uniq = {}
    neb_map = []
    NEB_BANK = 5
    for row in range(20):
        for col in range(32):
            t = neb[row * 8:row * 8 + 8, col * 8:col * 8 + 8]
            key = t.tobytes()
            if key not in uniq:
                uniq[key] = len(neb_tiles)
                neb_tiles.append(t)
            neb_map.append(uniq[key] | (NEB_BANK << 12))
    neb_map += [0] * (32 * 12)
    assert len(neb_tiles) <= 768, len(neb_tiles)
    text, n = c_u32_array("nebula_tiles", [b for t in neb_tiles for b in to_tiles(t)])
    c.append(text)
    hdr.append(f"extern const u32 nebula_tiles[{n}];")
    hdr.append(f"#define NEBULA_TILE_COUNT {len(neb_tiles)}")
    c.append(c_u16_array("nebula_map", neb_map))
    hdr.append("extern const u16 nebula_map[1024];")
    for v in range(3):
        Image.fromarray(np.array([[neb_palette(v)[i] for i in row] for row in neb], dtype=np.uint8)).resize(
            (512, 320), Image.NEAREST).save(os.path.join(BUILD, f"preview_nebula_{v}.png"))

    # --- BG palettes -------------------------------------------------------------
    bg_pal = []
    for color in HUD_BANKS:  # banks 0-3: text
        bg_pal += [BACKDROP, color, SHADOW] + OBJ_PAL0[3:]
    bg_pal += STAR_PAL  # bank 4
    bg_pal += neb_palette(0)  # bank 5
    bg_pal += LOGO_PAL  # bank 6
    bg_pal += OBJ_PAL0  # bank 7: icons and bars
    bg_pal[0] = BACKDROP
    c.append(c_u16_array("bg_palette", [bgr555(x) for x in bg_pal]))
    hdr.append(f"extern const u16 bg_palette[{len(bg_pal)}];")
    neb_pals = []
    for v in range(3):
        neb_pals += [bgr555(x) for x in neb_palette(v)]
    c.append(c_u16_array("nebula_palettes", neb_pals))
    hdr.append("extern const u16 nebula_palettes[48];")

    # --- Tables -------------------------------------------------------------------
    sin = [int(round(math.sin(2 * math.pi * i / 256) * 256)) for i in range(256)]
    c.append("const s16 sin_table[256] = {\n    " + ", ".join(str(v) for v in sin) + "\n};\n")
    hdr.append("extern const s16 sin_table[256];")
    sq = [max(0, min(2047, 2048 - round(131072 / note_freq(m)))) for m in range(128)]
    wv = [max(0, min(2047, 2048 - round(65536 / note_freq(m)))) for m in range(128)]
    c.append(c_u16_array("note_square", sq))
    c.append(c_u16_array("note_wave", wv))
    hdr.append("extern const u16 note_square[128];")
    hdr.append("extern const u16 note_wave[128];")

    hdr += ["", "#endif", ""]
    with open(os.path.join(SRC, "assets.c"), "w") as f:
        f.write("\n".join(c))
    with open(os.path.join(SRC, "assets.h"), "w") as f:
        f.write("\n".join(hdr))
    print("obj tiles:", len(obj_tiles), "bg0 tiles:", len(bg0) + len(lt), "nebula tiles:", len(neb_tiles),
          "logo:", logo_w, "x", logo_h)


if __name__ == "__main__":
    main()
