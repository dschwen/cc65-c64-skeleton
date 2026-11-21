#!/usr/bin/env python3
"""Paint the title picture and convert it to C64 hires (assets/title.hires).

The picture is the top 20 character rows of the title screen (320x160
pixels, modules/title.c). It is painted procedurally in full RGB - the
storm sky, the huge MOONFEN INN lettering, the inn with its glowing
windows and rain sheeting off the eaves, and the marsh in front, visible
only as far as the inn's light reaches - then converted under the hires
rule: every 8x8 cell may use exactly two of the 16 C64 colors. For each
cell the converter tries every color pair, scores how well ordered
dithering between the two reproduces the cell, and keeps the best.

Output format (what title.c embeds): 6400 bitmap bytes in the usual
8x8-cell order, then 800 screen bytes (high nibble = ink for set bits,
low nibble = paper).

    python3 tools/draw_title.py                       # writes assets/title.hires
    python3 tools/draw_title.py --preview out.png     # also a 3x PNG of the result
    python3 tools/draw_title.py --source src.png      # the RGB painting before conversion

Deterministic: the random streaks and reeds use a fixed seed.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

W, H = 320, 160
ROOT = Path(__file__).resolve().parents[1]
CHARGEN = Path("/usr/local/share/vice/C64/chargen-901225-01.bin")

# VICE "colodore"-like palette.
PALETTE = np.array([
    (0x00, 0x00, 0x00), (0xFF, 0xFF, 0xFF), (0x81, 0x33, 0x38), (0x75, 0xCE, 0xC8),
    (0x8E, 0x3C, 0x97), (0x56, 0xAC, 0x4D), (0x2E, 0x2C, 0x9B), (0xED, 0xF1, 0x71),
    (0x8E, 0x50, 0x29), (0x55, 0x38, 0x00), (0xC4, 0x6C, 0x71), (0x4A, 0x4A, 0x4A),
    (0x7B, 0x7B, 0x7B), (0xA9, 0xFF, 0x9F), (0x70, 0x6D, 0xEB), (0xB2, 0xB2, 0xB2),
], dtype=float)
BLACK, WHITE, RED, CYAN, PURPLE, GREEN, BLUE, YELLOW = range(8)
ORANGE, BROWN, LRED, DGREY, GREY, LGREEN, LBLUE, LGREY = range(8, 16)


def col(index: int, scale: float = 1.0) -> np.ndarray:
    return PALETTE[index] * scale


# ---------------------------------------------------------------- lettering
# 2x-scaled glyphs, 16 rows each: a chunky serif face for MOONFEN INN.
GLYPHS = {
    "M": [
        "####......####",
        ".###......###.",
        ".####....####.",
        ".##.##..##.##.",
        ".##.##..##.##.",
        ".##..####..##.",
        ".##..####..##.",
        ".##...##...##.",
        ".##...##...##.",
        ".##........##.",
        ".##........##.",
        ".##........##.",
        ".##........##.",
        ".##........##.",
        ".###......###.",
        "####......####",
    ],
    "O": [
        "....#####....",
        "..##.....##..",
        ".###.....###.",
        ".##.......##.",
        "###.......###",
        "###.......###",
        "###.......###",
        "###.......###",
        "###.......###",
        "###.......###",
        "###.......###",
        "###.......###",
        ".##.......##.",
        ".###.....###.",
        "..##.....##..",
        "....#####....",
    ],
    "N": [
        "####.....####",
        ".###......##.",
        ".####.....##.",
        ".##.##....##.",
        ".##.##....##.",
        ".##..##...##.",
        ".##..##...##.",
        ".##...##..##.",
        ".##...##..##.",
        ".##....##.##.",
        ".##....##.##.",
        ".##.....####.",
        ".##.....####.",
        ".##......###.",
        ".##......###.",
        "####......##.",
    ],
    "F": [
        "###########",
        ".###.....##",
        ".###......#",
        ".###.......",
        ".###.......",
        ".###...#...",
        ".###...#...",
        ".########..",
        ".###...#...",
        ".###...#...",
        ".###.......",
        ".###.......",
        ".###.......",
        ".###.......",
        ".###.......",
        "#####......",
    ],
    "E": [
        "###########",
        ".###.....##",
        ".###......#",
        ".###.......",
        ".###.......",
        ".###...#...",
        ".###...#...",
        ".########..",
        ".###...#...",
        ".###...#...",
        ".###.......",
        ".###.......",
        ".###......#",
        ".###.....##",
        ".###....###",
        "###########",
    ],
    "I": [
        "#####",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        ".###.",
        "#####",
    ],
}
# The O glyph is hollow on purpose; thicken its bowl to match the stems.
GLYPHS["O"] = [
    "....#####....",
    "..#########..",
    ".####...####.",
    ".###.....###.",
    "###.......###",
    "###.......###",
    "###.......###",
    "###.......###",
    "###.......###",
    "###.......###",
    "###.......###",
    "###.......###",
    ".###.....###.",
    ".####...####.",
    "..#########..",
    "....#####....",
]
TITLE_SCALE = 2
LETTER_GAP = 3   # glyph columns between letters
WORD_GAP = 9     # glyph columns between words
LETTER_TOP = 18


def title_mask() -> np.ndarray:
    text = "MOONFEN INN"
    widths = []
    for ch in text:
        widths.append(WORD_GAP if ch == " " else len(GLYPHS[ch][0]))
    total = sum(widths) + LETTER_GAP * (len(text) - 1 - 2)  # no gap around the space
    mask = np.zeros((H, W), bool)
    x = (W // TITLE_SCALE - total) // 2
    prev_space = False
    for i, ch in enumerate(text):
        if ch == " ":
            x += WORD_GAP
            prev_space = True
            continue
        if i > 0 and not prev_space:
            x += LETTER_GAP
        prev_space = False
        for gy, row in enumerate(GLYPHS[ch]):
            for gx, cell in enumerate(row):
                if cell == "#":
                    px = (x + gx) * TITLE_SCALE
                    py = LETTER_TOP + gy * TITLE_SCALE
                    mask[py:py + TITLE_SCALE, px:px + TITLE_SCALE] = True
        x += len(GLYPHS[ch][0])
    return mask


def dilate(mask: np.ndarray, radius: int) -> np.ndarray:
    out = mask.copy()
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            out |= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out


def small_text_mask(text: str, y: int) -> np.ndarray:
    """Mixed-case C64 ROM glyphs (the lower/upper set at offset $800)."""
    rom = CHARGEN.read_bytes()
    mask = np.zeros((H, W), bool)
    x0 = (W - 8 * len(text)) // 16 * 8  # centred, snapped to the cell grid
    for i, ch in enumerate(text):
        if "a" <= ch <= "z":
            code = ord(ch) - 96
        elif "A" <= ch <= "Z":
            code = ord(ch)
        else:
            code = ord(ch)
        glyph = rom[0x800 + code * 8: 0x800 + code * 8 + 8]
        for gy, bits in enumerate(glyph):
            for gx in range(8):
                if bits & (0x80 >> gx):
                    mask[y + gy, x0 + i * 8 + gx] = True
    return mask


# ---------------------------------------------------------------- painting
def smooth_noise(rng: np.random.Generator, cell: int) -> np.ndarray:
    gh, gw = H // cell + 2, W // cell + 2
    grid = rng.random((gh, gw))
    ys = np.arange(H) / cell
    xs = np.arange(W) / cell
    y0 = ys.astype(int)
    x0 = xs.astype(int)
    fy = (ys - y0)[:, None]
    fx = (xs - x0)[None, :]
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = grid[y0][:, x0]
    b = grid[y0][:, x0 + 1]
    c = grid[y0 + 1][:, x0]
    d = grid[y0 + 1][:, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def paint() -> np.ndarray:
    rng = np.random.default_rng(1913)
    img = np.zeros((H, W, 3))
    yy, xx = np.mgrid[0:H, 0:W]

    # Storm sky: dark blue cloud banks on black, a little brighter near the
    # horizon where the inn's light catches the underside of the rain.
    clouds = 0.65 * smooth_noise(rng, 32) + 0.35 * smooth_noise(rng, 12)
    clouds = np.clip((clouds - 0.58) * 2.6, 0, 1)
    img[:] = clouds[..., None] * col(DGREY, 0.45)

    # Distant reeds and willows on both sides of the inn, a black skyline.
    skyline = 112 + (smooth_noise(rng, 14)[0] * 14).astype(int)
    for x in range(W):
        img[skyline[x]:, x] = 0

    # Light sources: (x, y, strength) - the four windows, the door, the
    # dormer, the lantern. Used for the wall glow and the marsh lighting.
    windows = [(80, 88), (112, 88), (192, 88), (224, 88)]
    lights = [(x + 8, 104, 1.0) for x, _ in windows] + [(160, 118, 1.4), (160, 72, 0.5),
                                                        (146, 96, 0.6)]

    def light_at(px, py, reach):
        total = np.zeros(np.broadcast(px, py).shape)
        for lx, ly, s in lights:
            d2 = ((px - lx) ** 2 + ((py - ly) * 1.6) ** 2) / (reach * reach)
            total = total + s / (1 + d2)
        return np.clip(total / 1.25, 0, 1)

    # ---- the inn
    eave_y, ridge_y = 82, 60
    wall_l, wall_r, wall_b = 66, 254, 124
    # walls: dark plaster with black timber framing, warmed by the windows
    wall = (yy >= eave_y) & (yy < wall_b) & (xx >= wall_l) & (xx < wall_r)
    glow = light_at(xx, yy, 13.0)
    hot = np.clip((glow - 0.55) / 0.45, 0, 1)[..., None]
    plaster = (col(BROWN) * (1 - hot) + col(ORANGE) * hot) * np.clip(0.1 + glow * 1.4, 0, 1)[..., None]
    img[wall] = plaster[wall]
    beams = np.zeros((H, W), bool)
    for bx in (66, 98, 130, 176, 208, 240, 252):
        beams[eave_y:wall_b, bx:bx + 2] = True
    beams[eave_y:eave_y + 2, wall_l:wall_r] = True
    beams[106:108, wall_l:wall_r] = True
    beams[wall_b - 4:wall_b, wall_l:wall_r] = True        # stone footing
    for x0, x1 in ((98, 130), (176, 208)):                # diagonal braces
        for i in range(x1 - x0):
            y = 108 + (i * 12) // (x1 - x0)
            beams[y:y + 2, x0 + i] = True
    img[beams & wall] = col(BLACK)
    img[eave_y:eave_y + 6, wall_l:wall_r] = col(BLACK)    # shadow under the eave

    # windows: yellow panes, a hotter center, black frame and mullions
    for wx, wy in windows:
        pane = (slice(wy, wy + 16), slice(wx, wx + 16))
        img[pane] = col(YELLOW)
        img[wy + 4:wy + 12, wx + 4:wx + 12] = col(WHITE, 0.95)
        img[wy, wx:wx + 16] = img[wy + 15, wx:wx + 16] = 0
        img[wy:wy + 16, wx] = img[wy:wy + 16, wx + 15] = 0
        img[wy + 7:wy + 9, wx:wx + 16] = 0
        img[wy:wy + 16, wx + 7:wx + 9] = 0
    # door, ajar: a lit gap and the dark door leaf
    img[96:wall_b, 150:170] = 0
    img[98:wall_b, 152:162] = col(YELLOW)
    img[100:wall_b, 154:160] = col(WHITE, 0.9)
    img[98:wall_b, 162:168] = col(BROWN, 0.7)
    # lantern beside the door
    img[93:99, 144:148] = col(YELLOW)
    img[92, 144:148] = 0

    # roof: dark slate, shingle rows, a wet sheen on the eave
    roof = np.zeros((H, W), bool)
    for y in range(ridge_y, eave_y):
        t = (y - ridge_y) / (eave_y - ridge_y)
        x0 = int(110 - t * (110 - 58))
        x1 = int(210 + t * (262 - 210))
        roof[y, x0:x1] = True
    shade = 0.55 + 0.25 * ((yy - ridge_y) % 4 == 0)
    img[roof] = (col(DGREY)[None, None, :] * shade[..., None])[roof]
    img[roof & ((yy - ridge_y) % 4 == 3)] = col(BLACK)
    stagger = ((xx + ((yy - ridge_y) // 4) * 5) % 10 == 0) & roof
    img[stagger] = col(BLACK)
    img[eave_y - 1, 58:262] = col(LBLUE)
    img[ridge_y, 110:210] = col(GREY)
    # chimney
    img[48:72, 220:234] = col(DGREY, 0.8)
    img[48:50, 218:236] = col(GREY)
    for y in range(52, 72, 4):
        img[y, 220:234] = 0
    # dormer with a lit window
    for y in range(56, 66):
        half = (y - 56)
        img[y, 160 - half - 2:160 + half + 2] = col(DGREY, 0.7)
    img[66:eave_y, 148:172] = col(BROWN, 0.45)
    img[68:80, 154:166] = col(YELLOW)
    img[73, 154:166] = 0
    img[68:80, 159:161] = 0

    # rain sheeting off the eave: strings of drops falling to the ground,
    # caught in the window light as they pass
    for x in range(60, 262, 3):
        length = 1 + int(rng.integers(0, 4))
        img[eave_y:eave_y + length, x] = col(LBLUE)
        if rng.random() < 0.5:
            drop = eave_y + length + 1 + int(rng.integers(0, 2))
            if drop < eave_y + 6:
                img[drop, x] = col(LBLUE)
    for x in (58, 59, 60, 61, 259, 260, 261, 262):      # streams off the corners
        y = eave_y
        while y < wall_b + 2:
            run = 3 + int(rng.integers(0, 4))
            img[y:y + run, x] = col(LBLUE if (x % 2) else WHITE, 1.0)
            y += run + 1 + int(rng.integers(0, 2))
    splash_ys = (wall_b, wall_b + 1)
    for x in range(56, 266, 2):
        if rng.random() < 0.55:
            img[splash_ys[int(rng.integers(0, 2))], x] = col(LBLUE)

    # ---- the marsh, lit only as far as the inn's light reaches
    ground = (yy >= wall_b) | ((yy >= 118) & ((xx < wall_l) | (xx >= wall_r)) & (yy > skyline[None, :]))
    reach = light_at(xx, yy, 24.0) ** 1.5
    # The marsh itself is black; only the lit things in it show: the muddy
    # path from the door, the grass tufts, the reflections in the pools.
    grass = np.zeros((H, W, 3))
    mud = col(BROWN)[None, None, :] * np.clip(reach * 1.4, 0, 1)[..., None]
    marsh = np.where(((xx - 160) ** 2 / (10 + (yy - wall_b) * 0.7) ** 2 < 1)[..., None], mud, grass)
    img[ground] = marsh[ground]

    # pools: dark water that mirrors the windows as wavering streaks
    pools = [(104, 141, 34, 7), (216, 141, 34, 7), (48, 155, 40, 5), (272, 155, 40, 5)]
    for cx, cy, rx, ry in pools:
        pool = ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 < 1
        img[pool] = 0
        # each window mirrored as a wavering vertical streak: yellow near
        # the bank, orange further out, broken into ripples
        for lx, _, _ in lights[:4]:
            for y in range(cy - ry, min(cy + ry, H)):
                wobble = int(round(1.5 * np.sin(y * 1.3 + lx)))
                if (y + lx) % 3 == 0:
                    continue
                for x in range(lx - 5 + wobble, lx + 5 + wobble):
                    if 0 <= x < W and pool[y, x]:
                        img[y, x] = col(YELLOW if y < cy else ORANGE)
        # rain rings on the water
        for _ in range(rx // 3):
            px = cx + rng.integers(-rx + 4, rx - 4)
            py = cy + rng.integers(-ry + 2, ry - 1)
            cell = img[py - py % 8:py - py % 8 + 8, px - px % 8:px - px % 8 + 8]
            if 0 <= py < H and pool[py, px] and (cell == 0).all():
                img[py, px - 1:px + 2] = col(LBLUE)

    # reeds: lit stalks near the light, black silhouettes against the
    # bright water in the foreground corners
    for _ in range(170):
        x = int(rng.integers(0, W))
        base = int(rng.integers(wall_b + 4, H))
        height = int(rng.integers(4, 12))
        lean = rng.choice([-1, 0, 1])
        r = float(reach[base - 1, x])
        if 140 < x < 180:
            continue
        c = col(GREEN) if r > 0.18 else col(BLACK)
        for i in range(height):
            px = x + (lean * i) // 5
            if 0 <= px < W and base - i >= wall_b:
                img[base - i, px] = c
    for x0, flip in ((4, 1), (300, -1)):
        for k in range(9):
            x = x0 + flip * k * 2
            top = 128 + int(rng.integers(0, 10))
            for y in range(top, H):
                img[y, x + flip * ((y - top) // 9)] = 0

    # driving rain across everything but the lettering
    letters = title_mask()
    keep_clear = dilate(letters, 6) | dilate(small_text_mask("The Last Guest at", 4), 4) \
        | dilate(roof | wall, 2) | (yy >= wall_b)
    for _ in range(110):
        x = int(rng.integers(-20, W))
        y = int(rng.integers(0, H - 6))
        length = int(rng.integers(5, 10))
        for i in range(length):
            px, py = x + i // 3, y + i
            if 0 <= px < W and py < H and not keep_clear[py, px]:
                lit = float(light_at(np.array(px), np.array(py), 30.0))
                img[py, px] = col(LBLUE) if lit > 0.2 else col(BLUE)

    # ---- lettering: MOONFEN INN, white fading through yellow to orange
    outline = dilate(letters, 1) & ~letters
    img[dilate(letters, 2)] *= 0.25            # darken the sky right behind it
    img[outline] = 0
    ramp = {2: WHITE, 3: YELLOW, 4: YELLOW, 5: YELLOW, 6: ORANGE}
    for y in range(H):
        row = letters[y]
        if row.any():
            img[y, row] = col(ramp.get(y // 8, ORANGE))
    small = small_text_mask("The Last Guest at", 4)
    img[dilate(small, 4) & ~small] = 0
    img[small] = col(LGREY)
    return np.clip(img, 0, 255)


# ---------------------------------------------------------------- hires conversion
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 + 1 / 32


def to_hires(img: np.ndarray) -> tuple[bytes, bytes, np.ndarray]:
    allowed = (BLACK, WHITE, GREEN, BLUE, YELLOW, ORANGE, BROWN, DGREY, GREY, LBLUE, LGREY)
    pairs = [(a, b) for a in allowed for b in allowed if a <= b]
    pa = PALETTE[[a for a, _ in pairs]]
    pb = PALETTE[[b for _, b in pairs]]
    span = pb - pa
    span2 = np.maximum((span ** 2).sum(-1), 1e-6)
    bitmap = bytearray(6400)
    screen = bytearray(800)
    out = np.zeros((H, W), int)
    for cy in range(H // 8):
        for cx in range(W // 8):
            cell = img[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8].reshape(64, 3)
            d = cell[None, :, :] - pa[:, None, :]
            t = np.clip((d * span[:, None, :]).sum(-1) / span2[:, None], 0, 1)
            approx = pa[:, None, :] + t[..., None] * span[:, None, :]
            err = ((cell[None] - approx) ** 2).sum(-1)
            # Dithering two far-apart colors reads as noise: charge for it.
            err = err + 0.35 * t * (1 - t) * span2[:, None]
            best = int(err.sum(-1).argmin())
            paper, ink = pairs[best]
            tb = t[best].reshape(8, 8)
            thresh = np.tile(BAYER, (2, 2))
            bits = tb > thresh
            if paper == ink:
                bits[:] = False
            out[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8] = np.where(bits, ink, paper)
            offset = (cy * 40 + cx) * 8
            for row in range(8):
                byte = 0
                for bit in range(8):
                    if bits[row, bit]:
                        byte |= 0x80 >> bit
                bitmap[offset + row] = byte
            screen[cy * 40 + cx] = (ink << 4) | paper
    return bytes(bitmap), bytes(screen), out


def save_png(indices_or_rgb: np.ndarray, path: Path, scale: int = 3) -> None:
    from PIL import Image
    rgb = PALETTE[indices_or_rgb] if indices_or_rgb.ndim == 2 else indices_or_rgb
    image = Image.fromarray(rgb.astype(np.uint8), "RGB")
    image.resize((W * scale, H * scale), Image.NEAREST).save(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=ROOT / "assets/title.hires")
    parser.add_argument("--preview", type=Path, help="PNG of the converted hires picture")
    parser.add_argument("--source", type=Path, help="PNG of the RGB painting before conversion")
    args = parser.parse_args()
    img = paint()
    bitmap, screen, indices = to_hires(img)
    args.output.write_bytes(bitmap + screen)
    print(f"wrote {args.output} ({len(bitmap) + len(screen)} bytes)")
    if args.preview:
        save_png(indices, args.preview)
        print(f"wrote {args.preview}")
    if args.source:
        save_png(img, args.source)
        print(f"wrote {args.source}")


if __name__ == "__main__":
    main()
