#!/usr/bin/env python3
"""Generate PocketBook launcher icons for ReadingStats.

Creates 8-bit grayscale BMP icons sized 106x128:
- ReadingStats.bmp      normal state
- ReadingStats_f.bmp    focused/selected state
- ReadingStats.app.bmp  compatibility copy

Uses only the Python standard library so it can run in GitHub Actions without
Pillow/ImageMagick.
"""

from __future__ import annotations

import math
import os
import struct
import sys
from typing import List, Sequence, Tuple

W, H = 106, 128
SCALE = 4

Point = Tuple[float, float]


def inside_round_rect(x: float, y: float, rx: float, ry: float, rw: float, rh: float, r: float) -> bool:
    if x < rx or y < ry or x >= rx + rw or y >= ry + rh:
        return False
    cx = rx + r if x < rx + r else rx + rw - r if x >= rx + rw - r else x
    cy = ry + r if y < ry + r else ry + rh - r if y >= ry + rh - r else y
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r


def dist_to_segment(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
    vx = bx - ax
    vy = by - ay
    wx = px - ax
    wy = py - ay
    c1 = vx * wx + vy * wy
    if c1 <= 0:
        return math.hypot(px - ax, py - ay)
    c2 = vx * vx + vy * vy
    if c2 <= c1:
        return math.hypot(px - bx, py - by)
    t = c1 / c2
    qx = ax + t * vx
    qy = ay + t * vy
    return math.hypot(px - qx, py - qy)


def make_canvas(value: int) -> List[List[int]]:
    return [[value for _ in range(W * SCALE)] for _ in range(H * SCALE)]


def draw_rounded_rect(img: List[List[int]], x: float, y: float, w: float, h: float, r: float, value: int) -> None:
    xs, ys = int(x * SCALE), int(y * SCALE)
    xe, ye = int((x + w) * SCALE), int((y + h) * SCALE)
    for yy in range(max(0, ys), min(H * SCALE, ye)):
        cy = (yy + 0.5) / SCALE
        for xx in range(max(0, xs), min(W * SCALE, xe)):
            cx = (xx + 0.5) / SCALE
            if inside_round_rect(cx, cy, x, y, w, h, r):
                img[yy][xx] = value


def draw_rounded_rect_outline(img: List[List[int]], x: float, y: float, w: float, h: float, r: float, thickness: float, value: int) -> None:
    bg = img[0][0]
    xs, ys = int(x * SCALE), int(y * SCALE)
    xe, ye = int((x + w) * SCALE), int((y + h) * SCALE)
    for yy in range(max(0, ys), min(H * SCALE, ye)):
        cy = (yy + 0.5) / SCALE
        for xx in range(max(0, xs), min(W * SCALE, xe)):
            cx = (xx + 0.5) / SCALE
            outer = inside_round_rect(cx, cy, x, y, w, h, r)
            inner = inside_round_rect(
                cx,
                cy,
                x + thickness,
                y + thickness,
                w - 2 * thickness,
                h - 2 * thickness,
                max(0, r - thickness),
            )
            if outer and not inner:
                img[yy][xx] = value
            elif outer and inner:
                img[yy][xx] = bg


def draw_thick_segment(img: List[List[int]], a: Point, b: Point, thickness: float, value: int) -> None:
    ax, ay = a
    bx, by = b
    pad = thickness / 2 + 1
    xs = int((min(ax, bx) - pad) * SCALE)
    xe = int((max(ax, bx) + pad) * SCALE)
    ys = int((min(ay, by) - pad) * SCALE)
    ye = int((max(ay, by) + pad) * SCALE)
    for yy in range(max(0, ys), min(H * SCALE, ye + 1)):
        cy = (yy + 0.5) / SCALE
        for xx in range(max(0, xs), min(W * SCALE, xe + 1)):
            cx = (xx + 0.5) / SCALE
            if dist_to_segment(cx, cy, ax, ay, bx, by) <= thickness / 2:
                img[yy][xx] = value


def draw_polyline(img: List[List[int]], pts: Sequence[Point], thickness: float, value: int) -> None:
    for a, b in zip(pts, pts[1:]):
        draw_thick_segment(img, a, b, thickness, value)


def downsample(img: List[List[int]]) -> List[List[int]]:
    out = [[0 for _ in range(W)] for _ in range(H)]
    area = SCALE * SCALE
    for y in range(H):
        for x in range(W):
            total = 0
            for yy in range(y * SCALE, (y + 1) * SCALE):
                row = img[yy]
                for xx in range(x * SCALE, (x + 1) * SCALE):
                    total += row[xx]
            out[y][x] = int(round(total / area))
    return out


def write_bmp(path: str, pixels: List[List[int]]) -> None:
    row_stride = (W + 3) & ~3
    pixel_data_size = row_stride * H
    palette_size = 256 * 4
    offset = 14 + 40 + palette_size
    file_size = offset + pixel_data_size

    with open(path, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", file_size, 0, 0, offset))
        f.write(struct.pack("<IiiHHIIiiII", 40, W, H, 1, 8, 0, pixel_data_size, 3780, 3780, 256, 256))
        for i in range(256):
            f.write(bytes([i, i, i, 0]))
        pad = b"\0" * (row_stride - W)
        for y in range(H - 1, -1, -1):
            f.write(bytes(pixels[y]))
            f.write(pad)


def draw_icon(path: str, focused: bool) -> None:
    bg = 34 if focused else 255
    fg = 246 if focused else 24
    img = make_canvas(bg)

    # Icon tile border.
    draw_rounded_rect_outline(img, 3, 3, 100, 122, 12, 3.2, fg)

    # Book side covers / page blocks.
    draw_rounded_rect(img, 14, 45, 10, 55, 3, fg)
    draw_rounded_rect(img, 82, 45, 10, 55, 3, fg)

    # Open book outlines: bold, simple, readable at launcher size.
    left_page = [(22, 35), (34, 34), (45, 39), (53, 49), (53, 99), (42, 92), (29, 89), (22, 90), (22, 35)]
    right_page = [(53, 49), (61, 39), (72, 34), (84, 35), (84, 90), (77, 89), (64, 92), (53, 99)]
    draw_polyline(img, left_page, 4.0, fg)
    draw_polyline(img, right_page, 4.0, fg)

    # Lower page sweep reinforces the open-book silhouette.
    draw_polyline(img, [(18, 96), (35, 96), (47, 100), (53, 105), (59, 100), (72, 96), (88, 96)], 5.0, fg)

    # Reading stats bars.
    bars = [
        (31, 68, 8, 20),
        (44, 59, 8, 29),
        (57, 51, 8, 37),
        (70, 42, 8, 46),
    ]
    for x, y, w, h in bars:
        draw_rounded_rect(img, x, y, w, h, 2, fg)

    write_bmp(path, downsample(img))


def main(argv: Sequence[str]) -> int:
    out_dir = argv[1] if len(argv) > 1 else "dist"
    os.makedirs(out_dir, exist_ok=True)
    normal = os.path.join(out_dir, "ReadingStats.bmp")
    focused = os.path.join(out_dir, "ReadingStats_f.bmp")
    compat = os.path.join(out_dir, "ReadingStats.app.bmp")

    draw_icon(normal, focused=False)
    draw_icon(focused, focused=True)

    with open(normal, "rb") as src, open(compat, "wb") as dst:
        dst.write(src.read())

    for p in (normal, focused, compat):
        print(f"Icon: {p}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
