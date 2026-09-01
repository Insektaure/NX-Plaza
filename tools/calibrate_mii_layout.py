#!/usr/bin/env python3
"""Measure where a Mii's features actually sit, using Nintendo's own renderer.

The part outlines in `romfs/mii/parts.bin` are the reference project's artwork,
but nothing in that project composes them in 2D -- it renders Miis by asking a
remote FFL server for a picture. So the layout in `source/ui/mii_render.cpp` --
where an eye sits, how far a nose is down the face -- had to be written by hand,
and hand-written numbers are where this app's Mii bugs have all been.

This asks that renderer instead. It draws a bald reference Mii, then redraws it
with one feature swept between its extremes, and takes the centroid of whatever
changed: that is where the feature lives. The result is quoted as a fraction of
the face's own outline, which is exactly the unit `kPlace` uses.

    python3 tools/calibrate_mii_layout.py

It needs the network and it talks to a third party, so it is a development tool
and nothing in the build depends on it. Renders are cached under `fflcache/`.

Measured 2026-08-29 against mii-unsecure.ariankordi.net:

    feature     FFL     was      now
    eyes       0.536   0.520    0.536
    eyebrows   0.397   0.395    0.397
    nose       0.681   0.630    0.681   <- the one real miss, 5% of a face
    mouth      0.770   0.765    0.770
    eye x      0.189   0.200    0.189
    brow x     0.225   0.200    0.225

The face's aspect was checked too and deliberately left alone. Measured to the
lowest skin pixel FFL reads 1.31 against the artwork's 1.24, which looks like a
6% error -- but that measurement includes the neck. Measured down to the chin,
where the skin run collapses, FFL's median is 1.230 against 1.239. They agree,
and stretching the outlines to chase the difference would only have distorted
the artwork.

The colour tables are not measured here: they are copied verbatim from the
reference's ColorTables.ts, in its order, and `tools/` has no business
re-deriving them from pixels.
"""

import hashlib
import os
import struct
import urllib.request
import zlib

# The field order is the wire format; it is not alphabetical and must not move.
FIELDS = [
    "facialHairColor", "beardType", "build", "eyeVerticalStretch", "eyeColor",
    "eyeRotation", "eyeScale", "eyeType", "eyeSpacing", "eyeYPosition",
    "eyebrowVerticalStretch", "eyebrowColor", "eyebrowRotation", "eyebrowScale",
    "eyebrowType", "eyebrowSpacing", "eyebrowYPosition", "skinColor", "makeupType",
    "faceType", "wrinklesType", "favoriteColor", "gender", "glassesColor",
    "glassesScale", "glassesType", "glassesYPosition", "hairColor", "flipHair",
    "hairType", "height", "moleScale", "moleEnabled", "moleXPosition",
    "moleYPosition", "mouthHorizontalStretch", "mouthColor", "mouthScale",
    "mouthType", "mouthYPosition", "mustacheScale", "mustacheType",
    "mustacheYPosition", "noseScale", "noseType", "noseYPosition",
]

# FFL's own neutral Mii, at the midpoints its editor uses.
DEFAULT = {
    "facialHairColor": 0, "beardType": 0, "build": 64, "eyeVerticalStretch": 3,
    "eyeColor": 8, "eyeRotation": 4, "eyeScale": 4, "eyeType": 2, "eyeSpacing": 2,
    "eyeYPosition": 12, "eyebrowVerticalStretch": 3, "eyebrowColor": 0,
    "eyebrowRotation": 6, "eyebrowScale": 4, "eyebrowType": 0, "eyebrowSpacing": 2,
    "eyebrowYPosition": 10, "skinColor": 0, "makeupType": 0, "faceType": 0,
    "wrinklesType": 0, "favoriteColor": 0, "gender": 0, "glassesColor": 8,
    "glassesScale": 4, "glassesType": 0, "glassesYPosition": 10, "hairColor": 0,
    "flipHair": 0, "hairType": 33, "height": 64, "moleScale": 4, "moleEnabled": 0,
    "moleXPosition": 2, "moleYPosition": 20, "mouthHorizontalStretch": 3,
    "mouthColor": 19, "mouthScale": 4, "mouthType": 1, "mouthYPosition": 13,
    "mustacheScale": 4, "mustacheType": 0, "mustacheYPosition": 9, "noseScale": 4,
    "noseType": 1, "noseYPosition": 9,
}


def encode_studio(**overrides) -> str:
    mii = dict(DEFAULT)
    mii.update(overrides)
    out = bytearray(0x2F)
    prev = 256
    for i, name in enumerate(FIELDS):
        prev = (7 + (mii[name] ^ prev)) % 256
        out[i + 1] = prev
    return out.hex()


if __name__ == "__main__":
    print(encode_studio())

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fflcache")
os.makedirs(CACHE, exist_ok=True)
BASE = "https://mii-unsecure.ariankordi.net/miis/image.png"

def fetch(**ov):
    data = encode_studio(**ov)
    # The whole blob, not a prefix: the encoding chains each byte off the last,
    # so two Miis differing only in a late field share their first bytes.
    path = os.path.join(CACHE, hashlib.sha1(data.encode()).hexdigest()[:16] + ".png")
    if not os.path.exists(path):
        url = f"{BASE}?shaderType=wiiu&type=face&width=320&verifyCharInfo=0&data={data}"
        with urllib.request.urlopen(url, timeout=30) as r:
            open(path, "wb").write(r.read())
    return read_png(path)

def read_png(path):
    raw = open(path, "rb").read()
    pos, w, h, idat = 8, 0, 0, b""
    while pos < len(raw):
        ln = struct.unpack(">I", raw[pos:pos+4])[0]; typ = raw[pos+4:pos+8]
        body = raw[pos+8:pos+8+ln]
        if typ == b"IHDR": w, h, bd, ct = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT": idat += body
        pos += 12 + ln
    data = zlib.decompress(idat)
    bpp = 4
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = data[p]; p += 1
        line = bytearray(data[p:p+stride]); p += stride
        for i in range(stride):
            a = line[i-bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i-bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp-a), abs(pp-b), abs(pp-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y*stride:(y+1)*stride] = line
        prev = line
    return w, h, out

def skin_bbox(w, h, px):
    """The face outline: skin-coloured, opaque, and not the shirt."""
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            i = (y*w + x) * 4
            r, g, b, a = px[i], px[i+1], px[i+2], px[i+3]
            if a < 200: continue
            # pale skin: warm, bright, low saturation, and not the red shirt
            if r > 170 and g > 120 and b > 100 and r > b and (r - b) < 90:
                if x < x0: x0 = x
                if y < y0: y0 = y
                if x > x1: x1 = x
                if y > y1: y1 = y
    return x0, y0, x1, y1

def diff_centroid(a, b, w, h, box):
    """Where two renders differ, weighted -- that is the feature that moved."""
    sx = sy = n = 0
    x0, y0, x1, y1 = box
    for y in range(max(0,y0), min(h,y1+1)):
        for x in range(max(0,x0), min(w,x1+1)):
            i = (y*w + x) * 4
            d = abs(a[i]-b[i]) + abs(a[i+1]-b[i+1]) + abs(a[i+2]-b[i+2])
            if d > 60:
                sx += x; sy += y; n += 1
    return (sx/n, sy/n, n) if n else (0, 0, 0)

BALD = dict(hairType=30)
w, h, base = fetch(**BALD)
box = skin_bbox(w, h, base)
fw, fh = box[2]-box[0], box[3]-box[1]
print(f"FFL face outline: x {box[0]}..{box[2]}  y {box[1]}..{box[3]}   ({fw} x {fh} px)")
print(f"  aspect (h/w) = {fh/fw:.3f}   mine = 32.06/25.88 = 1.239\n")

# Each feature, located by moving it and seeing what changed.
tests = [
    ("eyes",     dict(eyeYPosition=6),      dict(eyeYPosition=18)),
    ("eyebrows", dict(eyebrowYPosition=4),  dict(eyebrowYPosition=18)),
    ("nose",     dict(noseYPosition=2),     dict(noseYPosition=16)),
    ("mouth",    dict(mouthYPosition=4),    dict(mouthYPosition=18)),
]
print("  feature   FFL centre, as a fraction down the face   mine")
mine = {"eyes": 0.520, "eyebrows": 0.395, "nose": 0.630, "mouth": 0.765}
for name, lo, hi in tests:
    _, _, a = fetch(**{**BALD, **lo})
    _, _, b = fetch(**{**BALD, **hi})
    cx, cy, n = diff_centroid(a, b, w, h, box)
    if not n:
        print(f"  {name:9s} no difference detected"); continue
    frac = (cy - box[1]) / fh
    print(f"  {name:9s} {frac:.3f}  (from {n} changed px)"
          f"                    {mine[name]:.3f}   delta {frac-mine[name]:+.3f}")

# Eye spacing, across the face.
_, _, a = fetch(**BALD, eyeSpacing=0)
_, _, b = fetch(**BALD, eyeSpacing=12)
cx, cy, n = diff_centroid(a, b, w, h, box)
print(f"\n  eye spacing sweep moved {n} px; centre x {(cx-box[0])/fw:.3f} of the face width")

# The eye's horizontal offset: sweep the eye type and take the centroid of the
# left half's changes, which is one eye.
_, _, a = fetch(**BALD, eyeType=2)
_, _, b = fetch(**BALD, eyeType=11)
half = (box[0], box[1], box[0] + fw // 2, box[3])
cx, cy, n = diff_centroid(a, b, w, h, half)
if n:
    off = (box[0] + fw * 0.5 - cx) / fw
    print(f"  left eye sits {off:.3f} of the face width from the centre line"
          f"        mine 0.200   delta {0.200-off:+.3f}")

# And the brow, the same way.
_, _, a = fetch(**BALD, eyebrowType=0)
_, _, b = fetch(**BALD, eyebrowType=11)
cx, cy, n = diff_centroid(a, b, w, h, half)
if n:
    off = (box[0] + fw * 0.5 - cx) / fw
    print(f"  left brow sits {off:.3f} from the centre line"
          f"                       mine 0.200   delta {0.200-off:+.3f}")
