#!/usr/bin/env python3
"""Bake Mii head geometry out of a Wii U FFL resource into the blob the console reads.

    python3 tools/bake_mii_shapes.py <FFLResHigh.dat> assets/miishapes.bin [--report]

The console draws Miis flat, from `parts.bin` - 2D vector artwork with no depth.
This is the other option: the real geometry, out of the resource file Nintendo's
own Mii library renders from. The console cannot read that file itself, and
should not have to: it is big-endian, zlib-compressed per part, and laid out
around FFL's own structures. So it is converted here, once, into little-endian
arrays that a vertex buffer can be filled from directly.

Nothing about the resource file is redistributable, and neither is what comes
out of this. The output belongs on the card that baked it, the same way the
puzzle artwork does.

The container, established by reading the file rather than from documentation,
and cross-checked against FFLiResourceShapeHeader:

    0x0000  'FFRA', version 0x00070000
    0x0010  u32 partsMaxSize[12]      largest entry per texture part type
    0x0040  texture parts info        16 bytes each
    0x1710  u32 partsMaxSize[12]      largest entry per shape part type
    0x1740  shape parts info          16 bytes each, 857 of them

Each parts-info record is {offset, dataSize, compressedSize, zlib params}, and
every payload is plain zlib. A decompressed shape is:

    u32 elementPos[6]      position, normal, texcoord, tangent, colour, index
    u32 elementSize[6]
    f32 boundingBox[6]
    ..  transform          6 Vec3 for hair, 3 for faceline, absent otherwise
                           (elementPos[0] says which: 144, 108 or 72)

Positions are three floats padded to sixteen bytes, normals are packed
10_10_10_2 SNORM with x in the low bits, texcoords are two floats, and indices
are u16 triangle lists.
"""

import os
import struct
import sys
import zlib

# ---------------------------------------------------------------- the input

FFRA_MAGIC = b"FFRA"
FFRA_VERSION = 0x00070000
SHAPE_TABLE = 0x1740
SHAPE_MAXES = 0x1710
PARTS_INFO = struct.Struct(">IIIBBBB")

# FFLiResourceShapeHeader, in order. The sizes are FFL's own array dimensions:
# 4 beards, 132 per hair-indexed type, 12 per face-indexed type, 18 per nose.
# 48 + 857 * 16 == 0x35C0, which is what that header static-asserts.
SHAPE_TYPES = [
    ("beard", 4), ("cap1", 132), ("cap2", 132), ("faceline", 12), ("glass", 1),
    ("mask", 12), ("noseline", 18), ("nose", 18), ("hair1", 132), ("hair2", 132),
    ("forehead1", 132), ("forehead2", 132),
]

ELEM_POSITION, ELEM_NORMAL, ELEM_TEXCOORD, ELEM_TANGENT, ELEM_COLOUR, ELEM_INDEX = range(6)

# ---------------------------------------------------------------- the output

NXMS_MAGIC = b"NXMS"
NXMS_VERSION = 1
HEADER = struct.Struct("<4sHHII")     # magic, version, entries, payloadAt, reserved
ENTRY = struct.Struct("<IHHBBH")      # offset, verts, indices, flags, xformVecs, pad

FLAG_NORMALS = 1
FLAG_TEXCOORDS = 2


def unpack_normal(word):
    """10_10_10_2 SNORM, x in the low bits, over 511.

    Worked out by trying every plausible layout and keeping the one whose
    vectors come out unit length: this one is off by 0.0006 on average, the
    others by 0.19 or more.
    """
    def signed(v):
        return v - 1024 if v & 0x200 else v
    return tuple(signed((word >> shift) & 0x3FF) / 511.0 for shift in (0, 10, 20))


def read_shape(data, index):
    """One shape, decompressed and unpacked, or None when the slot is empty."""
    off, size, packed, level, window, memory, strategy = PARTS_INFO.unpack_from(
        data, SHAPE_TABLE + index * 16)
    if off == 0 or packed == 0 or off + packed > len(data):
        return None

    blob = zlib.decompress(data[off:off + packed])
    if len(blob) != size:
        raise ValueError(f"slot {index} decompressed to {len(blob)}, not {size}")

    pos = struct.unpack_from(">6I", blob, 0)
    length = struct.unpack_from(">6I", blob, 24)
    bbox = struct.unpack_from(">6f", blob, 48)

    if length[ELEM_POSITION] % 16:
        raise ValueError(f"slot {index} has {length[ELEM_POSITION]} bytes of positions")
    # A slot can be present and hold nothing: some hairstyles are bald, and the
    # resource carries a well-formed header with no geometry behind it. Nothing
    # to draw is not the same as a missing part, but the console does the same
    # thing with both, so it is recorded as absent.
    if length[ELEM_POSITION] == 0:
        return None
    verts = length[ELEM_POSITION] // 16

    # Where the vertex data starts says which transform the part carries: hair
    # gets six Vec3, faceline three (hair, face centre and beard anchors), and
    # everything else none.
    xform_bytes = max(0, pos[ELEM_POSITION] - 72)
    if xform_bytes % 12:
        raise ValueError(f"slot {index} has a {xform_bytes}-byte transform")
    xform = struct.unpack_from(f">{xform_bytes // 4}f", blob, 72) if xform_bytes else ()

    positions = [struct.unpack_from(">3f", blob, pos[ELEM_POSITION] + v * 16)
                 for v in range(verts)]

    normals = []
    if length[ELEM_NORMAL] == verts * 4:
        normals = [unpack_normal(struct.unpack_from(">I", blob,
                                                   pos[ELEM_NORMAL] + v * 4)[0])
                   for v in range(verts)]

    uvs = []
    if length[ELEM_TEXCOORD] == verts * 8:
        uvs = [struct.unpack_from(">2f", blob, pos[ELEM_TEXCOORD] + v * 8)
               for v in range(verts)]

    count = length[ELEM_INDEX] // 2
    indices = struct.unpack_from(f">{count}H", blob, pos[ELEM_INDEX]) if count else ()
    if indices and max(indices) >= verts:
        raise ValueError(f"slot {index} indexes vertex {max(indices)} of {verts}")

    return dict(bbox=bbox, xform=xform, positions=positions, normals=normals,
                uvs=uvs, indices=indices)


def bake(src, dest, report=False):
    with open(src, "rb") as handle:
        data = handle.read()

    magic, version = struct.unpack_from(">4sI", data, 0)
    if magic != FFRA_MAGIC:
        raise SystemExit(f"{src} is not an FFL resource ({magic!r})")
    if version != FFRA_VERSION:
        raise SystemExit(f"{src} is resource version 0x{version:08x}, expected "
                         f"0x{FFRA_VERSION:08x}")

    total = sum(n for _, n in SHAPE_TYPES)
    maxes = struct.unpack_from(">12I", data, SHAPE_MAXES)

    entries = []
    payload = bytearray()
    stats = []

    slot = 0
    for kind, (name, count) in enumerate(SHAPE_TYPES):
        present = 0
        biggest = 0
        verts_here = 0
        for _ in range(count):
            shape = read_shape(data, slot)
            slot += 1
            if shape is None:
                entries.append(None)
                continue

            present += 1
            verts = len(shape["positions"])
            verts_here += verts
            off, size, packed, *_ = PARTS_INFO.unpack_from(data, SHAPE_TABLE
                                                           + (slot - 1) * 16)
            biggest = max(biggest, size)

            at = len(payload)
            payload += struct.pack("<6f", *shape["bbox"])
            if shape["xform"]:
                payload += struct.pack(f"<{len(shape['xform'])}f", *shape["xform"])
            for x, y, z in shape["positions"]:
                payload += struct.pack("<3f", x, y, z)

            flags = 0
            if shape["normals"]:
                flags |= FLAG_NORMALS
                for x, y, z in shape["normals"]:
                    # Signed bytes, which is what a vertex attribute wants and
                    # a tenth of the precision nobody will see at this size.
                    payload += struct.pack("<4b", *(max(-127, min(127, int(round(c * 127))))
                                                    for c in (x, y, z)), 0)
            if shape["uvs"]:
                flags |= FLAG_TEXCOORDS
                for u, v in shape["uvs"]:
                    payload += struct.pack("<2f", u, v)

            payload += struct.pack(f"<{len(shape['indices'])}H", *shape["indices"])

            entries.append(dict(offset=at, verts=verts, indices=len(shape["indices"]),
                                flags=flags, xform=len(shape["xform"]) // 3))

        # The resource declares the largest entry of each type; if what we read
        # disagrees, the table has been partitioned wrongly and every index
        # after it would address the wrong part.
        if present and biggest != maxes[kind]:
            raise SystemExit(f"{name}: largest entry is {biggest}, resource says "
                             f"{maxes[kind]} - the slot ranges are wrong")
        stats.append((name, count, present, verts_here))

    payload_at = HEADER.size + total * ENTRY.size
    out = bytearray(HEADER.pack(NXMS_MAGIC, NXMS_VERSION, total, payload_at, 0))
    for e in entries:
        if e is None:
            out += ENTRY.pack(0, 0, 0, 0, 0, 0)
        else:
            out += ENTRY.pack(payload_at + e["offset"], e["verts"], e["indices"],
                              e["flags"], e["xform"], 0)
    out += payload

    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    with open(dest, "wb") as handle:
        handle.write(out)

    if report:
        print(f"  {'type':<11} {'slots':>6} {'present':>8} {'vertices':>10}")
        for name, count, present, verts in stats:
            print(f"  {name:<11} {count:>6} {present:>8} {verts:>10,}")
    live = sum(1 for e in entries if e)
    print(f"{dest}: {live} shapes of {total} slots, "
          f"{sum(s[3] for s in stats):,} vertices, {len(out) / 1024:.0f} KB")
    return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    return bake(argv[1], argv[2], "--report" in argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
