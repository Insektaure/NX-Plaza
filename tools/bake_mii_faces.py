#!/usr/bin/env python3
"""Bake Mii face artwork out of a Wii U FFL resource into one atlas.

    python3 tools/bake_mii_faces.py <FFLResHigh.dat> assets/miifaces.bin [--preview DIR]

The geometry baked by bake_mii_shapes.py gives a head and a hairstyle and no
face: FFL keeps no face texture, it builds one per Mii at run time out of
separate eye, eyebrow, mouth, moustache and mole textures. This packs those
into a single atlas the console binds once, so each feature is a quad with a
pair of texture coordinates rather than a render target and a compositing pass.

Two things about the stored textures matter enough to write down, both worked
out by decoding them rather than from any documentation:

  * They are **not tiled**. A GX2 surface normally is, and undoing that is the
    part everybody warns about, but these are plain linear rows with the pitch
    the footer gives. Decoding one straight out and looking at it produced an
    eye first time.

  * The colours are **not colours**. An RGBA8 texture carries three masks in
    R, G and B and its coverage in A; the console multiplies each mask by a
    colour the Mii itself supplies. An R8 texture is one mask, tinted the same
    way. This is the same arrangement tools/bake_mii_parts.py uses for the flat
    artwork, arrived at independently.

Nothing here is redistributable and neither is what comes out: the output
belongs on the card that baked it.
"""

import os
import struct
import sys
import zlib

TEX_TABLE = 0x40
TEX_MAXES = 0x10
PARTS_INFO = struct.Struct(">IIIBBBB")
# mipOffset, width, height, numMips, format - and two bytes of tail padding,
# because FFLiResourceTextureFooter static-asserts to 0xC and those five fields
# come to ten. Reading at len - 10 instead of len - 12 lands two bytes in and
# every dimension comes out nonsense.
FOOTER = struct.Struct(">IHHBB")
FOOTER_SIZE = 12

# The texture table, derived by fingerprinting each type's largest entry
# against partsMaxSize[] and matching the runs of equally-shaped textures.
# The counts sum to exactly 365, which is the whole table.
TEX_TYPES = [
    ("beard", 3), ("cap", 132), ("eye", 80), ("eyebrow", 28), ("faceline", 12),
    ("makeup", 12), ("glass", 20), ("mole", 2), ("mouth", 52), ("mustache", 6),
    ("noseline", 18),
]

# What a mask needs. The rest stay in the resource: caps and facelines are for
# hats and skin detail, and neither is drawn yet.
WANTED = ("eye", "eyebrow", "mouth", "mustache", "mole", "glass", "noseline")

# Quarter size. A face in the square is about seventy pixels tall, so an eye
# lands on fifteen or so; a hundred and fifty pixels of source for that is
# paying for detail nobody can see. Full size would be 22 MB against the 3 MB
# of image memory the glyph atlas leaves.
REDUCE = 4

MAGIC = b"NXMF"
VERSION = 1
HEADER = struct.Struct("<4sHHHHII")  # magic, version, entries, atlasW, atlasH, payloadAt, reserved
ENTRY = struct.Struct("<HHHHBBH")    # x, y, w, h, channels, flags, reserved


def read_texture(data, index):
    """(width, height, channels, rows) for one slot, or None when empty."""
    off, size, packed, *_ = PARTS_INFO.unpack_from(data, TEX_TABLE + index * 16)
    if off == 0 or packed == 0 or off + packed > len(data):
        return None
    blob = zlib.decompress(data[off:off + packed])
    if len(blob) != size or size < FOOTER_SIZE:
        return None
    mip_offset, w, h, mips, fmt = FOOTER.unpack_from(blob, len(blob) - FOOTER_SIZE)
    if w == 0 or h == 0 or fmt > 2:
        return None
    channels = (1, 2, 4)[fmt]
    # An 8x8 texture is a placeholder for a part the resource does not carry.
    if w <= 8 and h <= 8:
        return None
    if w * h * channels > len(blob):
        return None
    return w, h, channels, blob


def downscale(w, h, channels, blob, div):
    """Box filter, which is all a mask wants: no gamma, no sharpening."""
    ow, oh = max(1, w // div), max(1, h // div)
    out = bytearray(ow * oh * 4)
    for y in range(oh):
        for x in range(ow):
            acc = [0, 0, 0, 0]
            n = 0
            for sy in range(y * div, min((y + 1) * div, h)):
                row = sy * w
                for sx in range(x * div, min((x + 1) * div, w)):
                    o = (row + sx) * channels
                    if channels == 1:
                        # One mask, and it goes in alpha with zero in the other
                        # three. The shader blends a colour over its base for
                        # each of R, G and B, so zeros make those blends no-ops
                        # and the whole thing comes out in the base colour -
                        # which for an eyebrow or a moustache is exactly right.
                        acc[3] += blob[o]
                    elif channels == 2:
                        # Glasses. R is the coverage; G marks the lens, which
                        # goes in the green mask so a shader can tint it apart
                        # from the frame.
                        acc[1] += blob[o + 1]
                        acc[3] += blob[o]
                    else:
                        for c in range(4):
                            acc[c] += blob[o + c]
                    n += 1
            i = (y * ow + x) * 4
            for c in range(4):
                out[i + c] = acc[c] // n if n else 0
    return ow, oh, out


def pack(items, width):
    """Shelf packing: rows of whatever fits, which for uniform parts is tight."""
    x = y = shelf = 0
    placed = []
    for w, h in items:
        if x + w > width:
            x = 0
            y += shelf
            shelf = 0
        placed.append((x, y))
        x += w
        shelf = max(shelf, h)
    return placed, y + shelf


def bake(src, dest, preview=None):
    with open(src, "rb") as handle:
        data = handle.read()
    if data[:4] != b"FFRA":
        raise SystemExit(f"{src} is not an FFL resource")

    maxes = struct.unpack_from(">12I", data, TEX_MAXES)

    base = 0
    bases = {}
    for name, count in TEX_TYPES:
        bases[name] = base
        base += count
    if base != 365:
        raise SystemExit(f"the part table sums to {base}, not 365")

    # Decode everything wanted, keeping each type's slots in order.
    decoded = {}
    for name in WANTED:
        count = dict(TEX_TYPES)[name]
        run = []
        for i in range(count):
            t = read_texture(data, bases[name] + i)
            if t is None:
                run.append(None)
                continue
            w, h, channels, blob = t
            run.append((channels,) + downscale(w, h, channels, blob, REDUCE))
        decoded[name] = run
        live = sum(1 for r in run if r)
        print(f"  {name:9} {live:>3} of {count:<3} "
              f"{'' if not live else f'{run[next(i for i, r in enumerate(run) if r)][1]}x'
                                    f'{run[next(i for i, r in enumerate(run) if r)][2]}'}")

    order = [(name, i) for name in WANTED for i in range(len(decoded[name]))]
    sizes = [(decoded[n][i][1], decoded[n][i][2]) if decoded[n][i] else (0, 0)
             for n, i in order]
    atlas_w = 1024
    placed, atlas_h = pack([s for s in sizes if s != (0, 0)], atlas_w)
    # Round the height up so the image is a sane shape to allocate.
    atlas_h = (atlas_h + 3) & ~3

    atlas = bytearray(atlas_w * atlas_h * 4)
    entries = []
    at = 0
    for (name, i), size in zip(order, sizes):
        cell = decoded[name][i]
        if not cell:
            entries.append(None)
            continue
        channels, w, h, pixels = cell
        px, py = placed[at]
        at += 1
        for row in range(h):
            dst = ((py + row) * atlas_w + px) * 4
            src_o = row * w * 4
            atlas[dst:dst + w * 4] = pixels[src_o:src_o + w * 4]
        entries.append((px, py, w, h, channels))

    payload_at = HEADER.size + len(entries) * ENTRY.size
    out = bytearray(HEADER.pack(MAGIC, VERSION, len(entries), atlas_w, atlas_h,
                                payload_at, 0))
    for e in entries:
        out += ENTRY.pack(0, 0, 0, 0, 0, 0, 0) if e is None else \
            ENTRY.pack(e[0], e[1], e[2], e[3], e[4], 0, 0)
    out += atlas

    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    with open(dest, "wb") as handle:
        handle.write(out)
    print(f"\n{dest}: {sum(1 for e in entries if e)} features in a "
          f"{atlas_w}x{atlas_h} atlas, {len(out) / 1024:.0f} KB")

    if preview:
        os.makedirs(preview, exist_ok=True)
        path = os.path.join(preview, "atlas.png")

        def chunk(t, d):
            c = struct.pack(">I", len(d)) + t + d
            return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

        rgb = bytearray(atlas_w * atlas_h * 3)
        for p in range(atlas_w * atlas_h):
            a = atlas[p * 4 + 3] / 255.0
            bg = 200 if ((p % atlas_w) // 16 + (p // atlas_w) // 16) & 1 else 150
            for c in range(3):
                rgb[p * 3 + c] = int(atlas[p * 4 + c] * a + bg * (1 - a))
        raw = b"".join(b"\x00" + bytes(rgb[y * atlas_w * 3:(y + 1) * atlas_w * 3])
                       for y in range(atlas_h))
        with open(path, "wb") as handle:
            handle.write(b"\x89PNG\r\n\x1a\n"
                         + chunk(b"IHDR", struct.pack(">IIBBBBB", atlas_w, atlas_h,
                                                      8, 2, 0, 0, 0))
                         + chunk(b"IDAT", zlib.compress(raw, 6))
                         + chunk(b"IEND", b""))
        print(f"  preview: {path}")
    return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    preview = None
    if "--preview" in argv:
        preview = argv[argv.index("--preview") + 1]
    return bake(argv[1], argv[2], preview)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
