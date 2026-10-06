#!/usr/bin/env python3
"""Turns the app's background picture into the two files the console shows behind the app
(sce_sys/pic0.dds and pic1.dds).

This follows what worked for the EmulationStation port on the same console: uncompressed
1920x1080 pictures. When that port's own file is available it is used as the pattern, so the
header - and the order of the colours - is exactly the one known to work.

usage: make_dds.py picture.png pattern.dds-or-"" out0.dds [out1.dds ...]
"""
import struct, sys, zlib

W, H = 1920, 1080


def read_png(path):
    """A small PNG reader (8-bit RGB or RGBA, not interlaced), so no extra software is needed."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path} is not a PNG picture")
    pos, packed, width, height, kind = 8, b"", 0, 0, 0
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height, depth, kind, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or kind not in (2, 6) or interlace:
                raise SystemExit(f"{path}: only plain 8-bit RGB or RGBA pictures are supported")
        elif tag == b"IDAT":
            packed += body
        pos += 12 + length
    step = 4 if kind == 6 else 3
    raw = zlib.decompress(packed)
    stride = width * step
    rows, previous = [], bytearray(stride)
    at = 0
    for _ in range(height):
        mode = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        if mode == 1:
            for i in range(step, stride):
                line[i] = (line[i] + line[i - step]) & 255
        elif mode == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 255
        elif mode == 3:
            for i in range(stride):
                left = line[i - step] if i >= step else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 255
        elif mode == 4:
            for i in range(stride):
                a = line[i - step] if i >= step else 0
                b = previous[i]
                c = previous[i - step] if i >= step else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        previous = line
    return width, height, step, rows


def main():
    picture, pattern, outputs = sys.argv[1], sys.argv[2], sys.argv[3:]
    width, height, step, rows = read_png(picture)
    if (width, height) != (W, H):
        raise SystemExit(f"{picture} is {width}x{height}; it must be {W}x{H}")

    header, blue_first, source = None, False, "a standard header"
    try:
        known = open(pattern, "rb").read(148) if pattern else b""
    except OSError:
        known = b""
    if len(known) == 148 and known[:4] == b"DDS " and struct.unpack("<II", known[12:20]) == (H, W) and known[84:88] == b"DX10":
        header = known
        blue_first = struct.unpack("<I", known[128:132])[0] in (87, 88, 91, 93)     # the B8G8R8A8 family
        source = "the header of " + pattern
    if header is None:
        # DDS with the DX10 extension, R8G8B8A8, one picture, no smaller copies
        header = b"DDS " + struct.pack("<7I", 124, 0x100F, H, W, W * 4, 0, 1) + bytes(44)
        header += struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
        header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
        header += struct.pack("<5I", 28, 3, 0, 1, 0)
    assert len(header) == 148

    pixels = bytearray(W * H * 4)
    for y, line in enumerate(rows):
        out = memoryview(pixels)[y * W * 4:(y + 1) * W * 4]
        out[0::4] = line[2::step] if blue_first else line[0::step]
        out[1::4] = line[1::step]
        out[2::4] = line[0::step] if blue_first else line[2::step]
        out[3::4] = line[3::step] if step == 4 else b"\xff" * W
    for path in outputs:
        with open(path, "wb") as f:
            f.write(header)
            f.write(pixels)
    print(f"background: wrote {len(outputs)} file(s), {148 + len(pixels)} bytes each, using {source}")


if __name__ == "__main__":
    main()
