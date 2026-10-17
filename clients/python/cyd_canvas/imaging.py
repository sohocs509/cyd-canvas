# SPDX-License-Identifier: Apache-2.0
"""PNG/JPEG -> LVGL v9 binary image (RGB565, or RGB565A8 when there is transparency)."""
from __future__ import annotations

import struct
from pathlib import Path

LV_IMAGE_HEADER_MAGIC = 0x19
LV_COLOR_FORMAT_RGB565 = 0x12
LV_COLOR_FORMAT_RGB565A8 = 0x14


def to_lvgl_bin(path, max_w: int = 320, max_h: int = 240) -> bytes:
    try:
        from PIL import Image
    except ImportError as e:  # pragma: no cover
        raise SystemExit("image conversion needs Pillow: pip install Pillow") from e
    img = Image.open(path).convert("RGBA")
    img.thumbnail((max_w, max_h))
    w, h = img.size
    rgba = img.tobytes()
    has_alpha = any(rgba[i] < 255 for i in range(3, len(rgba), 4))
    color = bytearray()
    alpha = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        color += struct.pack("<H", v)
        alpha.append(a)
    cf = LV_COLOR_FORMAT_RGB565A8 if has_alpha else LV_COLOR_FORMAT_RGB565
    # magic:8 cf:8 flags:16 | w:16 h:16 | stride:16 reserved:16   (little endian)
    header = struct.pack("<BBHHHHH", LV_IMAGE_HEADER_MAGIC, cf, 0, w, h, w * 2, 0)
    return header + bytes(color) + (bytes(alpha) if has_alpha else b"")


def default_asset_name(path) -> str:
    return Path(path).stem.lower().replace(" ", "-")[:28] + ".bin"
