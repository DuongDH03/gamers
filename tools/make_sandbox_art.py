"""Generates the sandbox game's pixel art as PNG files.

    python tools/make_sandbox_art.py            # writes games/sandbox/scenes/art/*.png

Uses only the Python standard library (zlib + struct write the PNGs), so the
art is reproducible from this file: change a colour here, run it again.
Every image is deterministic (fixed random seeds).

Layout conventions the engine expects (docs/scene-format.md):
  - tile textures: 16x16 frames side by side; frame 0 = top face,
    frame 1 = side face, optional frame 2 = side face under another block;
  - sprite sheets: frames side by side, transparent background, the
    character's feet on the bottom row.
"""

import os
import random
import struct
import sys
import zlib

TRANSPARENT = (0, 0, 0, 0)
OUTLINE = (34, 28, 40, 255)


def rgb(r, g, b, a=255):
    return (r, g, b, a)


def shade(color, factor):
    r, g, b, a = color
    clamp = lambda v: max(0, min(255, int(round(v * factor))))
    return (clamp(r), clamp(g), clamp(b), a)


class Canvas:
    def __init__(self, width, height, fill=TRANSPARENT):
        self.width = width
        self.height = height
        self.pixels = [[fill for _ in range(width)] for _ in range(height)]

    def set(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y][x] = color

    def get(self, x, y):
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.pixels[y][x]
        return TRANSPARENT

    def rect(self, x0, y0, x1, y1, color):
        """Fills the inclusive rectangle [x0, x1] x [y0, y1]."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, color)

    def outline(self, x0, x1, color=OUTLINE):
        """Adds a 1-pixel outline around opaque pixels, within columns [x0, x1]."""
        edges = []
        for y in range(self.height):
            for x in range(x0, x1 + 1):
                if self.get(x, y)[3] != 0:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx = x + dx
                    if x0 <= nx <= x1 and self.get(nx, y + dy)[3] != 0 and self.get(nx, y + dy) != color:
                        edges.append((x, y))
                        break
        for x, y in edges:
            self.set(x, y, color)

    def save(self, path):
        raw = bytearray()
        for row in self.pixels:
            raw.append(0)  # filter type 0 (none) for every row
            for pixel in row:
                raw.extend(pixel)

        def chunk(kind, data):
            body = kind + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)  # 8-bit RGBA
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        png += chunk(b"IEND", b"")
        with open(path, "wb") as f:
            f.write(png)


def speckle(canvas, x0, y0, w, h, base, rng, amount=0.35, spread=0.12):
    """Fills a block with `base` and random lighter/darker specks."""
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            factor = 1.0
            if rng.random() < amount:
                factor = 1.0 + rng.uniform(-spread, spread)
            canvas.set(x, y, shade(base, factor))


# ---------------------------------------------------------------- tiles

GRASS = rgb(92, 160, 72)
DIRT = rgb(140, 100, 64)


def grass_tile():
    rng = random.Random(1)
    c = Canvas(48, 16)
    # Frame 0: grass seen from above, with short blades.
    speckle(c, 0, 0, 16, 16, GRASS, rng, 0.45, 0.15)
    for _ in range(14):
        x, y = rng.randrange(16), rng.randrange(1, 16)
        c.set(x, y, shade(GRASS, 1.25))
        c.set(x, y - 1, shade(GRASS, 1.12))
    # Frame 1: side; dirt with a ragged grass edge on top.
    speckle(c, 16, 0, 16, 16, DIRT, rng, 0.4, 0.15)
    for x in range(16):
        depth = 3 + rng.randrange(3)
        for y in range(depth):
            c.set(16 + x, y, shade(GRASS, 1.0 - 0.06 * y))
    for _ in range(5):  # a few pebbles
        x, y = rng.randrange(1, 15), rng.randrange(7, 15)
        c.set(16 + x, y, rgb(170, 160, 150))
    # Frame 2: side under another block: dirt only.
    speckle(c, 32, 0, 16, 16, DIRT, rng, 0.4, 0.15)
    for _ in range(6):
        x, y = rng.randrange(1, 15), rng.randrange(1, 15)
        c.set(32 + x, y, rgb(170, 160, 150))
    return c


def brick_rows(c, x0, base, mortar, rng):
    """Bricks 8x4 with offset rows."""
    for y in range(16):
        for x in range(16):
            row = y // 4
            offset = 4 if row % 2 else 0
            if y % 4 == 3 or (x + offset) % 8 == 7:
                c.set(x0 + x, y, mortar)
            else:
                c.set(x0 + x, y, shade(base, 1.0 + rng.uniform(-0.08, 0.08)))


def wall_tile():
    rng = random.Random(2)
    c = Canvas(32, 16)
    base, mortar = rgb(128, 118, 138), rgb(88, 80, 96)
    # Frame 0: the top of a wall: big flat cap stones.
    for y in range(16):
        for x in range(16):
            edge = x % 8 == 7 or y % 8 == 7
            c.set(x, y, mortar if edge else shade(base, 1.12 + rng.uniform(-0.05, 0.05)))
    # Frame 1: bricks.
    brick_rows(c, 16, base, mortar, rng)
    return c


def stone_tile():
    rng = random.Random(3)
    c = Canvas(32, 16)
    base = rgb(168, 168, 176)
    # Frame 0: a smooth slab with a few cracks.
    speckle(c, 0, 0, 16, 16, base, rng, 0.3, 0.06)
    for x, y in ((3, 4), (4, 5), (5, 5), (10, 10), (11, 11), (11, 12)):
        c.set(x, y, shade(base, 0.7))
    c.rect(0, 0, 15, 0, shade(base, 1.15))
    c.rect(0, 15, 15, 15, shade(base, 0.8))
    # Frame 1: stacked blocks.
    brick_rows(c, 16, base, shade(base, 0.7), rng)
    return c


def leaves_tile():
    rng = random.Random(4)
    c = Canvas(32, 16)
    base = rgb(52, 120, 64)
    for frame in range(2):
        speckle(c, frame * 16, 0, 16, 16, base, rng, 0.6, 0.25)
        for _ in range(10):  # clumps of light leaves
            x, y = rng.randrange(16), rng.randrange(16)
            c.set(frame * 16 + x, y, shade(base, 1.35))
            c.set(frame * 16 + (x + 1) % 16, y, shade(base, 1.2))
    return c


def water_tile():
    rng = random.Random(5)
    c = Canvas(16, 16)
    base = rgb(64, 124, 204)
    speckle(c, 0, 0, 16, 16, base, rng, 0.3, 0.06)
    for y in (3, 8, 13):  # wave crests
        x0 = rng.randrange(8)
        for x in range(x0, x0 + 5):
            c.set(x % 16, y, shade(base, 1.35))
            c.set((x + 1) % 16, y - 1, shade(base, 1.2))
    return c


def hill_tile():
    rng = random.Random(6)
    c = Canvas(16, 16)
    speckle(c, 0, 0, 16, 16, rgb(84, 140, 84), rng, 0.25, 0.08)
    return c


def cloud_tile():
    c = Canvas(16, 16)
    base = rgb(236, 242, 250)
    for y in range(16):
        for x in range(16):
            c.set(x, y, shade(base, 1.0 if y < 12 else 0.92))
    return c


# ---------------------------------------------------------------- sprites

SKIN = rgb(240, 196, 156)
HAIR = rgb(92, 56, 36)
SHIRT = rgb(214, 72, 60)
PANTS = rgb(60, 84, 160)
BOOTS = rgb(70, 46, 34)


def hero_sheet():
    """4-frame walk cycle, 16x24 per frame, feet on the bottom row."""
    c = Canvas(64, 24)
    # Leg offsets (left leg, right leg) per frame, in pixels forward/back.
    strides = [(0, 0), (-1, 1), (0, 0), (1, -1)]
    arm_swing = [0, 1, 0, -1]
    for frame in range(4):
        ox = frame * 16
        bob = 1 if frame in (1, 3) else 0  # the body dips at full stride
        # Head.
        c.rect(ox + 5, 2 + bob, ox + 10, 3 + bob, HAIR)
        c.rect(ox + 5, 4 + bob, ox + 10, 8 + bob, SKIN)
        c.set(ox + 5, 4 + bob, HAIR)
        c.set(ox + 10, 4 + bob, HAIR)
        c.set(ox + 7, 6 + bob, OUTLINE)  # eyes
        c.set(ox + 9, 6 + bob, OUTLINE)
        # Body.
        c.rect(ox + 4, 9 + bob, ox + 11, 15, SHIRT)
        c.rect(ox + 4, 9 + bob, ox + 11, 9 + bob, shade(SHIRT, 1.15))
        c.rect(ox + 4, 15, ox + 11, 15, rgb(110, 70, 40))  # belt
        # Arms swing opposite to the legs.
        swing = arm_swing[frame]
        c.rect(ox + 3, 10 + bob + swing, ox + 3, 13 + bob + swing, shade(SHIRT, 0.85))
        c.set(ox + 3, 14 + bob + swing, SKIN)
        c.rect(ox + 12, 10 + bob - swing, ox + 12, 13 + bob - swing, shade(SHIRT, 0.85))
        c.set(ox + 12, 14 + bob - swing, SKIN)
        # Legs and boots.
        left, right = strides[frame]
        c.rect(ox + 5 + left, 16, ox + 7 + left, 20, PANTS)
        c.rect(ox + 8 + right, 16, ox + 10 + right, 20, shade(PANTS, 0.85))
        c.rect(ox + 5 + left, 21, ox + 7 + left, 23, BOOTS)
        c.rect(ox + 8 + right, 21, ox + 10 + right, 23, BOOTS)
        c.outline(ox, ox + 15)
    return c


def coin_sheet():
    """4-frame spin, 12x12 per frame."""
    c = Canvas(48, 12)
    gold, rim, shine = rgb(250, 206, 60), rgb(196, 140, 28), rgb(255, 246, 180)
    half_widths = [4.5, 3.0, 1.0, 3.0]
    for frame, half_width in enumerate(half_widths):
        ox = frame * 12
        for y in range(1, 11):
            for x in range(12):
                dx = (x + 0.5 - 6.0) / half_width
                dy = (y + 0.5 - 6.0) / 4.8
                d = dx * dx + dy * dy
                if d <= 1.0:
                    color = rim if d > 0.6 else gold
                    if dx < -0.2 and dy < -0.2 and d < 0.5:
                        color = shine
                    c.set(ox + x, y, color)
        c.outline(ox, ox + 11)
    return c


def critter_sheet():
    """2-frame bounce of a little slime, 12x12 per frame."""
    c = Canvas(24, 12)
    body = rgb(172, 120, 224)
    shapes = [(4.5, 4.5, 6.5), (5.5, 3.5, 7.5)]  # (half width, half height, centre y)
    for frame, (hw, hh, cy) in enumerate(shapes):
        ox = frame * 12
        for y in range(12):
            for x in range(12):
                dx = (x + 0.5 - 6.0) / hw
                dy = (y + 0.5 - cy) / hh
                if dx * dx + dy * dy <= 1.0 and y <= 10:
                    c.set(ox + x, y, shade(body, 1.2 if dy < -0.4 else 1.0))
        eye_y = int(cy)
        c.set(ox + 4, eye_y, rgb(255, 255, 255))
        c.set(ox + 7, eye_y, rgb(255, 255, 255))
        c.rect(ox + 1, 11, ox + 10, 11, TRANSPARENT)
        c.outline(ox, ox + 11)
    return c


ART = {
    "grass.png": grass_tile,
    "wall.png": wall_tile,
    "stone.png": stone_tile,
    "leaves.png": leaves_tile,
    "water.png": water_tile,
    "hill.png": hill_tile,
    "cloud.png": cloud_tile,
    "hero.png": hero_sheet,
    "coin.png": coin_sheet,
    "critter.png": critter_sheet,
}


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "games", "sandbox", "scenes", "art")
    os.makedirs(out, exist_ok=True)
    for name, make in ART.items():
        canvas = make()
        canvas.save(os.path.join(out, name))
        print(f"{name:12s} {canvas.width}x{canvas.height}")


if __name__ == "__main__":
    main()
