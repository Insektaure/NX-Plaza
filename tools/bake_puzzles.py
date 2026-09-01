#!/usr/bin/env python3
"""Bake puzzle artwork into the blob the console reads a picture out of.

    python3 tools/bake_puzzles.py <png-dir> romfs/puzzles/pictures.bin --check

The console cannot hold every picture at once: image memory is a bump allocator
with no free (source/gfx/mempool.cpp), and the glyph atlas already has 9 of its
12 MB. But it never needs to - only one puzzle is ever open. So this writes an
indexed file the runtime seeks into, uploading a single picture into a single
reused slot, and the number of puzzles stops costing memory at all.

Pictures are stored as BC1: four bits a pixel, decoded by the GPU's texture
units at no cost, so a 1280x720 picture is 450 KB rather than 3.5 MB. The
alternative - RGBA8 - does not fit even once in the room that is left.

Names come from the file names, which is what source/core/pieces.cpp puts in
each puzzle's `image` field. Nothing at runtime opens this directory; it is
read here, at bake time, and does not have to exist on a build machine that is
not baking.
"""

import os
import re
import struct
import sys
import zlib

MAGIC = b"NXPI"
VERSION = 1
FMT_BC1 = 0
KEY_LEN = 32
ENTRY = struct.Struct("<32sHHBBBBII")   # key, w, h, format, pad*3, offset, size
HEADER = struct.Struct("<4sHHI")


# ----------------------------------------------------------------- png

def read_png(path: str):
    """(width, height, RGB bytes). Enough PNG for the artwork, not a library."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")

    pos = 8
    width = height = depth = colour = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length          # length, type, body, crc
        if kind == b"IHDR":
            width, height, depth, colour, comp, filt, interlace = \
                struct.unpack(">IIBBBBB", body)
            if depth != 8:
                raise ValueError(f"{depth}-bit PNG; only 8 is handled")
            if colour not in (2, 6):
                raise ValueError(f"colour type {colour}; only 2 (RGB) and 6 (RGBA)")
            if interlace:
                raise ValueError("interlaced PNG")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break

    if width is None:
        raise ValueError("no IHDR")

    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * width * 3)

    # Undo the per-scanline filters. Each line is prefixed with its filter type
    # and predicted from the pixel to its left (a) and the line above (b).
    prev = bytearray(stride)
    pos = 0
    for row in range(height):
        filt = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filt == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif filt != 0:
            raise ValueError(f"filter {filt} on row {row}")
        prev = line

        if channels == 3:
            out[row * width * 3:(row + 1) * width * 3] = line
        else:
            base = row * width * 3
            for x in range(width):
                out[base + x * 3:base + x * 3 + 3] = line[x * 4:x * 4 + 3]

    return width, height, bytes(out)


# ----------------------------------------------------------------- bc1

def pack565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def unpack565(c: int):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def encode_bc1(width: int, height: int, rgb: bytes) -> bytes:
    """One 8-byte block per 4x4 pixels: two endpoints and sixteen 2-bit indices.

    The endpoints are the extremes along the block's dominant axis, which for a
    4x4 patch of a painting is close enough to optimal that the difference does
    not survive being drawn at a third of the size.
    """
    if width % 4 or height % 4:
        raise ValueError(f"{width}x{height} is not a multiple of 4")

    out = bytearray(width * height // 2)
    at = 0
    row3 = width * 3
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            # Gather the block once; everything below reads these lists.
            px = []
            for y in range(4):
                base = (by + y) * row3 + bx * 3
                for x in range(4):
                    i = base + x * 3
                    px.append((rgb[i], rgb[i + 1], rgb[i + 2]))

            # Extremes along the axis with the widest spread. Cheap, and on
            # smooth artwork indistinguishable from a least-squares fit.
            rs = [p[0] for p in px]
            gs = [p[1] for p in px]
            bs = [p[2] for p in px]
            spread = (max(rs) - min(rs), max(gs) - min(gs), max(bs) - min(bs))
            axis = spread.index(max(spread))
            lo = min(px, key=lambda p: p[axis])
            hi = max(px, key=lambda p: p[axis])

            c0 = pack565(*hi)
            c1 = pack565(*lo)
            if c0 == c1:
                # A flat block: every index reads endpoint 0.
                out[at:at + 8] = struct.pack("<HHI", c0, c1, 0)
                at += 8
                continue
            if c0 < c1:
                c0, c1 = c1, c0
                lo, hi = hi, lo

            # The palette the hardware will build, so indices are chosen
            # against what actually gets sampled rather than the originals.
            e0 = unpack565(c0)
            e1 = unpack565(c1)
            p2 = ((2 * e0[0] + e1[0]) // 3, (2 * e0[1] + e1[1]) // 3,
                  (2 * e0[2] + e1[2]) // 3)
            p3 = ((e0[0] + 2 * e1[0]) // 3, (e0[1] + 2 * e1[1]) // 3,
                  (e0[2] + 2 * e1[2]) // 3)
            palette = (e0, e1, p2, p3)

            bits = 0
            for n, (r, g, b) in enumerate(px):
                best = 0
                bestd = 1 << 30
                for k in range(4):
                    q = palette[k]
                    dr = r - q[0]; dg = g - q[1]; db = b - q[2]
                    d = dr * dr + dg * dg + db * db
                    if d < bestd:
                        bestd = d
                        best = k
                bits |= best << (n * 2)
            out[at:at + 8] = struct.pack("<HHI", c0, c1, bits)
            at += 8
    return bytes(out)


def decode_bc1(width: int, height: int, blob: bytes) -> bytes:
    """The GPU's side of the deal, for checking the encoder against the source."""
    out = bytearray(width * height * 3)
    row3 = width * 3
    at = 0
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            c0, c1, bits = struct.unpack_from("<HHI", blob, at)
            at += 8
            e0, e1 = unpack565(c0), unpack565(c1)
            if c0 > c1:
                palette = (e0, e1,
                           tuple((2 * e0[i] + e1[i]) // 3 for i in range(3)),
                           tuple((e0[i] + 2 * e1[i]) // 3 for i in range(3)))
            else:
                palette = (e0, e1, tuple((e0[i] + e1[i]) // 2 for i in range(3)),
                           (0, 0, 0))
            for n in range(16):
                q = palette[(bits >> (n * 2)) & 3]
                i = (by + n // 4) * row3 + (bx + n % 4) * 3
                out[i], out[i + 1], out[i + 2] = q
    return bytes(out)


# ---------------------------------------------------------------- bake

def key_for(path: str) -> str:
    name = os.path.splitext(os.path.basename(path))[0].lower()
    key = re.sub(r"[^a-z0-9_]+", "_", name).strip("_")
    if not key:
        raise ValueError(f"{path} has no usable name")
    if len(key) >= KEY_LEN:
        raise ValueError(f"'{key}' is longer than {KEY_LEN - 1} characters")
    return key


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, dest = argv[1], argv[2]
    check = "--check" in argv

    if not os.path.isdir(src):
        print(f"{src} is not a directory", file=sys.stderr)
        return 1
    pngs = sorted(f for f in os.listdir(src) if f.lower().endswith(".png"))
    if not pngs:
        print(f"no PNGs in {src}", file=sys.stderr)
        return 1

    entries = []
    payloads = []
    for name in pngs:
        path = os.path.join(src, name)
        key = key_for(path)
        width, height, rgb = read_png(path)
        blob = encode_bc1(width, height, rgb)
        entries.append((key, width, height))
        payloads.append(blob)
        line = (f"  {key:24} {width}x{height}  {len(blob) / 1024:7.1f} KB"
                f"  ({len(rgb) / len(blob):.1f}x smaller)")
        if check:
            back = decode_bc1(width, height, blob)
            err = sum((a - b) ** 2 for a, b in zip(rgb, back)) / len(rgb)
            psnr = 10 * __import__("math").log10(255 * 255 / err) if err else 99.0
            line += f"  PSNR {psnr:.1f} dB"
        print(line)

    offset = HEADER.size + ENTRY.size * len(entries)
    out = bytearray(HEADER.pack(MAGIC, VERSION, len(entries), 0))
    for (key, width, height), blob in zip(entries, payloads):
        out += ENTRY.pack(key.encode("ascii"), width, height, FMT_BC1, 0, 0, 0,
                          offset, len(blob))
        offset += len(blob)
    for blob in payloads:
        out += blob

    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    with open(dest, "wb") as handle:
        handle.write(out)
    print(f"{dest}: {len(entries)} pictures, {len(out) / 1024:.0f} KB "
          f"({max(len(b) for b in payloads) / 1024:.0f} KB is the largest, "
          f"which is what the console reserves)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
