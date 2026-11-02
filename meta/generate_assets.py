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


def generate_banner(path: str) -> None:
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

            # Draw 4x4 mini board emblem in center
            bx = x - 80
            by = y - 16
            if 0 <= bx < 96 and 0 <= by < 96:
                cell_x = bx // 24
                cell_y = by // 24
                lx = bx % 24
                ly = by % 24
                if lx in (0, 23) or ly in (0, 23):
                    buf[idx : idx + 4] = bytes((70, 90, 130, 255))
                else:
                    piece = cell_y * 4 + cell_x
                    light = (piece & 4) != 0
                    square = (piece & 2) != 0
                    solid = (piece & 1) != 0
                    color = (245, 208, 105, 255) if light else (92, 168, 250, 255)
                    dx = lx - 12
                    dy = ly - 12
                    if square:
                        inside = abs(dx) <= 6 and abs(dy) <= 6
                        hole = not solid and abs(dx) <= 3 and abs(dy) <= 3
                    else:
                        r2 = dx * dx + dy * dy
                        inside = r2 <= 40
                        hole = not solid and r2 <= 12
                    if inside and not hole:
                        buf[idx : idx + 4] = bytes(color)

    write_png(path, w, h, bytes(buf))


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


def main() -> None:
    meta_dir = os.path.dirname(os.path.abspath(__file__))
    generate_icon(os.path.join(meta_dir, "icon.png"))
    generate_banner(os.path.join(meta_dir, "banner.png"))
    generate_audio(os.path.join(meta_dir, "audio.wav"))
    print("Generated meta/icon.png, meta/banner.png, meta/audio.wav")


if __name__ == "__main__":
    main()
