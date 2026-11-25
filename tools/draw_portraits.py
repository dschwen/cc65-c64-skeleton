#!/usr/bin/env python3
"""Draw the Mara (03), Pell Thimble (04) and open-visor Caldus (05)
portraits.

Portraits are 48x42 one-bit pictures stored as four 24x21 hires sprites
(top-left, top-right, bottom-left, bottom-right; 63 bytes plus one pad
byte each, 256 bytes in all) - see assets/portraits/ and
platform_portrait_show(). These two follow the style of Orra (01) and
Vey (02): one-pixel outlines, checker hatching for hair and cloth, open
faces, all inside the shared notched frame.

    python3 tools/draw_portraits.py [--preview out.png]
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path

W, H = 48, 42
ROOT = Path(__file__).resolve().parent.parent


def blank() -> list[list[int]]:
    return [[0] * W for _ in range(H)]


def frame(g):
    """The notched frame every portrait shares."""
    for x in range(1, W - 1):
        g[1][x] = 1
        g[H - 2][x] = 1
    for y in range(1, H - 1):
        g[y][1] = 1
        g[y][W - 2] = 1
    for i in range(3):
        # Corner diagonals, as in portraits 01 and 02.
        for x, y in ((2 + i, 4 - i), (W - 3 - i, 4 - i),
                     (2 + i, H - 5 + i), (W - 3 - i, H - 5 + i)):
            g[y][x] = 1


def inside(x, y):
    return 2 <= x <= W - 3 and 2 <= y <= H - 3


def plot(g, x, y, v=1):
    if inside(x, y):
        g[y][x] = v


def line(g, x0, y0, x1, y1, v=1):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        plot(g, x0, y0, v)
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def poly(g, pts, v=1):
    for (a, b), (c, d) in zip(pts, pts[1:]):
        line(g, a, b, c, d, v)


def ellipse_mask(cx, cy, rx, ry):
    return {(x, y) for y in range(H) for x in range(W)
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0}


def outline(g, mask, v=1):
    for x, y in mask:
        if any((x + dx, y + dy) not in mask
               for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            plot(g, x, y, v)


def fill(g, mask, pattern):
    for x, y in mask:
        plot(g, x, y, pattern(x, y))


def clear(g, mask):
    fill(g, mask, lambda x, y: 0)


def checker(x, y):
    return (x + y) & 1


def sparse(x, y):
    return 1 if (x % 4 == 0 and y % 4 == 0) or (x % 4 == 2 and y % 4 == 2) else 0


def poly_mask(pts):
    """Even-odd fill of a closed polygon, sampled at pixel centres."""
    out = set()
    for y in range(H):
        cy = y + 0.5
        xs = []
        for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
            if (y0 <= cy < y1) or (y1 <= cy < y0):
                xs.append(x0 + (cy - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        for a, b in zip(xs[::2], xs[1::2]):
            for x in range(math.ceil(a - 0.5), math.floor(b - 0.5) + 1):
                out.add((x, y))
    return out


def eye(g, cx, cy, look=0):
    """An almond eye with a pupil, as in Orra's portrait."""
    poly(g, [(cx - 3, cy), (cx - 2, cy - 1), (cx + 2, cy - 1), (cx + 3, cy)])
    poly(g, [(cx - 3, cy), (cx - 2, cy + 1), (cx + 2, cy + 1), (cx + 3, cy)])
    plot(g, cx + look, cy)
    plot(g, cx + look + 1, cy)


def lidded_eye(g, cx, cy):
    """A calm, half-lidded eye: a heavy upper lid, the pupil beneath it,
    and an open lower edge."""
    line(g, cx - 2, cy - 1, cx + 2, cy - 1)
    plot(g, cx - 3, cy)
    plot(g, cx + 3, cy)
    plot(g, cx, cy)
    plot(g, cx + 1, cy)
    line(g, cx - 1, cy + 1, cx + 2, cy + 1, 0)


def open_eye(g, cx, cy):
    """Orra's eye: a small ring around a two-pixel pupil."""
    line(g, cx - 1, cy - 1, cx + 1, cy - 1)
    line(g, cx - 1, cy + 1, cx + 1, cy + 1)
    plot(g, cx - 2, cy)
    plot(g, cx + 2, cy)
    plot(g, cx, cy)


def mara():
    """Mara: the inn's true keeper, seen in the repaired moon mirror. A
    pale oval face under a crescent moon, long straight hair falling to
    the frame, a high-collared gown, and the mirror's glow stippled
    behind her."""
    g = blank()
    fill(g, ellipse_mask(23.5, 20, 20, 19), sparse)

    # Hair: a tall mass of fine vertical strands down past the shoulders.
    hair = poly_mask([(23.5, 3), (31, 4.5), (35, 9), (36.5, 20), (38, 32),
                      (41, 45), (6, 45), (9, 32), (10.5, 20), (12, 9),
                      (16, 4.5)])
    # Long strands: every other column, broken where light catches them.
    fill(g, hair, lambda x, y: 1 if x % 2 == 1 and (y + 3 * x) % 9 else 0)
    outline(g, hair)

    face_c = (23.5, 18)
    # The hair's inner edge is the face's outline, as in Orra's portrait.
    face = ellipse_mask(*face_c, 8.5, 11)
    clear(g, face)
    outline(g, face)

    # A small crescent moon on the brow.
    for x, y in ((24, 8), (25, 8), (23, 9), (23, 10), (24, 11), (25, 11)):
        plot(g, x, y)

    # Fine level brows, calm eyes, a long straight nose, a small mouth.
    line(g, 19, 13, 21, 13)
    line(g, 26, 13, 28, 13)
    open_eye(g, 20, 16)
    open_eye(g, 27, 16)
    line(g, 23, 17, 23, 21)
    plot(g, 22, 22)
    plot(g, 24, 22)
    poly(g, [(21, 25), (22, 25), (23, 26), (24, 26), (25, 25), (26, 25)])

    # Neck, then a gown with a high collar and a moon clasp.
    clear(g, poly_mask([(20, 28), (27, 28), (28, 32), (19, 32)]))
    line(g, 20, 28, 20, 31)
    line(g, 27, 28, 27, 31)
    gown = poly_mask([(17, 32), (30, 32), (37, 35), (40, 45), (7, 45),
                      (10, 35)])
    fill(g, gown, lambda x, y: 1 if (x + y) % 3 == 0 else 0)
    outline(g, gown)
    poly(g, [(17, 32), (23, 35), (24, 35), (30, 32)])
    clasp = ellipse_mask(23.5, 36.5, 1.7, 1.7)
    clear(g, clasp)
    outline(g, clasp)
    frame(g)
    return g


def pell():
    """Pell Thimble: a small, anxious courier. A peaked courier's cap with
    a badge, round spectacles, worried brows, freckles, a wound scarf, and
    his mailbag strap across his chest."""
    g = blank()
    cx = 23.5

    # Coat first, so the scarf and head sit on top of it.
    coat = poly_mask([(15, 33), (32, 33), (40, 36), (45, 45), (2, 45),
                      (7, 36)])
    outline(g, coat)
    line(g, 19, 33, 23, 40)          # lapels
    line(g, 28, 33, 24, 40)
    for y in (37, 39):
        plot(g, 23, y)
        plot(g, 24, y)
    for dx in range(3):              # strap over the left shoulder
        line(g, 12 + dx, 35, 18 + dx, 40)

    # Ears behind the face.
    for ex in (cx - 9, cx + 9):
        ear = ellipse_mask(ex, 21, 2, 3)
        clear(g, ear)
        outline(g, ear)
    face = ellipse_mask(cx, 21, 8.5, 9.5)
    clear(g, face)
    outline(g, face)

    # Scarf at the throat with a hanging end.
    scarf = poly_mask([(16, 29.5), (31, 29.5), (32, 33), (15, 33)])
    clear(g, scarf)
    fill(g, scarf, checker)
    outline(g, scarf)
    tail = poly_mask([(26, 33), (30, 33), (31, 38.5), (27, 38.5)])
    clear(g, tail)
    fill(g, tail, checker)
    outline(g, tail)

    # Cap: a hatched crown, a solid band with a badge, and a curved peak.
    crown = poly_mask([(13.5, 12), (14.5, 7), (19, 3.5), (28, 3.5),
                       (32.5, 7), (33.5, 12)])
    clear(g, crown)
    fill(g, crown, checker)
    outline(g, crown)
    clear(g, {(x, y) for x in range(14, 34) for y in (10, 11)})
    line(g, 14, 10, 33, 10)
    line(g, 14, 12, 33, 12)
    plot(g, 14, 11)
    plot(g, 33, 11)
    badge = ellipse_mask(cx, 9, 2, 2)
    clear(g, badge)
    outline(g, badge)
    peak = poly_mask([(12, 12.5), (35, 12.5), (31, 15.5), (16, 15.5)])
    clear(g, peak)
    outline(g, peak)

    # Worried brows, tilted up in the middle.
    line(g, 17, 17, 20, 16)
    line(g, 27, 16, 30, 17)
    # Round spectacles with pupils glancing aside.
    for ex in (20, 27):
        outline(g, ellipse_mask(ex + 0.5, 20.5, 2.6, 2.4))
        plot(g, ex - 1, 20)
        plot(g, ex - 1, 21)
    line(g, 23, 20, 24, 20)
    # Button nose, freckles, a small uncertain mouth.
    outline(g, ellipse_mask(23.5, 24, 1.6, 1.2))
    for x, y in ((17, 24), (18, 25), (29, 24), (30, 25)):
        plot(g, x, y)
    poly(g, [(20, 28), (22, 27), (25, 27), (27, 28)])
    frame(g)
    return g


def decode(data: bytes):
    g = blank()
    for y in range(H):
        for x in range(W):
            sprite = (1 if x >= 24 else 0) + (2 if y >= 21 else 0)
            byte = data[sprite * 64 + (y % 21) * 3 + (x % 24) // 8]
            g[y][x] = (byte >> (7 - x % 8)) & 1
    return g


def caldus_open():
    """Sir Caldus with his visor freed: portrait 00's helmet and shoulders
    unchanged, the visor raised into a riveted band across the brow, and
    a weathered face with a drooping moustache in the opening."""
    g = decode((ROOT / "assets/portraits/00").read_bytes())

    # Clear the closed visor (eye slit and breathing grille) inside the
    # helmet's cheek guards.
    for y in range(14, 27):
        for x in range(13, 33):
            g[y][x] = 0

    # The raised visor: a solid band with rivets, its lower lip below.
    line(g, 12, 14, 34, 14)
    line(g, 12, 15, 34, 15)
    for x in (15, 20, 26, 31):
        plot(g, x, 14, 0)
    line(g, 13, 16, 33, 16)
    for x in range(14, 33, 2):
        plot(g, x, 16, 0)

    # Close the helmet's right edge where the old visor met it.
    line(g, 33, 18, 35, 18)

    # Weary brows sloping down at the outer ends; small deep-set eyes.
    line(g, 16, 19, 17, 18)
    line(g, 18, 18, 20, 18)
    line(g, 25, 18, 27, 18)
    line(g, 28, 18, 29, 19)
    for cx in (18, 27):
        plot(g, cx, 20)
        plot(g, cx + 1, 20)
        line(g, cx - 1, 21, cx + 2, 21)
    # A long, slightly crooked nose.
    line(g, 23, 19, 23, 21)
    plot(g, 24, 22)
    plot(g, 22, 23)
    plot(g, 25, 23)
    # Lines of age beside the nose.
    plot(g, 20, 23)
    plot(g, 19, 24)
    plot(g, 27, 23)
    plot(g, 28, 24)
    # A drooping moustache over a short hatched beard.
    poly(g, [(18, 26), (20, 25), (22, 25), (23, 24), (24, 25),
             (26, 25), (28, 26)])
    for x in range(20, 27, 2):
        plot(g, x, 26)
    return g


def encode(g) -> bytes:
    out = bytearray()
    for ox, oy in ((0, 0), (24, 0), (0, 21), (24, 21)):
        for r in range(21):
            for b in range(3):
                v = 0
                for k in range(8):
                    v = (v << 1) | g[oy + r][ox + b * 8 + k]
                out.append(v)
        out.append(0)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", type=Path)
    args = ap.parse_args()
    portraits = {"03": mara(), "04": pell(), "05": caldus_open()}
    for pid, g in portraits.items():
        path = ROOT / "assets/portraits" / pid
        path.write_bytes(encode(g))
        print(f"wrote {path}")
    if args.preview:
        from PIL import Image
        img = Image.new("L", (len(portraits) * 58 - 10, H), 60)
        for i, g in enumerate(portraits.values()):
            tile = Image.new("L", (W, H))
            tile.putdata([255 if v else 0 for row in g for v in row])
            img.paste(tile, (i * 58, 0))
        img.resize((img.width * 6, img.height * 6), Image.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
