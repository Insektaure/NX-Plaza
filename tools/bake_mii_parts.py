#!/usr/bin/env python3
"""Bake Mii part artwork into the binary the console reads at startup.

The console draws quads. This turns each part's SVG -- filled paths and stroked
paths alike -- into a list of quads and discs, so that at runtime a face is a
few hundred quads through the pipeline that already exists, with no path solver,
no tessellator and no texture.

    python3 tools/bake_mii_parts.py <svg-dir> romfs/mii/parts.bin [tolerance]

Filled paths become horizontal trapezoids: within a band containing no vertex,
every edge crossing it is a straight line, so a trapezoid reproduces the polygon
exactly rather than approximating it. Even-odd pairing across the band handles
holes -- which is what the hair needs, every style being a silhouette with a
face-shaped hole in it -- without a hole-bridging step.

Stroked paths become one quad per segment plus a disc at each join, which is
what a round-capped stroke is. Overlap between them is invisible because a part
is drawn in one opaque colour.
"""

import os, re, struct, sys
from collections import defaultdict

MAGIC = b"MIIP"
VERSION = 2
UNIT = 16.0  # fixed-point: SVG units -> 1/16ths

# Colour slots. The artwork is drawn in an editor's icon palette -- greys and a
# flat blue -- so nothing here uses the SVG's own colours. Each fill is mapped to
# what it *means*, and the console supplies the actual colour from the Mii.
SLOT_SKIN, SLOT_SKIN_DARK, SLOT_HAIR, SLOT_HAIR_DARK, SLOT_EYE, SLOT_WHITE, \
    SLOT_DARK, SLOT_MOUTH, SLOT_MOUTH_DARK, SLOT_FRAME, SLOT_LENS, SLOT_HAT, \
    SLOT_HAT_DARK, SLOT_MAKEUP = range(14)

SLOT_NAMES = ["skin", "skin_dark", "hair", "hair_dark", "eye", "white", "dark",
              "mouth", "mouth_dark", "frame", "lens", "hat", "hat_dark", "makeup"]

DROP = object()  # a path that is scaffolding in the icon, not part of the Mii

# A filled path becomes bands with horizontal tops and bottoms; a stroked one
# becomes bands with two parallel slanted sides. The console antialiases each
# kind along the edges that are actually the silhouette, so it has to be told
# which is which.
KIND_TRAPEZOID, KIND_BAND = 0, 1

# Per category, what each fill in the source art stands for. `None` is the
# fallback for a colour not listed; DROP removes the path entirely.
PALETTE = {
    # The head silhouette. The grey is the icon's placeholder features, which the
    # console draws itself from the real eye and mouth parts.
    "face":     {"white": SLOT_SKIN, "#8d8d8d": DROP, "#6f6f6f": DROP, None: SLOT_SKIN},
    # White is the face showing through the hair, drawn under it anyway.
    "hair":     {"white": DROP, "#5f5f5f": SLOT_HAIR, "#404040": SLOT_HAIR_DARK,
                 "#808080": SLOT_HAIR_DARK, "#bfbfbf": SLOT_HAIR_DARK, None: SLOT_HAIR},
    "eyes":     {"white": SLOT_WHITE, "#6c7070": SLOT_EYE, "#0010bf": SLOT_DARK,
                 "#00ffff": SLOT_WHITE, None: SLOT_DARK},
    "eyebrows": {None: SLOT_HAIR},
    "nose":     {None: SLOT_SKIN_DARK},
    "mouth":    {"#712a04": SLOT_MOUTH_DARK, "#be4e26": SLOT_MOUTH,
                 "#ff5f5f": SLOT_MOUTH, "#0055ff": SLOT_MOUTH_DARK, None: SLOT_MOUTH},
    "glasses":  {"white": SLOT_LENS, None: SLOT_FRAME},
    # As in the hairstyles, the white shape is the placeholder head, not the
    # whiskers. Reading it as hair paints the entire face in hair colour.
    "mustache": {"white": DROP, "#1e1e1e": SLOT_HAIR, None: SLOT_HAIR},
    "goatee":   {"white": DROP, "#1e1e1e": SLOT_HAIR, None: SLOT_HAIR},
    # The hats colour themselves from CSS in the source, so the variable names
    # are the mapping: the console picks the actual colour.
    # Blush and eye shadow. The source draws them in one pink; the console
    # tints them off the skin so they read on every complexion.
    "makeup":   {None: SLOT_MAKEUP},
    # Drawn as thin strokes in a brown, which is a shade of the skin.
    "wrinkles": {None: SLOT_SKIN_DARK},
    "hat":      {"var(--icon-hat-fill)": SLOT_HAT,
                 "var(--icon-hat-stroke)": SLOT_HAT_DARK,
                 "var(--icon-face-fill)": DROP, "white": DROP,
                 "#808080": SLOT_HAT, "#404040": SLOT_HAT_DARK, None: SLOT_HAT},
}

CATEGORIES = ["face", "hair", "eyes", "eyebrows", "nose", "mouth", "glasses",
              "mustache", "goatee", "hat", "makeup", "wrinkles"]

# Artwork the app cannot use. hat-08's icon is a plain filled blob standing in
# for a 3D model, so on a 2D face it is a green tombstone rather than a hat.
EXCLUDE = {"hat-08.svg"}

def is_pale_grey(colour):
    """A neutral grey light enough to be the placeholder head's outline.

    The artwork does not use one consistent value -- #BFBFBF, #6F6F6F, #999999
    and #BFBEBD all appear -- so it is recognised by being grey and light rather
    than by being listed.
    """
    c = (colour or "").strip().lower()
    if not c.startswith("#") or len(c) not in (4, 7):
        return False
    if len(c) == 4:
        c = "#" + "".join(ch * 2 for ch in c[1:])
    try:
        r, g, b = (int(c[i:i + 2], 16) for i in (1, 3, 5))
    except ValueError:
        return False
    return max(r, g, b) - min(r, g, b) <= 12 and (r + g + b) / 3 >= 0.35 * 255

# Only these draw a placeholder head, so only these have anything behind it.
SPLIT_CATEGORIES = {"hair", "hat", "mustache", "goatee"}

# ------------------------------------------------------------------ svg path

TOKEN = re.compile(r'([MmLlHhVvCcSsQqTtAaZz])|([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)')


def tokenize(d):
    return [m.group(1) if m.group(1) else float(m.group(2)) for m in TOKEN.finditer(d)]


def flatten(d, steps=16):
    """Path data -> list of subpaths, each a list of points, plus whether it closed."""
    t = tokenize(d)
    i = 0
    cur = (0.0, 0.0)
    start = cur
    subs = []
    poly = []
    cmd = None
    prev_ctrl = None

    def emit(closed):
        nonlocal poly
        if len(poly) >= 2:
            subs.append((poly, closed))
        poly = []

    def bezier(p0, c1, c2, p3):
        for s in range(1, steps + 1):
            u = s / steps
            v = 1.0 - u
            poly.append((
                v*v*v*p0[0] + 3*v*v*u*c1[0] + 3*v*u*u*c2[0] + u*u*u*p3[0],
                v*v*v*p0[1] + 3*v*v*u*c1[1] + 3*v*u*u*c2[1] + u*u*u*p3[1]))

    while i < len(t):
        if isinstance(t[i], str):
            cmd = t[i]
            i += 1
            if cmd in "Zz":
                emit(True)
                cur = start
                continue
        if cmd is None:
            i += 1
            continue
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = cur if rel else (0.0, 0.0)
        if c == "M":
            emit(False)
            cur = (t[i] + ox, t[i+1] + oy); i += 2
            start = cur
            poly = [cur]
            cmd = "l" if rel else "L"
        elif c == "L":
            cur = (t[i] + ox, t[i+1] + oy); i += 2
            poly.append(cur)
        elif c == "H":
            cur = (t[i] + ox, cur[1]); i += 1
            poly.append(cur)
        elif c == "V":
            cur = (cur[0], t[i] + oy); i += 1
            poly.append(cur)
        elif c == "C":
            c1 = (t[i] + ox, t[i+1] + oy); c2 = (t[i+2] + ox, t[i+3] + oy)
            p3 = (t[i+4] + ox, t[i+5] + oy); i += 6
            bezier(cur, c1, c2, p3)
            prev_ctrl = c2; cur = p3
        elif c == "S":
            c2 = (t[i] + ox, t[i+1] + oy); p3 = (t[i+2] + ox, t[i+3] + oy); i += 4
            c1 = (2*cur[0] - prev_ctrl[0], 2*cur[1] - prev_ctrl[1]) if prev_ctrl else cur
            bezier(cur, c1, c2, p3)
            prev_ctrl = c2; cur = p3
        elif c == "Q":
            q = (t[i] + ox, t[i+1] + oy); p3 = (t[i+2] + ox, t[i+3] + oy); i += 4
            bezier(cur, (cur[0] + 2/3*(q[0]-cur[0]), cur[1] + 2/3*(q[1]-cur[1])),
                   (p3[0] + 2/3*(q[0]-p3[0]), p3[1] + 2/3*(q[1]-p3[1])), p3)
            prev_ctrl = q; cur = p3
        elif c == "T":
            p3 = (t[i] + ox, t[i+1] + oy); i += 2
            q = (2*cur[0] - prev_ctrl[0], 2*cur[1] - prev_ctrl[1]) if prev_ctrl else cur
            bezier(cur, (cur[0] + 2/3*(q[0]-cur[0]), cur[1] + 2/3*(q[1]-cur[1])),
                   (p3[0] + 2/3*(q[0]-p3[0]), p3[1] + 2/3*(q[1]-p3[1])), p3)
            prev_ctrl = q; cur = p3
        elif c == "A":
            p3 = (t[i+5] + ox, t[i+6] + oy); i += 7
            poly.append(p3); cur = p3   # arcs are rare here and short
        else:
            i += 1
    emit(False)
    return subs


# ------------------------------------------------------------- to primitives

def simplify(points, tol):
    """Douglas-Peucker. A part is never drawn much above 200px, so the last few
    tenths of an SVG unit are quads spent on detail nobody can see."""
    if len(points) < 3:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1:
            continue
        ax, ay = points[a]; bx, by = points[b]
        dx, dy = bx - ax, by - ay
        n = (dx*dx + dy*dy) ** 0.5
        worst, wi = -1.0, -1
        for k in range(a + 1, b):
            px, py = points[k]
            dist = abs(dx*(ay-py) - (ax-px)*dy) / n if n > 1e-9 else \
                ((px-ax)**2 + (py-ay)**2) ** 0.5
            if dist > worst:
                worst, wi = dist, k
        if worst > tol:
            keep[wi] = True
            stack += [(a, wi), (wi, b)]
    return [p for p, k in zip(points, keep) if k]


def trapezoids(subpaths, tol, even_odd=False):
    """Fill, as horizontal trapezoids.

    The winding rule matters more here than it looks. SVG fills with *nonzero*
    unless a path says otherwise, and several parts are drawn as a handful of
    overlapping subpaths -- an eyebrow as six overlapping tufts, for one. Filling
    those even-odd cancels every overlap and leaves a row of disconnected
    fragments where the eyebrow should be.
    """
    edges = []
    ys = set()
    for poly, _ in subpaths:
        pts = simplify(poly, tol)
        if len(pts) < 3:
            continue
        for k in range(len(pts)):
            a, b = pts[k], pts[(k + 1) % len(pts)]
            if abs(a[1] - b[1]) < 1e-9:
                continue
            edges.append((a, b, 1 if b[1] > a[1] else -1))
            ys.add(a[1]); ys.add(b[1])
    out = []
    ys = sorted(ys)
    for y0, y1 in zip(ys, ys[1:]):
        if y1 - y0 < 1e-7:
            continue
        mid = (y0 + y1) * 0.5
        active = []
        for a, b, wind in edges:
            lo, hi = (a, b) if a[1] < b[1] else (b, a)
            if lo[1] <= mid < hi[1]:
                def at(y):
                    return lo[0] + (y - lo[1]) * (hi[0] - lo[0]) / (hi[1] - lo[1])
                active.append((at(mid), at(y0), at(y1), wind))
        active.sort()
        winding = 0
        for k in range(len(active) - 1):
            winding += active[k][3]
            inside = (k % 2 == 0) if even_odd else (winding != 0)
            if not inside:
                continue
            _, l0, l1, _ = active[k]
            _, r0, r1, _ = active[k + 1]
            if abs(r0 - l0) < 1e-6 and abs(r1 - l1) < 1e-6:
                continue
            out.append((l0, y0, r0, y0, r1, y1, l1, y1))
    return out


def stroke(subpaths, width, tol):
    """Stroke, as a quad per segment and a disc per join: a round-capped line."""
    half = max(width, 0.1) * 0.5
    quads, discs = [], []
    for poly, closed in subpaths:
        pts = simplify(poly, tol)
        if len(pts) < 2:
            continue
        ring = pts + [pts[0]] if closed else pts
        for a, b in zip(ring, ring[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            n = (dx*dx + dy*dy) ** 0.5
            if n < 1e-9:
                continue
            nx, ny = -dy / n * half, dx / n * half
            quads.append((a[0]+nx, a[1]+ny, b[0]+nx, b[1]+ny,
                          b[0]-nx, b[1]-ny, a[0]-nx, a[1]-ny))
        for p in (ring if closed else ring[1:-1] or ring):
            discs.append((p[0], p[1], half))
        if not closed:
            discs.append((ring[0][0], ring[0][1], half))
            discs.append((ring[-1][0], ring[-1][1], half))
    return quads, discs


# ------------------------------------------------------------------- baking

PATH_RE = re.compile(r'<path[^>]*?/?>', re.S)
ATTR = lambda tag, name: (re.search(r'\s%s="([^"]*)"' % name, tag) or [None, None])[1]


def bake_part(path, category, tol):
    src = open(path).read()
    vb = re.search(r'viewBox="([^"]+)"', src)
    vx, vy, vw, vh = ([float(v) for v in vb.group(1).split()] if vb else (0, 0, 52, 52))
    table = PALETTE.get(category, {None: SLOT_DARK})
    # The art layers itself: the white placeholder head inside a hairstyle marks
    # the face plane. Whatever is drawn before it belongs behind the head, the
    # rest in front of it. That is the difference between hair that frames a face
    # and hair that covers one, and it is recorded here rather than guessed.
    back_quads, back_discs = [], []
    front_quads, front_discs = [], []
    quads, discs = back_quads, back_discs
    seen_face = False
    # The placeholder head this part was drawn against. Recorded before it is
    # dropped: it is the only thing that says which head the artist had in mind,
    # and the console needs it to put a beard on a chin that is not that head's.
    anchor = None
    for tag in PATH_RE.findall(src):
        d = ATTR(tag, "d")
        if not d:
            continue
        fill = (ATTR(tag, "fill") or "none").strip().lower()
        stroke_col = (ATTR(tag, "stroke") or "none").strip().lower()
        subs = flatten(d)
        if not subs:
            continue
        # A dropped fill drops the whole path, its outline included. The hair's
        # face-shaped hole is drawn in the source as a white placeholder head
        # with a pale stroke; keeping that stroke rings every hairstyle with a
        # necklace of beads.
        # The face shapes are the one category whose white outlined shape is the
        # head itself rather than a stand-in for it.
        if category != "face" and fill == "white" and is_pale_grey(stroke_col):
            xs = [p[0] for poly, _ in subs for p in poly]
            ys = [p[1] for poly, _ in subs for p in poly]
            if xs and anchor is None:
                anchor = (min(xs), min(ys), max(xs), max(ys))
            if category in SPLIT_CATEGORIES:
                seen_face = True
            continue

        # A gradient cannot be reproduced by a flat band, but dropping the path
        # loses the part -- several mouths are drawn entirely in gradients. Fall
        # back to the category's own colour.
        named = fill in table or fill.startswith("url(")
        fill_slot = table.get(fill, table.get(None)) if fill != "none" else None
        if fill_slot is DROP:
            if category in SPLIT_CATEGORIES:
                seen_face = True
            continue
        if not seen_face:
            quads, discs = back_quads, back_discs
        else:
            quads, discs = front_quads, front_discs
        if fill != "none" and (named or not fill.startswith(("url(", "var("))):
            if fill_slot is not None:
                rule = (ATTR(tag, "fill-rule") or "nonzero").strip().lower()
                for q in trapezoids(subs, tol, even_odd=(rule == "evenodd")):
                    quads.append((q, fill_slot, KIND_TRAPEZOID))
        stroke_named = stroke_col in table
        if stroke_col != "none" and (
                stroke_named or not stroke_col.startswith(("url(", "var("))):
            slot = table.get(stroke_col, table.get(None))
            if slot is not DROP and slot is not None:
                w = float(ATTR(tag, "stroke-width") or 1.0)
                sq, sd = stroke(subs, w, tol)
                quads += [(q, slot, KIND_BAND) for q in sq]
                discs += [(c, slot) for c in sd]
    # Only artwork that actually contains the placeholder head is split. In
    # everything else -- an eye, a nose -- there is no face plane to be behind,
    # so all of it is in front.
    if not seen_face:
        return (vx, vy, vw, vh), [], [], back_quads, back_discs, anchor
    return (vx, vy, vw, vh), back_quads, back_discs, front_quads, front_discs, anchor


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src_dir, out_path = sys.argv[1], sys.argv[2]
    # Simplification tolerance, in the artwork's own units (52 to a head). At
    # 0.22 a part drawn at 460px -- larger than anywhere in the app but the
    # passport -- has no visible faceting, and it costs a fifth fewer quads than
    # tessellating finer.
    tol = float(sys.argv[3]) if len(sys.argv) > 3 else 0.22

    found = defaultdict(list)
    for fn in sorted(os.listdir(src_dir)):
        m = re.match(r'([a-z]+)-(\d+)\.svg$', fn)
        if m and m.group(1) in CATEGORIES and fn not in EXCLUDE:
            found[m.group(1)].append((int(m.group(2)), fn))

    parts, blob = [], bytearray()
    for cat_id, cat in enumerate(CATEGORIES):
        for idx, (_, fn) in enumerate(sorted(found.get(cat, []))):
            (vx, vy, vw, vh), bq, bd, fq, fd, anchor = bake_part(
                os.path.join(src_dir, fn), cat, tol)
            quads = bq + fq
            discs = bd + fd

            def fx(v, o, s):
                return max(-32768, min(32767, int(round((v - o) / s * 52.0 * UNIT))))

            xs = [q[0][k] for q in quads for k in range(0, 8, 2)] + \
                 [c[0][0] - c[0][2] for c in discs] + [c[0][0] + c[0][2] for c in discs]
            ys = [q[0][k] for q in quads for k in range(1, 8, 2)] + \
                 [c[0][1] - c[0][2] for c in discs] + [c[0][1] + c[0][2] for c in discs]
            bbox = (min(xs), min(ys), max(xs), max(ys)) if xs else (0, 0, 0, 0)

            off = len(blob)
            for q, slot, kind in quads:
                for k in range(0, 8, 2):
                    blob += struct.pack("<hh", fx(q[k], vx, vw), fx(q[k+1], vy, vh))
                blob += struct.pack("<BB", slot, kind)
            for (cx, cy, rr), slot in discs:
                blob += struct.pack("<hhh", fx(cx, vx, vw), fx(cy, vy, vh),
                                    fx(rr, 0, vw))
                blob += struct.pack("<BB", slot, 0)
            # A face shape anchors on itself; everything else on the placeholder
            # head it was drawn against, so the console can move it to whichever
            # head it is actually going on.
            anchor_box = anchor if anchor else (bbox if cat == "face" else (0, 0, 0, 0))
            parts.append((cat_id, idx, off, len(quads), len(discs), len(bq), len(bd),
                          tuple(fx(v, vx if k % 2 == 0 else vy,
                                   vw if k % 2 == 0 else vh)
                                for k, v in enumerate(anchor_box)),
                          tuple(fx(v, vx if k % 2 == 0 else vy,
                                   vw if k % 2 == 0 else vh)
                                for k, v in enumerate(bbox))))

    header = MAGIC + struct.pack("<HH", VERSION, len(parts))
    table = b"".join(struct.pack("<BBHIHHHH4h4h", c, i, 0, o, q, d, bq, bd, *an, *bb)
                     for c, i, o, q, d, bq, bd, an, bb in parts)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(header + struct.pack("<I", len(table)) + table + bytes(blob))

    per = defaultdict(int)
    for row in parts:
        per[CATEGORIES[row[0]]] += 1
    total_q = sum(p[3] for p in parts)
    total_d = sum(p[4] for p in parts)
    print(f"{len(parts)} parts -> {out_path}  "
          f"({os.path.getsize(out_path)/1024:.0f} KB, {total_q} quads, {total_d} discs)")
    for cat in CATEGORIES:
        rows = [p for p in parts if CATEGORIES[p[0]] == cat]
        if rows:
            avg = sum(p[3] + p[4] for p in rows) / len(rows)
            worst = max(p[3] + p[4] for p in rows)
            wid = sorted((p[8][2] - p[8][0]) / UNIT for p in rows)
            hei = sorted((p[8][3] - p[8][1]) / UNIT for p in rows)
            behind = sum(1 for p in rows if p[5] or p[6])
            anchored = sum(1 for p in rows if p[7][2] > p[7][0])
            print(f"  {cat:9s} {len(rows):4d} parts   avg {avg:5.1f} prims   "
                  f"worst {worst:4d}   median box {wid[len(wid)//2]:5.1f}"
                  f" x {hei[len(hei)//2]:5.1f}"
                  + (f"   {behind} behind the head" if behind else "")
                  + (f"   {anchored} anchored" if anchored else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
