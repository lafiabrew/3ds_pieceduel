import base64
#!/usr/bin/env python3
"""
Deterministic generator for PieceDuel 3DS homebrew packaging assets:
- meta/icon.png (48x48 RGBA PNG for smdhtool / bannertool)
- meta/banner.png (256x128 RGBA PNG for bannertool makebanner)
- meta/audio.wav (short 1-second chime WAV for bannertool makebanner)
Uses only Python's standard library (zlib, struct, math, wave).
"""

import math
import os
import struct
import wave
import zlib
from generate_bnr_shared import draw_banner_text_centered, write_cbmd_bnr  # type: ignore


def write_png(path: str, width: int, height: int, rgba_bytes: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    raw_rows = bytearray()
    stride = width * 4
    for y in range(height):
        raw_rows.append(0)  # Filter type 0 (None)
        raw_rows.extend(rgba_bytes[y * stride : (y + 1) * stride])

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    compressed = zlib.compress(bytes(raw_rows), 9)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", compressed))
        f.write(chunk(b"IEND", b""))


def generate_icon(path: str) -> None:
    w, h = 48, 48
    buf = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            idx = (y * w + x) * 4
            # Deep slate board background with rounded gold border
            border = x < 3 or x >= w - 3 or y < 3 or y >= h - 3
            if border:
                buf[idx : idx + 4] = bytes((230, 185, 75, 255))
                continue

            # 2x2 showcase of PieceDuel pieces (tall/short, light/dark, square/round, solid/hollow)
            gx = 0 if x < 24 else 1
            gy = 0 if y < 24 else 1
            cx = 13 if gx == 0 else 35
            cy = 13 if gy == 0 else 35
            dx = x - cx
            dy = y - cy

            # Grid divider lines
            if x == 24 or y == 24:
                buf[idx : idx + 4] = bytes((50, 62, 88, 255))
                continue

            buf[idx : idx + 4] = bytes((24, 30, 46, 255))

            if gx == 0 and gy == 0:
                # Light Square Solid
                if abs(dx) <= 6 and abs(dy) <= 6:
                    buf[idx : idx + 4] = bytes((245, 210, 110, 255))
            elif gx == 1 and gy == 0:
                # Dark Round Hollow
                r2 = dx * dx + dy * dy
                if 10 <= r2 <= 42:
                    buf[idx : idx + 4] = bytes((95, 165, 255, 255))
            elif gx == 0 and gy == 1:
                # Dark Square Hollow
                if abs(dx) <= 6 and abs(dy) <= 6 and (abs(dx) >= 4 or abs(dy) >= 4):
                    buf[idx : idx + 4] = bytes((95, 165, 255, 255))
            else:
                # Light Round Solid
                if dx * dx + dy * dy <= 42:
                    buf[idx : idx + 4] = bytes((245, 210, 110, 255))

    write_png(path, w, h, bytes(buf))


def generate_banner(path: str) -> bytes:
    w, h = 256, 128
    buf = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            idx = (y * w + x) * 4
            # Gradient dark navy background
            r = 18 + (y * 16) // h
            g = 24 + (y * 22) // h
            b = 42 + (y * 36) // h
            buf[idx : idx + 4] = bytes((r, g, b, 255))

            # Draw 4x4 mini board emblem in upper center (x: 96..160, y: 12..76)
            bx = x - 96
            by = y - 12
            if 0 <= bx < 64 and 0 <= by < 64:
                cell_x = bx // 16
                cell_y = by // 16
                lx = bx % 16
                ly = by % 16
                if lx in (0, 15) or ly in (0, 15):
                    buf[idx : idx + 4] = bytes((70, 90, 130, 255))
                else:
                    piece = cell_y * 4 + cell_x
                    light = (piece & 4) != 0
                    square = (piece & 2) != 0
                    solid = (piece & 1) != 0
                    color = (245, 208, 105, 255) if light else (92, 168, 250, 255)
                    dx = lx - 8
                    dy = ly - 8
                    if square:
                        inside = abs(dx) <= 4 and abs(dy) <= 4
                        hole = not solid and abs(dx) <= 2 and abs(dy) <= 2
                    else:
                        r2 = dx * dx + dy * dy
                        inside = r2 <= 20
                        hole = not solid and r2 <= 6
                    if inside and not hole:
                        buf[idx : idx + 4] = bytes(color)

    draw_banner_text_centered(buf, w, h, 86, "PIECEDUEL", 2, (245, 210, 105, 255))
    draw_banner_text_centered(buf, w, h, 108, "STRATEGY BOARD GAME", 1, (185, 215, 255, 255))
    raw = bytes(buf)
    write_png(path, w, h, raw)
    return raw


def generate_audio(path: str) -> None:
    sample_rate = 22050
    duration_s = 0.8
    n_samples = int(sample_rate * duration_s)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        frames = bytearray()
        for i in range(n_samples):
            t = i / sample_rate
            freq = 523.25 if t < 0.25 else (659.25 if t < 0.5 else 783.99)
            env = max(0.0, 1.0 - (t / duration_s))
            val = int(12000 * env * math.sin(2.0 * math.pi * freq * t))
            frames.extend(struct.pack("<h", val))
        wf.writeframes(bytes(frames))



CBMD_PREFIX_ZLIB_B64 = (
    "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"
)


def generate_bnr(path: str) -> None:
    prefix = zlib.decompress(base64.b64decode(CBMD_PREFIX_ZLIB_B64))
    sample_rate = 22050
    duration_s = 0.8
    n_samples = int(sample_rate * duration_s)
    frames = bytearray()
    for i in range(n_samples):
        t = i / sample_rate
        freq = 523.25 if t < 0.25 else (659.25 if t < 0.5 else 783.99)
        env = max(0.0, 1.0 - (t / duration_s))
        val = int(12000 * env * math.sin(2.0 * math.pi * freq * t))
        frames.extend(struct.pack("<h", val))
    with open(path, "wb") as f:
        f.write(prefix)
        f.write(bytes(frames))


def main() -> None:
    meta_dir = os.path.dirname(os.path.abspath(__file__))
    generate_icon(os.path.join(meta_dir, "icon.png"))
    banner_rgba = generate_banner(os.path.join(meta_dir, "banner.png"))
    generate_audio(os.path.join(meta_dir, "audio.wav"))
    write_cbmd_bnr(os.path.join(meta_dir, "banner.bnr"), (523.25, 659.25, 783.99), banner_rgba)
    print("Generated meta/icon.png, meta/banner.png, meta/audio.wav, meta/banner.bnr")


if __name__ == "__main__":
    main()
