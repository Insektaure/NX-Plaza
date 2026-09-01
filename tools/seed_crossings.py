#!/usr/bin/env python3
"""Write a populated crossings.idx / crossings.dat pair for offline testing.

The console's collection is a fixed-size index plus an append-only text blob
(source/core/crossing_file.cpp). This writes both from scratch, packed, with no
dead records, which is exactly the shape compact() produces - so the app loads
it without wanting to rewrite anything.

Copy the two files it writes next to identity.json and profile.json on the SD
card. The passes are the same personas the network simulator uses, so a seeded
collection and a simulated plaza look like the same world.

Recipes
-------

A collection to look at, straight onto the card. Two hundred is plenty to
scroll, and the Square samples its twenty from them:

    python3 tools/seed_crossings.py --count 200 --out /run/media/me/SD/switch/nx-plaza

The same collection every time, which is what you want when comparing a screen
before and after a change:

    python3 tools/seed_crossings.py --count 200 --seed 42 --out out/nx-plaza

The storage limit, to see what 5000 cards does to load time and scrolling:

    python3 tools/seed_crossings.py --count 5000 --out out/nx-plaza

A plaza that looks unread - most cards still have their NEW badge:

    python3 tools/seed_crossings.py --count 60 --unread 0.9 --out out/nx-plaza

Everything crossed in the last day, so the cards read "3m ago" rather than
"2 weeks ago":

    python3 tools/seed_crossings.py --count 40 --days 1 --out out/nx-plaza

Just enough to fill the Square once, with nothing else in the way:

    python3 tools/seed_crossings.py --count 20 --out out/nx-plaza

Options
-------

    --count N     how many crossings to write            (default 200)
    --out DIR     where crossings.idx and .dat go        (default out/nx-plaza)
    --days N      spread last-seen over this many days   (default 30)
    --unread F    fraction left unopened, 0..1           (default 0.15)
    --seed N      fixed seed, for a reproducible file    (default random)

What is in the file
-------------------

Every run cycles through ten deliberate shapes, so the awkward ones are present
at any --count and the run prints the tally. They exist because the persona
list on its own produces only comfortable passes, and the states that break a
layout are the empty one and the full one:

    plain           nothing special
    no-greeting     the greeting card is skipped entirely
    no-carrying     the button reads "Trade something back"
    carrying-full   four chips, which wrap onto a second row
    no-title        "hours played" rather than "in <game>"
    zero-hours      the hours tile shows a dash
    no-mii          no face on the wire; the portrait seed stands in
    max-text        every field at its limit, to find what crops
    many-crossings  "met 14 times"
    games-full      the games tile at its maximum

Safety rails
------------

The format is mirrored from the C++ by hand, so both halves are checked against
source before anything is written:

  * _assert_layout() reads kRecordSize, kVersion, kHeaderSize and kMiiMaxBytes
    out of crossing_file.cpp, and confirms packRecord still writes its fields in
    the order this expects. A silently disagreeing offset would write a file the
    console drops on load with no useful error.
  * check_limits() refuses to write anything Pass::sanitize() would clamp. A
    greeting one character too long is truncated on the way in, and the file
    would no longer be what the screen shows.

Either one exits with a message naming the mismatch rather than writing.
"""

import argparse
import datetime
import json
import os
import random
import re
import secrets
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import simulate_consoles as sim  # personas, districts, and the Mii wire format

# ---------------------------------------------------------------- the format

MAGIC = b"NXPC"
VERSION = 1
HEADER_SIZE = 16
RECORD_SIZE = 128
MII_MAX_BYTES = 64

# Record layout, derived exactly as the C++ derives it.
OFF_FLAGS = 0
OFF_THEME = OFF_FLAGS + 1
OFF_RESERVED = OFF_THEME + 1
OFF_ID = OFF_RESERVED + 2
OFF_MII_LEN = OFF_ID + 16
OFF_MII = OFF_MII_LEN + 1
OFF_PORTRAIT = OFF_MII + MII_MAX_BYTES
OFF_FIRST_SEEN = OFF_PORTRAIT + 4
OFF_LAST_SEEN = OFF_FIRST_SEEN + 8
OFF_COUNT = OFF_LAST_SEEN + 8
OFF_BLOB_OFFSET = OFF_COUNT + 4
OFF_BLOB_LENGTH = OFF_BLOB_OFFSET + 4
OFF_HOURS = OFF_BLOB_LENGTH + 4
OFF_MET = OFF_HOURS + 4
RECORD_USED = OFF_MET + 4

FLAG_LIVE = 1 << 0
FLAG_OPENED = 1 << 1
FLAG_TRADED_BACK = 1 << 2

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "..", "source", "core", "crossing_file.cpp")
MODEL = os.path.join(HERE, "..", "source", "core", "model.cpp")


# The clamps live in the simulator, which reads them from model.cpp. One copy,
# so a seeded collection and a simulated plaza cannot disagree about the shape
# of a pass.
field_limits = sim.field_limits


def _assert_layout() -> None:
    """Fail loudly when the C++ has moved and this file has not.

    Reads the constants straight out of crossing_file.cpp. Cheap, and it turns
    a format drift into a message here instead of a collection the console
    quietly refuses to load.
    """
    try:
        with open(SOURCE, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        print("note: could not read crossing_file.cpp; layout not verified",
              file=sys.stderr)
        return

    def constant(pattern: str):
        found = re.search(pattern, text)
        return int(found.group(1)) if found else None

    checks = {
        "kRecordSize": (constant(r"constexpr uint16_t kRecordSize = (\d+);"), RECORD_SIZE),
        "kVersion": (constant(r"constexpr uint16_t kVersion = (\d+);"), VERSION),
        "kHeaderSize": (constant(r"constexpr size_t kHeaderSize = (\d+);"), HEADER_SIZE),
        "kMiiMaxBytes": (constant(r"constexpr size_t kMiiMaxBytes = (\d+);"), MII_MAX_BYTES),
    }
    for name, (found, mine) in checks.items():
        if found is not None and found != mine:
            sys.exit(f"{name} is {found} in crossing_file.cpp but {mine} here. "
                     "Update tools/seed_crossings.py before seeding anything.")

    if RECORD_USED > RECORD_SIZE:
        sys.exit(f"the record needs {RECORD_USED} bytes of {RECORD_SIZE}")

    # And the fields must still be written in this order.
    order = [m.group(1) for m in
             re.finditer(r"put(?:8|16|32|64)\(p, (?:uint8_t\()?c\.(?:pass\.)?(\w+)", text)]
    expected = ["theme", "portrait", "firstSeen", "lastSeen", "count", "hours", "met"]
    trimmed = [f for f in order if f in expected]
    if trimmed and trimmed != expected:
        sys.exit(f"packRecord writes {trimmed}, this expects {expected}")


# -------------------------------------------------------------------- pieces
#
# A crossing grants a collectible, and the console works out which one rather
# than being told: fnv1a("myId|theirId|day|set") % count, in
# source/core/pieces.cpp. A seeded collection never goes through that code -
# the records are written straight to the card - so the puzzles would sit empty
# on a console holding two hundred crossings. Redone here so a seeded card comes
# with the progress those crossings would have earned.
#
# The set table and the hash are read from the source where they can be, and
# mirrored where they cannot, for the same reason the record layout is: a copy
# that quietly disagrees is worse than no copy.

PIECES_SOURCE = os.path.join(HERE, "..", "source", "core", "pieces.cpp")


def piece_sets() -> list:
    """(name, picture, count) per puzzle, read out of pieces.cpp.

    The picture key is how a granted piece records which puzzle it belongs to,
    because the position in this table is not stable across builds.
    """
    try:
        with open(PIECES_SOURCE, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        print("note: could not read pieces.cpp; not granting any pieces", file=sys.stderr)
        return []
    body = re.search(r"kSets = \{(.*?)\};", text, re.S)
    if not body:
        return []
    entries = re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*(\d+)\s*\}',
                         body.group(1))
    return [(n, p, int(c)) for n, p, c in entries]


def fnv1a(text: str) -> int:
    """The 32-bit FNV-1a in source/core/util.cpp."""
    h = 2166136261
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def local_day_index(when: int) -> int:
    """Whole local days since the epoch, as pieces.cpp counts them."""
    day = datetime.datetime.fromtimestamp(when).replace(hour=0, minute=0, second=0,
                                                        microsecond=0)
    return int(day.timestamp()) // 86400


def piece_for(my_id: str, their_id: str, when: int, index: int, count: int) -> int:
    if count <= 0:
        return 0
    return fnv1a(f"{my_id}|{their_id}|{local_day_index(when)}|{index}") % count


def grant_pieces(out_dir: str, crossings: list) -> str:
    """Fills in the pieces these crossings would have earned.

    Needs identity.json for the console's own id and profile.json to write
    into; without either it does nothing and says so, because a seeded card in
    a scratch directory has neither.
    """
    sets = piece_sets()
    if not sets:
        return ""

    identity_path = os.path.join(out_dir, "identity.json")
    profile_path = os.path.join(out_dir, "profile.json")
    if not os.path.exists(identity_path) or not os.path.exists(profile_path):
        return ("no identity.json or profile.json here, so no pieces were granted "
                "(seed straight onto the card to get them)")

    with open(identity_path, "r", encoding="utf-8") as handle:
        my_id = json.load(handle).get("id", "")
    if len(my_id) != 32:
        return "identity.json has no usable id, so no pieces were granted"

    with open(profile_path, "r", encoding="utf-8") as handle:
        profile = json.load(handle)

    block = profile.get("pieces") or {}
    active = int(block.get("active", 0))
    if active < 0 or active >= len(sets):
        active = 0

    # Only the active puzzle, which is what the console does: a crossing fills
    # whichever one is being collected at the time.
    owned = [0] * len(sets)
    for i, mask in enumerate(block.get("owned", [])[:len(sets)]):
        try:
            owned[i] = int(mask, 16)
        except (TypeError, ValueError):
            owned[i] = 0

    def full(index: int) -> bool:
        return bin(owned[index]).count("1") >= sets[index][2]

    # Who brought each piece, as the console records it: keyed on the piece,
    # holding the handle rather than the id, because the collection prunes and
    # the name has to outlive the card.
    by_picture = {p: i for i, (_, p, _) in enumerate(sets)}
    sources = {}
    for entry in block.get("from") or []:
        if not isinstance(entry, dict):
            continue
        # Older cards keyed on the position in the table; migrate them the way
        # Store::load does, by reading the index once.
        picture = entry.get("picture") or ""
        if not picture:
            index = int(entry.get("set", -1))
            picture = sets[index][1] if 0 <= index < len(sets) else ""
        if picture in by_picture:
            sources[(picture, int(entry.get("piece", -1)))] = {
                "picture": picture,
                "piece": int(entry.get("piece", -1)),
                "who": entry.get("who", ""),
                "when": int(entry.get("when", 0)),
            }

    before = [bin(m).count("1") for m in owned]
    meetings = 0
    for crossing in crossings:
        # One per meeting, not one per person. A console grants a piece every
        # time it crosses somebody, so a card met five times over a fortnight
        # is worth up to five pieces - and two meetings on the same day are
        # worth one, because the day is part of what decides the piece. Using
        # only lastSeen made a seeded card lag well behind a real one.
        for when in crossing.get("meetings") or [crossing["lastSeen"]]:
            # The console moves on when a puzzle is finished rather than
            # throwing the rest away, so seeding has to as well - otherwise two
            # hundred crossings fill the first puzzle and vanish.
            if full(active):
                nxt = next((i for i in range(len(sets)) if not full(i)), active)
                active = nxt
            piece = piece_for(my_id, crossing["id"], when, active, sets[active][2])
            is_new = not (owned[active] >> piece) & 1
            owned[active] |= 1 << piece
            meetings += 1
            # Only a piece that was new records a source: a duplicate must not
            # overwrite the person who actually handed it over.
            if is_new:
                sources[(sets[active][1], piece)] = {
                    "picture": sets[active][1],
                    "piece": piece,
                    "who": (crossing.get("pass") or {}).get("handle", ""),
                    "when": when,
                }

    # Bits past the end of a puzzle cannot be held.
    for i, (_, _, count) in enumerate(sets):
        owned[i] &= (1 << count) - 1

    profile["pieces"] = {
        "active": active,
        "owned": ["%08x" % m for m in owned],
        # Drop anything that does not name a piece actually held, which is what
        # PieceInventory::normalise does on load.
        "from": [e for (pic, pc), e in sorted(sources.items())
                 if pic in by_picture and (owned[by_picture[pic]] >> pc) & 1],
    }
    with open(profile_path, "w", encoding="utf-8") as handle:
        json.dump(profile, handle, indent=2)

    parts = []
    for i, (name, _, count) in enumerate(sets):
        after = bin(owned[i]).count("1")
        if after == 0:
            continue
        gained = after - before[i]
        parts.append(f"{name} {after}/{count}" + (f" (+{gained})" if gained else ""))
    if not parts:
        parts.append("nothing granted")
    return ", ".join(parts) + f" from {meetings} meetings"


# ------------------------------------------------------------------- packing

def put_text(out: bytearray, text: str) -> None:
    raw = text.encode("utf-8")[:0xFFFF]
    out += struct.pack("<H", len(raw))
    out += raw


def put_list(out: bytearray, items: list) -> None:
    items = items[:0xFF]
    out.append(len(items))
    for item in items:
        put_text(out, item)


def pack_blob(pass_: dict, place: str) -> bytes:
    """The variable text, in the order unpackBlob reads it."""
    out = bytearray()
    put_text(out, pass_["handle"])
    put_text(out, pass_["greeting"])
    put_text(out, pass_["activity"])
    put_text(out, pass_["playing"])
    put_text(out, pass_["district"])
    put_text(out, place)
    put_list(out, pass_["carrying"])
    put_list(out, pass_["games"])
    return bytes(out)


def pack_record(crossing: dict, blob_offset: int, blob_length: int) -> bytes:
    rec = bytearray(RECORD_SIZE)
    pass_ = crossing["pass"]

    flags = FLAG_LIVE
    if crossing["opened"]:
        flags |= FLAG_OPENED
    if crossing["tradedBack"]:
        flags |= FLAG_TRADED_BACK
    rec[OFF_FLAGS] = flags
    rec[OFF_THEME] = pass_["theme"] & 0xFF

    rec[OFF_ID:OFF_ID + 16] = bytes.fromhex(crossing["id"])

    # Stored as the bytes the hex decodes to, not as hex.
    mii = bytes.fromhex(pass_["mii"])
    if len(mii) > MII_MAX_BYTES:
        raise ValueError(f"a {len(mii)}-byte Mii does not fit in {MII_MAX_BYTES}")
    rec[OFF_MII_LEN] = len(mii)
    rec[OFF_MII:OFF_MII + len(mii)] = mii

    struct.pack_into("<I", rec, OFF_PORTRAIT, pass_["portrait"] & 0xFFFFFFFF)
    struct.pack_into("<Q", rec, OFF_FIRST_SEEN, crossing["firstSeen"])
    struct.pack_into("<Q", rec, OFF_LAST_SEEN, crossing["lastSeen"])
    struct.pack_into("<I", rec, OFF_COUNT, crossing["count"])
    struct.pack_into("<I", rec, OFF_BLOB_OFFSET, blob_offset)
    struct.pack_into("<I", rec, OFF_BLOB_LENGTH, blob_length)
    struct.pack_into("<I", rec, OFF_HOURS, pass_["hours"])
    struct.pack_into("<I", rec, OFF_MET, pass_["met"])
    return bytes(rec)


# ------------------------------------------------------------------ contents

# Deliberate awkward cases, cycled through so even a small collection contains
# some of each. The point of seeding is to see the screens under load, and the
# states that break layouts are the empty one and the full one - neither of
# which the persona list produces on its own. Named, because the summary says
# which ones a file ended up with.
EDGE_CASES = [
    "plain",
    "no-greeting",      # the greeting card is skipped entirely
    "no-carrying",      # "Trade something back" instead of "Take it"
    "carrying-full",    # four chips, which wrap onto a second row
    "no-title",         # "hours played" instead of "in <game>"
    "zero-hours",       # the hours tile shows a dash
    "no-mii",           # no face on the wire; the portrait seed stands in
    "max-text",         # every field at its limit, to find what crops
    "many-crossings",   # "met 14 times"
    "games-full",       # the games tile at its maximum
]


def apply_edge(case: str, crossing: dict, limits: dict, rng: random.Random) -> None:
    """Bend one generated crossing into a shape worth looking at."""
    pass_ = crossing["pass"]

    if case == "no-greeting":
        pass_["greeting"] = ""
    elif case == "no-carrying":
        pass_["carrying"] = []
    elif case == "carrying-full":
        pool = ["Lantern shard", "Rope, forty metres", "Bay bream", "Puzzle piece 14 of 30",
                "Coin, worn smooth", "Tuning fork", "Purple radish seed", "Map fragment 3 of 9"]
        pass_["carrying"] = rng.sample(pool, limits["carrying"])
    elif case == "no-title":
        pass_["playing"] = ""
        pass_["hours"] = 0
    elif case == "zero-hours":
        pass_["hours"] = 0
    elif case == "no-mii":
        pass_["mii"] = ""
    elif case == "max-text":
        pass_["handle"] = "Maximilianeaux"[:limits["handle"]]
        pass_["greeting"] = ("Every last character of this greeting is here to "
                             "find the crop.")[:limits["greeting"]]
        pass_["playing"] = "A Very Long Game Title Indeed!!"[:limits["title"]]
        pass_["district"] = "Kita-Shinchi Crossing"[:limits["district"]]
        crossing["place"] = pass_["district"]
        pass_["carrying"] = ["A carried thing with a long name"[:limits["title"]]
                             ] * limits["carrying"]
    elif case == "many-crossings":
        crossing["count"] = rng.randrange(6, 40)
        crossing["firstSeen"] = crossing["lastSeen"] - rng.randrange(30, 400) * 86400
    elif case == "games-full":
        pass_["games"] = [g[:limits["title"]] for g in
                          ["Turnip Grove", "Skyward Rally", "Hollowreach", "Puzzle Cadence",
                           "Lantern Bay", "Darkest Dungeon", "Tetris", "Pokemon Violet"]
                          ][:limits["games"]]


def check_limits(crossings: list, limits: dict) -> list:
    """Anything the console would clamp on load. Should always come back empty."""
    problems = []
    for c in crossings:
        p = c["pass"]
        for key, limit in (("handle", limits["handle"]), ("greeting", limits["greeting"]),
                           ("activity", limits["activity"]), ("playing", limits["title"]),
                           ("district", limits["district"])):
            if len(p[key]) > limit:
                problems.append(f"{c['id'][:8]} {key} is {len(p[key])} of {limit}")
        if len(c["place"]) > limits["district"]:
            problems.append(f"{c['id'][:8]} place is {len(c['place'])} of {limits['district']}")
        for name, key in (("carrying", "carrying"), ("games", "games")):
            if len(p[name]) > limits[key]:
                problems.append(f"{c['id'][:8]} {name} has {len(p[name])} of {limits[key]}")
            for item in p[name]:
                if len(item) > limits["title"]:
                    problems.append(f"{c['id'][:8]} {name} item is {len(item)} of {limits['title']}")
        if p["mii"] and len(p["mii"]) != 78:
            problems.append(f"{c['id'][:8]} mii is {len(p['mii'])} hex chars, expected 78")
        if p["theme"] >= 6:
            problems.append(f"{c['id'][:8]} theme {p['theme']} is out of range")
    return problems


def make_crossings(count: int, days: int, unread: float, limits: dict,
                   rng: random.Random) -> tuple:
    now = int(time.time())
    out = []
    tally = {}
    for i in range(count):
        persona = sim.PERSONAS[i % len(sim.PERSONAS)]
        district = rng.choice(sim.DISTRICTS)
        console = sim.make_console(persona, district, reach=2)

        # make_console() now varies what a console carries and how many titles
        # it lists, so nothing more is needed here. It used to multiply the
        # games list to add variety, which quietly produced nine of them once
        # the simulator started sending up to three - caught by check_limits
        # rather than by anybody noticing a truncated tile on a console.
        pass_ = console["pass"]

        last = now - rng.randrange(60, max(120, days * 86400))
        times = rng.randrange(2, 6) if rng.random() < 0.3 else 1
        first = last - rng.randrange(1, 30) * 86400 if times > 1 else last

        # When each of those meetings happened. The file only keeps the first
        # and the last and a count, but a console granted a piece on every one
        # of them, so the pieces cannot be worked out from the record alone.
        # Held on the dict and never written; pack_record and pack_blob take
        # the fields they want by name.
        if times == 1:
            meetings = [last]
        else:
            middle = [rng.randrange(first, last) for _ in range(times - 2)]
            meetings = sorted([first] + middle + [last])

        crossing = {
            "id": console["id"],
            "pass": pass_,
            "place": district,
            "firstSeen": first,
            "lastSeen": last,
            "count": times,
            "meetings": meetings,
            "opened": rng.random() > unread,
            "tradedBack": rng.random() < 0.25,
        }

        # One in every len(EDGE_CASES), so the mix is the same at any size.
        case = EDGE_CASES[i % len(EDGE_CASES)]
        apply_edge(case, crossing, limits, rng)
        tally[case] = tally.get(case, 0) + 1
        out.append(crossing)

    # The console keeps the list newest first and so does compact().
    out.sort(key=lambda c: c["lastSeen"], reverse=True)
    return out, tally


def write_files(crossings: list, out_dir: str) -> tuple:
    blob = bytearray()
    records = bytearray()

    for crossing in crossings:
        block = pack_blob(crossing["pass"], crossing["place"])
        offset = len(blob)
        blob += block
        records += pack_record(crossing, offset, len(block))

    header = bytearray(HEADER_SIZE)
    header[0:4] = MAGIC
    struct.pack_into("<H", header, 4, VERSION)
    struct.pack_into("<H", header, 6, RECORD_SIZE)
    struct.pack_into("<I", header, 8, len(crossings))
    struct.pack_into("<I", header, 12, 0)

    os.makedirs(out_dir, exist_ok=True)
    idx = os.path.join(out_dir, "crossings.idx")
    dat = os.path.join(out_dir, "crossings.dat")
    with open(idx, "wb") as handle:
        handle.write(header)
        handle.write(records)
    with open(dat, "wb") as handle:
        handle.write(blob)
    return idx, dat, len(records), len(blob)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--count", type=int, default=200,
                        help="how many crossings to write (default 200)")
    parser.add_argument("--out", default="out/nx-plaza",
                        help="directory to write crossings.idx and crossings.dat into")
    parser.add_argument("--days", type=int, default=30,
                        help="spread last-seen times over this many days back")
    parser.add_argument("--unread", type=float, default=0.15,
                        help="fraction left unopened, 0..1 (default 0.15)")
    parser.add_argument("--seed", type=int, default=None,
                        help="fixed seed, for a collection you can reproduce")
    args = parser.parse_args()

    if args.count < 1:
        return parser.error("--count must be at least 1")
    if not 0.0 <= args.unread <= 1.0:
        return parser.error("--unread must be between 0 and 1")

    _assert_layout()

    rng = random.Random(args.seed if args.seed is not None else secrets.randbits(32))
    random.seed(args.seed if args.seed is not None else secrets.randbits(32))

    limits = field_limits()
    crossings, tally = make_crossings(args.count, args.days, args.unread, limits, rng)

    # Refuse to write a file the console would quietly clamp: what is seeded has
    # to be what the screens show, or a layout bug hides behind a truncation.
    problems = check_limits(crossings, limits)
    if problems:
        for line in problems[:10]:
            print("  " + line, file=sys.stderr)
        if len(problems) > 10:
            print(f"  ... and {len(problems) - 10} more", file=sys.stderr)
        sys.exit("generated data exceeds what Pass::sanitize() allows; nothing written")

    idx, dat, idx_bytes, dat_bytes = write_files(crossings, args.out)
    pieces = grant_pieces(args.out, crossings)

    unopened = sum(1 for c in crossings if not c["opened"])
    multi = sum(1 for c in crossings if c["count"] > 1)
    traded = sum(1 for c in crossings if c["tradedBack"])
    print(f"{len(crossings)} crossings: {unopened} unopened, {multi} crossed more than once, "
          f"{traded} already traded back")
    print("  coverage: " + ", ".join(f"{name} {n}" for name, n in sorted(tally.items())))
    if pieces:
        print("  pieces:   " + pieces)
    print(f"  {idx}  {idx_bytes + HEADER_SIZE:,} bytes")
    print(f"  {dat}  {dat_bytes:,} bytes")
    print("Copy both next to identity.json in sdmc:/switch/nx-plaza/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
