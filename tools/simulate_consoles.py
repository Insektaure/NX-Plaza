#!/usr/bin/env python3
"""
Fake consoles for testing Crossings with only one Switch.

Spawns a roster of virtual consoles that talk to the plaza server exactly like
the homebrew does: they say hello, check in, and exchange passes. Once they are
running, your real Switch sees them on the Nearby radar and collects their
passes, and they collect yours -- so you can also read back what your own pass
looks like from the other side.

The only thing that has to line up is the bucket. Pick one:

    --ssid "YourWifiName"   the sims hash it exactly like the console does, so
                            they land in the same place bucket. Best fidelity.
    --place <16 hex>        paste a token you already know.
    --world                 no bucket; set the console to Settings > How far a
                            crossing reaches > Anywhere.

Typical session:

    python3 server/plaza_server.py --port 8080 &
    python3 tools/simulate_consoles.py spawn --count 6 --ssid "Home WiFi"
    python3 tools/simulate_consoles.py live

Stdlib only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import re
import secrets
import sys
import time
import urllib.error
import urllib.request

# Must match kPlaceSalt in source/core/place.cpp.
PLACE_SALT = "nx-plaza/place/v1|"

DEFAULT_STATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sim-consoles.json")
DEFAULT_SERVER = "http://127.0.0.1:8080"

# Personas in the voice of the mockups: invented games, small tradeable things,
# a greeting with an opinion in it.
PERSONAS = [
    ("Mika", "Turnip Grove", "I will trade almost anything for a purple radish. Almost.",
     ["Purple radish seed", "Card theme: Dusk Market"], "trading radishes"),
    ("Rowan", "Skyward Rally", "Beat my Dune Pass time and I will retire.",
     ["Rally ghost - Dune Pass 1:42"], "chasing a ghost"),
    ("Kestrel", "Hollowreach", "Do not go down the west stairs. Trust me.",
     ["Lantern shard", "Map fragment 3 of 9"], "mapping the deep"),
    ("Bo", "Puzzle Cadence", "Puzzle piece 14 of 30. Only 16 to go, forever.",
     ["Puzzle piece 14 of 30"], "collecting pieces"),
    ("Ines", "Lantern Bay", "Fishing tournament starts at six. Bring bread.",
     ["Bay bream", "Paper lantern"], "at the tournament"),
    ("Juno", "Skyward Rally", "I only play the night tracks.",
     ["Night decal", "Rally ghost - Aster Loop 2:08"], "on the night circuit"),
    ("Devi", "Hollowreach", "Ask me about the bell. Do not ring the bell.",
     ["Bell rope", "Hollow key"], "ringing nothing"),
    ("Sora", "Turnip Grove", "My town is called Pancake. I regret nothing.",
     ["Golden watering can", "Pancake town flag"], "watering things"),
    ("Nabi", "Puzzle Cadence", "Speedrunning the tutorial. It is a whole thing.",
     ["Tutorial medal"], "speedrunning badly"),
    ("Oleander", "Lantern Bay", "I have named every cat on the pier.",
     ["Cat census, page 4"], "counting cats"),
    ("Wren", "Turnip Grove", "Selling tulips at a criminal markup.",
     ["Tulip bundle", "Market stall permit"], "running a stall"),
    ("Tycho", "Hollowreach", "Two hundred hours and I have not finished chapter one.",
     ["Unread letter"], "stalling forever"),
    ("Pim", "Skyward Rally", "Reverse laps only. It is more honest.",
     ["Reverse-lap trophy"], "driving backwards"),
    ("Lull", "Lantern Bay", "The 14:20 express has the best window seat. Left side.",
     ["Train timetable", "Window seat, left side"], "on the express"),
]

# The fourteen above are the voice of the thing, and a small spawn gets them
# first. Past that the roster is built rather than written out: a thousand
# hand-typed personas would be a thousand lines nobody reads, and what
# `spawn --count 900` actually needs is a thousand consoles that are distinct,
# plausible, and stable between runs.
#
# Each generated console belongs to one of the worlds below, so its greeting,
# the thing it is carrying and what it says it is playing all agree with each
# other. A crowd of nine hundred should still read as people rather than as rows.

def field_limits() -> dict:
    """The clamps Pass::sanitize() applies, read from source/core/model.cpp.

    Read rather than copied: the console applies these on the way in, so a pass
    that exceeds one is silently truncated and the simulation stops matching
    what the screens show. Also used by tools/seed_crossings.py, so the two
    tools cannot disagree about the shape of a pass.
    """
    fallback = {"handle": 16, "greeting": 60, "activity": 28, "title": 32,
                "district": 24, "carrying": 4, "games": 8}
    model = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "source", "core", "model.cpp")
    try:
        with open(model, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError:
        print("note: could not read model.cpp; using built-in limits", file=sys.stderr)
        return fallback

    out = {}
    for key, name in (("handle", "kMaxHandle"), ("greeting", "kMaxGreeting"),
                      ("activity", "kMaxActivity"), ("title", "kMaxTitle"),
                      ("district", "kMaxDistrict"), ("carrying", "kMaxCarrying"),
                      ("games", "kMaxGames")):
        found = re.search(r"constexpr size_t %s = (\d+);" % name, text)
        out[key] = int(found.group(1)) if found else fallback[key]
    return out


LIMITS = field_limits()
MAX_HANDLE = LIMITS["handle"]  # the console clamps a handle to this


def check_pass(pass_: dict) -> list:
    """Anything the console would clamp on load. Empty when the pass is clean."""
    problems = []
    for key, limit in (("handle", LIMITS["handle"]), ("greeting", LIMITS["greeting"]),
                       ("activity", LIMITS["activity"]), ("playing", LIMITS["title"]),
                       ("district", LIMITS["district"])):
        if len(pass_.get(key, "")) > limit:
            problems.append(f"{key} is {len(pass_[key])} of {limit}")
    for name, key in (("carrying", "carrying"), ("games", "games")):
        items = pass_.get(name, [])
        if len(items) > LIMITS[key]:
            problems.append(f"{name} has {len(items)} of {LIMITS[key]}")
        for item in items:
            if len(item) > LIMITS["title"]:
                problems.append(f"{name} item is {len(item)} of {LIMITS['title']}")
    mii = pass_.get("mii", "")
    if mii and len(mii) != 78:
        problems.append(f"mii is {len(mii)} hex chars, expected 78")
    if pass_.get("theme", 0) >= 6:
        problems.append(f"theme {pass_['theme']} is out of range")
    return problems

_GIVEN_NAMES = [
    "Aki", "Alba", "Ander", "Anouk", "Arlo", "Asa", "Aster", "Aubry", "Avel", "Azra",
    "Beca", "Bela", "Bern", "Birdie", "Bly", "Bodhi", "Brann", "Bri", "Bruno", "Cade",
    "Cai", "Calla", "Caro", "Cass", "Cedar", "Celia", "Cleo", "Cove", "Cyrus", "Dara",
    "Dashi", "Delia", "Dev", "Dima", "Doran", "Dov", "Drea", "Eda", "Eero", "Efa",
    "Eiko", "Elian", "Elke", "Ellis", "Elsie", "Emeri", "Enzo", "Esme", "Etta", "Ewan",
    "Faye", "Fen", "Fia", "Finn", "Flor", "Fran", "Freya", "Gable", "Gaia", "Gemma",
    "Gil", "Ginny", "Gray", "Greta", "Hana", "Haru", "Hazel", "Heli", "Hollis", "Idris",
    "Ilse", "Imani", "Indra", "Ira", "Isla", "Ivo", "Jae", "Jarl", "Jem", "Joss",
    "Juna", "Kai", "Kaja", "Kanto", "Kara", "Keiko", "Kenji", "Kira", "Kito", "Kova",
    "Lark", "Lasse", "Leif", "Lena", "Lior", "Liv", "Lore", "Lucia", "Luka", "Mabel",
    "Maeve", "Mako", "Malin", "Mara", "Mars", "Maya", "Meru", "Milo", "Mira", "Miro",
    "Mona", "Nadia", "Nao", "Neve", "Niko", "Nina", "Noor", "Nori", "Nova", "Odile",
    "Oona", "Orin", "Osa", "Otto", "Paz", "Pell", "Petra", "Pia", "Pim", "Quill",
    "Rae", "Rami", "Ravi", "Reva", "Rhys", "Rina", "Rio", "Rosa", "Runa", "Sable",
    "Sacha", "Saga", "Sami", "Sana", "Sena", "Shai", "Shiro", "Sika", "Sol", "Sora",
    "Stig", "Suki", "Sven", "Tama", "Tamar", "Tao", "Tarn", "Tegan", "Thea", "Tilda",
    "Tomo", "Tove", "Tuva", "Ula", "Umi", "Vada", "Vale", "Vera", "Vida", "Vik",
    "Wren", "Xan", "Yara", "Yuki", "Zaid", "Zara", "Zev", "Zia", "Zola", "Zuri",
]

# (game, things they carry, what they are doing, things they say)
_WORLDS = [
    ("Turnip Grove",
     ["Purple radish seed", "Turnip crate", "Bell jar of soil", "Watering can, dented",
      "Blue tulip cutting", "Almanac, page 12 torn"],
     ["trading radishes", "weeding", "at the market", "planting on a slope"],
     ["The radish market moves at eleven. Do not be late.",
      "My turnips went bad on a Sunday. Learn from me.",
      "I will swap a blue tulip for almost nothing.",
      "Everything here is soil and patience.",
      "Ask me about the west field. Do not ask about the east."]),
    ("Skyward Rally",
     ["Rally ghost - Dune Pass 1:42", "Night decal", "Spare gearbox",
      "Rally ghost - Aster Loop 2:08", "Tyre set, worn", "Pit radio"],
     ["chasing a ghost", "on the night circuit", "in the pits", "running Dune Pass"],
     ["Beat my Dune Pass time and I will retire.",
      "I only play the night tracks.",
      "Reverse laps only. It is more honest.",
      "Manual gearbox or nothing at all.",
      "Braking is a suggestion on the Aster Loop."]),
    ("Hollowreach",
     ["Lantern shard", "Map fragment 3 of 9", "Rope, forty metres", "Cave moss",
      "Bell rubbing", "Iron key, unlabelled"],
     ["mapping the deep", "descending", "on level nine", "resting at the shrine"],
     ["Do not go down the west stairs. Trust me.",
      "Ask me about the bell. Do not ring the bell.",
      "Level nine is a lie. There is no level nine.",
      "I have mapped four of the nine. Slowly.",
      "Bring rope. Bring more rope than that."]),
    ("Puzzle Cadence",
     ["Puzzle piece 14 of 30", "Corner piece, gold", "Metronome", "Solved edge, framed",
      "Piece 27, chipped", "Timing chart"],
     ["collecting pieces", "on the daily", "stuck on the blue one", "timing myself"],
     ["Puzzle piece 14 of 30. Only 16 to go, forever.",
      "The daily puzzle beat me. I will not discuss it.",
      "I solve to the metronome. It helps. It does.",
      "Trading corners for edges, always.",
      "Sixteen pieces left and none of them fit."]),
    ("Lantern Bay",
     ["Bay bream", "Paper lantern", "Fishing licence, expired", "Bread, for the fish",
      "Tide table", "Net, mended twice"],
     ["at the tournament", "on the pier", "waiting on the tide", "gutting a bream"],
     ["Fishing tournament starts at six. Bring bread.",
      "The bream are only honest at dawn.",
      "My licence expired. The fish do not check.",
      "I light one lantern a night. That is the whole game.",
      "Low tide is the only tide worth having."]),
    ("Aster Loop",
     ["Loop token", "Star chart, annotated", "Comet decal", "Orbit log",
      "Fuel cell, half", "Beacon, blinking"],
     ["running the loop", "charting", "in slow orbit", "waiting on a comet"],
     ["The loop takes eight minutes. I have run it nine hundred.",
      "I chart comets nobody asked me to chart.",
      "Slow orbit is the correct orbit.",
      "Trade me a beacon and I will owe you.",
      "There is a comet due. There is always a comet due."]),
    ("Dusk Market",
     ["Card theme: Dusk Market", "Copper coin, bent", "Spice tin", "Ledger, half full",
      "Lamp oil", "Receipt for nothing"],
     ["haggling", "closing the stall", "counting coppers", "restocking"],
     ["Everything is negotiable except the spice tin.",
      "I closed the stall early. I regret it.",
      "A bent copper still spends. Mostly.",
      "My ledger says I am winning. My ledger lies.",
      "Come at dusk or do not come."]),
    ("Paper Harbour",
     ["Folded crane", "Harbour stamp", "Ink block", "Letter, unsent",
      "Boat, paper, seaworthy", "Wax seal"],
     ["folding", "stamping letters", "at the harbour", "writing to nobody"],
     ["I fold one crane a day. It is a slow game.",
      "The harbour stamp is worth more than the letter.",
      "My paper boat outlasted a real one.",
      "I write letters I do not send. That is the mechanic.",
      "Ink first, then fold. Never the other way."]),
    ("Kettle Peak",
     ["Summit pin", "Thermos, dented", "Rope glove", "Trail mix, sorted",
      "Weather log", "Boot lace, spare"],
     ["climbing", "at base camp", "reading the weather", "descending slowly"],
     ["The summit pin is not worth it. I have four.",
      "I sort my trail mix. Do not judge me.",
      "Weather turns at three. It always turns at three.",
      "Base camp is the best part of the mountain.",
      "Going down is the hard half. Nobody says so."]),
    ("Signal Garden",
     ["Antenna coil", "Recorded hum", "Frequency card", "Moth, brass",
      "Dish, miniature", "Static sample"],
     ["listening", "tuning", "recording the hum", "in the garden"],
     ["I record the hum. Somebody has to.",
      "There is a signal at 42. I will not explain.",
      "Tuning is the whole game. The rest is decoration.",
      "My brass moth found a frequency. I am not joking.",
      "Listen for a while before you say it is noise."]),
    ("Tidefall",
     ["Salt jar", "Shell, spiral", "Drift wood, carved", "Wave log",
      "Glass float", "Rope knot, unnamed"],
     ["walking the shore", "carving drift", "logging waves", "after the tide"],
     ["The tide takes what it likes. I log it anyway.",
      "I carve driftwood badly and proudly.",
      "A glass float is the best thing the sea gives up.",
      "Every knot I tie is a knot I invented.",
      "Come after the tide, not before."]),
    ("Ember Line",
     ["Ticket stub, 14:20", "Coal token", "Timetable, marked", "Signal flag",
      "Carriage number", "Lamp glass"],
     ["riding the line", "on the platform", "reading timetables", "between stops"],
     ["The 14:20 is never the 14:20.",
      "I ride the whole line and get off where I started.",
      "Timetables are fiction and I read them anyway.",
      "Trade me a coal token, I collect the ugly ones.",
      "The last carriage is the only carriage."]),
]


def _build_personas(target: int = 1000) -> list:
    """The curated personas, then as many generated ones as it takes.

    Deterministic: its own Random instance, seeded fixed, so console 700 is the
    same person on every run and a roster stays recognisable between spawns.
    """
    out = list(PERSONAS)
    taken = {p[0] for p in out}
    rng = random.Random(20260829)

    # Plain given names first, so the early ones read best; then the same names
    # with an initial, which is how a room full of people actually disambiguates.
    handles = list(_GIVEN_NAMES)
    for letter in "ABCDEFGHIJKLMNOPRSTUVWYZ":
        handles += [f"{name} {letter}." for name in _GIVEN_NAMES]

    for handle in handles:
        if len(out) >= target:
            break
        if handle in taken or len(handle) > MAX_HANDLE:
            continue
        game, carrying, doing, saying = _WORLDS[len(out) % len(_WORLDS)]
        out.append((
            handle,
            game,
            rng.choice(saying),
            rng.sample(carrying, rng.randint(1, 2)),
            rng.choice(doing),
        ))
        taken.add(handle)
    return out


#: How many of the personas above were written by hand rather than generated.
CURATED = len(PERSONAS)

PERSONAS = _build_personas()

DISTRICTS = [
    "Namba Station", "Shinsaibashi", "Amerika-mura", "the 14:20 express",
    "a bookshop", "the pier", "Tenma market", "the long platform",
]


# ------------------------------------------------------------------ plumbing


def place_token(ssid: str) -> str:
    """Exactly what the console computes, so the buckets match."""
    digest = hashlib.sha256((PLACE_SALT + ssid).encode()).digest()
    return digest[:8].hex()


def post(server: str, path: str, device_id: str, token: str, body: dict,
         timeout: float = 10.0) -> dict:
    data = json.dumps(body).encode()
    request = urllib.request.Request(
        server.rstrip("/") + path,
        data=data,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "X-Plaza-Id": device_id,
            "Authorization": f"Bearer {token}",
            "User-Agent": "nx-plaza-sim/1.0",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        payload = error.read()
        try:
            parsed = json.loads(payload or b"{}")
        except json.JSONDecodeError:
            parsed = {"error": payload.decode("utf-8", "replace")[:200]}
        parsed.setdefault("error", f"HTTP {error.code}")
        parsed["ok"] = False
        parsed["status"] = error.code
        return parsed
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        return {"ok": False, "error": f"{type(error).__name__}: {error}"}


# --------------------------------------------------------------------- state


class Roster:
    def __init__(self, path: str):
        self.path = path
        self.server = DEFAULT_SERVER
        self.place = ""
        self.ssid_hint = ""
        self.consoles: list[dict] = []

    @classmethod
    def load(cls, path: str) -> "Roster":
        roster = cls(path)
        if os.path.exists(path):
            with open(path, encoding="utf-8") as handle:
                blob = json.load(handle)
            roster.server = blob.get("server", DEFAULT_SERVER)
            roster.place = blob.get("place", "")
            roster.ssid_hint = blob.get("ssid_hint", "")
            roster.consoles = blob.get("consoles", [])
        return roster

    def save(self) -> None:
        with open(self.path, "w", encoding="utf-8") as handle:
            json.dump(
                {
                    "note": "Fake consoles for testing nx-plaza. Safe to delete; "
                            "run `forget` first to clear them off the server.",
                    "server": self.server,
                    "place": self.place,
                    "ssid_hint": self.ssid_hint,
                    "consoles": self.consoles,
                },
                handle,
                indent=2,
            )

    def ids(self) -> set[str]:
        return {console["id"] for console in self.consoles}

    def by_name(self, name: str) -> list[dict]:
        needle = name.lower()
        return [c for c in self.consoles if needle in c["pass"]["handle"].lower()]


# The Mii wire format, mirroring source/core/mii.h. Twenty bytes: a version and
# nineteen small part indices. Keep this table in step with that header -- the
# host test in tools/ checks the two agree.
MII_VERSION = 8  # matches Mii::kVersion in source/core/mii.h
# The real Mii catalogue, matching MiiPartCounts in source/core/mii.h and the
# artwork baked into romfs/mii/parts.bin. Keep the two in step: the console
# clamps anything out of range, so a mismatch here shows up as simulated
# consoles whose faces are all drawn from the first few styles.
MII_PARTS = [
    ("faceShape", 12), ("skinTone", 6), ("build", 128), ("height", 128),
    ("favouriteColour", 12),
    ("hairStyle", 132), ("hairColour", 8),
    ("eyeStyle", 60), ("eyeColour", 6),
    ("browStyle", 24), ("browColour", 8),
    ("noseStyle", 18), ("mouthStyle", 36), ("mouthColour", 5),
    ("glasses", 20), ("glassesColour", 6),
    ("mustache", 6), ("beard", 6), ("facialHairColour", 8),
    ("wrinkles", 12), ("makeup", 12),
    ("headwear", 9), ("flags", 2),
]
#: The placement steps, in the order the console packs them: five bits each,
#: biased by 16, streamed across byte boundaries. Order matters -- it is the
#: wire layout.
MII_STEPS = [
    "eyeSpacing", "eyeHeight", "eyeScale", "eyeScaleY", "eyeRotate", "browSpacing",
    "browHeight", "browScale", "browScaleY", "browRotate", "noseHeight", "noseScale",
    "mouthHeight", "mouthScale", "mouthScaleY", "mustacheHeight", "mustacheScale",
    "beardHeight", "beardScale", "glassesHeight", "glassesScale",
    "moleX", "moleY", "moleScale",
]
MII_STEP_RANGE = 12
MII_STEP_BITS = 5
MII_STEP_BIAS = 16

#: version byte + one per part + the packed step bytes.
MII_STEP_BYTES = (len(MII_STEPS) * MII_STEP_BITS + 7) // 8
MII_BYTES = 1 + len(MII_PARTS) + MII_STEP_BYTES


def pack_steps(steps: list[int]) -> bytes:
    """The console's packSteps, in source/core/mii.cpp."""
    acc = bits = 0
    out = bytearray()
    for value in steps:
        acc = (acc << MII_STEP_BITS) | ((value + MII_STEP_BIAS) & 0x1F)
        bits += MII_STEP_BITS
        while bits >= 8:
            bits -= 8
            out.append((acc >> bits) & 0xFF)
    if bits:
        out.append((acc << (8 - bits)) & 0xFF)
    return bytes(out)


def unpack_steps(raw: bytes) -> list[int]:
    """The console's unpackSteps."""
    acc = bits = at = 0
    out = []
    for _ in range(len(MII_STEPS)):
        while bits < MII_STEP_BITS:
            acc = (acc << 8) | raw[at]
            at += 1
            bits += 8
        bits -= MII_STEP_BITS
        out.append(((acc >> bits) & 0x1F) - MII_STEP_BIAS)
    return out

SKIN_NAMES = ["fair", "light", "tan", "olive", "brown", "deep"]
HAIR_NAMES = ["black", "dark brown", "brown", "light brown", "blond", "red", "grey", "white"]
EYE_NAMES = ["brown", "blue", "green", "violet", "grey", "dark"]


def random_mii() -> str:
    """A face in the console's own format, weighted the way the console is.

    The naive version of this -- uniform random bytes -- produced a circus:
    `glasses` is one of twenty options with 0 meaning "none", so nineteen sims in
    twenty wore glasses. source/core/mii.cpp rolls the accessories separately for
    exactly that reason, and these are its odds.
    """
    parts = {name: secrets.randbelow(count) for name, count in MII_PARTS}

    def accessory(count: int, percent: int) -> int:
        return 1 + secrets.randbelow(count - 1) if secrets.randbelow(100) < percent else 0

    counts = dict(MII_PARTS)
    # Build and height are a full byte each; the extremes are a stick and a
    # barrel, so the sims stay in the middle band the way the console does.
    parts["build"] = 40 + secrets.randbelow(48)
    parts["height"] = 40 + secrets.randbelow(48)
    parts["glasses"] = accessory(counts["glasses"], 28)
    # A moustache and a beard are rolled apart, so both can turn up at once.
    parts["mustache"] = accessory(counts["mustache"], 14)
    parts["beard"] = accessory(counts["beard"], 11)
    parts["wrinkles"] = accessory(counts["wrinkles"], 12)
    parts["makeup"] = accessory(counts["makeup"], 16)
    parts["headwear"] = accessory(counts["headwear"], 14)
    # Brows usually match the hair, the way the console rolls them.
    if secrets.randbelow(100) < 80:
        parts["browColour"] = parts["hairColour"]

    body = [MII_VERSION] + [parts[name] for name, _ in MII_PARTS]

    # Placement steps. Kept near the middle rather than spread over the full
    # -12..12: the extremes are caricatures, and a plaza of caricatures is a
    # plaza of noise.
    def step() -> int:
        return secrets.randbelow(7) - 3

    packed = "".join(f"{b:02x}" for b in pack_steps([step() for _ in MII_STEPS]))
    return "".join(f"{b & 0xFF:02x}" for b in body) + packed


def decode_mii(mii: str) -> dict | None:
    """The parts of a face, for `list` and `doctor`. None if it is not one."""
    if not mii or len(mii) != MII_BYTES * 2:
        return None
    try:
        raw = bytes.fromhex(mii)
    except ValueError:
        return None
    if raw[0] != MII_VERSION:
        return None

    # Derived from the table rather than written as literals: the byte after the
    # version, one per part, then the offsets. Hard-coded indices are what broke
    # when the format grew.
    out = {name: raw[1 + i] % count for i, (name, count) in enumerate(MII_PARTS)}
    base = 1 + len(MII_PARTS)
    for name, value in zip(MII_STEPS, unpack_steps(raw[base:])):
        out[name] = value
    return out


def describe_mii(mii: str) -> str:
    """One line a person can read, since nothing here can draw the face."""
    parts = decode_mii(mii)
    if parts is None:
        return "no face (the console will derive one from the seed)"

    bits = [
        f"{SKIN_NAMES[parts['skinTone']]} skin",
        # Style 30 is the artwork's own empty hairstyle.
        "bald" if parts["hairStyle"] == 30
        else f"{HAIR_NAMES[parts['hairColour']]} hair {parts['hairStyle']}",
        f"{EYE_NAMES[parts['eyeColour']]} eyes {parts['eyeStyle']}",
        f"mouth {parts['mouthStyle']}",
    ]
    if parts["glasses"]:
        bits.append(f"glasses {parts['glasses']}")
    if parts["mustache"]:
        bits.append(f"moustache {parts['mustache']}")
    if parts["beard"]:
        bits.append(f"beard {parts['beard']}")
    if parts["wrinkles"]:
        bits.append(f"wrinkles {parts['wrinkles']}")
    if parts["makeup"]:
        bits.append(f"makeup {parts['makeup']}")
    if parts["headwear"]:
        bits.append(f"headwear {parts['headwear']}")
    return ", ".join(bits)


# Spare things to carry, so a console can hold more than the one or two its
# persona was written with. The chip row on an encounter card wraps at four,
# and a simulation that never sends four never puts that row on screen.
_ALL_GAMES = [w[0] for w in _WORLDS]

_SPARE_CARRY = [
    "Coin, worn smooth", "Tuning fork", "Lantern shard", "Metronome",
    "Bay bream", "Paper lantern", "Dice, one chipped", "Rope, forty metres",
]


def make_console(persona: tuple, district: str, reach: int) -> dict:
    handle, game, greeting, carrying, activity = persona

    # Vary what a console carries and how many titles it lists. The persona
    # list gives one or two carried things and exactly one game, so without
    # this every pass on the plaza has the same shape: the empty tile and the
    # full chip row - the two that break layouts - would never appear.
    want = random.choices([0, 1, 2, 3, 4], weights=[8, 30, 34, 18, 10])[0]
    pool = list(carrying) + [c for c in _SPARE_CARRY if c not in carrying]
    carried = [c[:LIMITS["title"]] for c in pool[:min(want, LIMITS["carrying"])]]

    games = [game]
    for extra in random.sample(_ALL_GAMES, k=random.randrange(0, 3)):
        if extra not in games and len(games) < LIMITS["games"]:
            games.append(extra)

    # A minority arrive stripped down, which is what a console looks like
    # before anyone has filled their pass in.
    roll = random.random()
    if roll < 0.06:
        greeting = ""
    if roll > 0.94:
        game = ""

    built = {
        "id": secrets.token_hex(16),      # same shape as the console's own id
        "token": secrets.token_hex(32),
        "reach": reach,
        "district": district,
        "pass": {
            "handle": handle[:LIMITS["handle"]],
            "greeting": greeting[:LIMITS["greeting"]],
            "activity": activity[:LIMITS["activity"]],
            "playing": game[:LIMITS["title"]],
            "district": district[:LIMITS["district"]],
            "carrying": carried,
            "games": [g[:LIMITS["title"]] for g in games],
            "mii": random_mii(),
            "portrait": random.getrandbits(32),
            "theme": random.randrange(6),
            "hours": 0 if not game else random.randrange(3, 900),
            "met": random.randrange(4, 2000),
        },
    }

    # Never hand the plaza something the console would clamp on the way in.
    problems = check_pass(built["pass"])
    if problems:
        raise ValueError(f"{handle}: " + "; ".join(problems))
    return built


# ------------------------------------------------------------------ commands

def announce(roster: "Roster", console: dict) -> dict:
    """Register (or re-register) a console and put it in its place bucket.

    Both halves matter: /v1/hello stores the pass, but only /v1/checkin sets the
    place bucket, and a console with no bucket can never be crossed.
    """
    reply = post(roster.server, "/v1/hello", console["id"], console["token"],
                 {"id": console["id"], "client": "nx-plaza-sim/1.0",
                  "region": "EUR", "pass": console["pass"]})
    if not reply.get("ok"):
        return reply
    return post(roster.server, "/v1/checkin", console["id"], console["token"],
                {"id": console["id"], "place": roster.place,
                 "reach": console.get("reach", 1),
                 "district": console.get("district", ""), "awake": True})


def cmd_spawn(args: argparse.Namespace) -> int:
    roster = Roster.load(args.state)
    roster.server = args.server

    if args.world:
        roster.place = ""
        roster.ssid_hint = "(world plaza)"
    elif args.place:
        roster.place = args.place.strip().lower()
        roster.ssid_hint = "(token given directly)"
    elif args.ssid:
        roster.place = place_token(args.ssid)
        roster.ssid_hint = args.ssid
    elif not roster.place and not roster.ssid_hint:
        print("Pick a bucket: --ssid \"Your WiFi\", --place <16 hex>, or --world.",
              file=sys.stderr)
        return 2

    if args.fresh:
        roster.consoles = []

    # The hand-written personas first and in order, so a small spawn gets the
    # ones with a voice; the generated remainder behind them, shuffled so a big
    # spawn is not a run through the alphabet.
    tail = PERSONAS[CURATED:]
    random.shuffle(tail)
    pool = PERSONAS[:CURATED] + tail
    taken = {c["pass"]["handle"] for c in roster.consoles}

    added = []
    for persona in pool:
        if len(added) >= args.count:
            break
        if persona[0] in taken:
            continue
        district = args.district or random.choice(DISTRICTS)
        console = make_console(persona, district, args.reach)
        roster.consoles.append(console)
        added.append(console)

    if len(added) < args.count:
        print(f"note: only {len(PERSONAS)} personas exist, spawned {len(added)}.")

    roster.save()

    print(f"server  {roster.server}")
    print(f"bucket  place={roster.place or '(none)'}  ssid={roster.ssid_hint or '-'}")
    print(f"roster  {len(roster.consoles)} consoles ({len(added)} new) in {args.state}")

    # Naming every console is useful for six and useless for nine hundred.
    verbose = len(added) <= 24

    failed = []
    for index, console in enumerate(added, 1):
        reply = announce(roster, console)
        if not reply.get("ok"):
            failed.append(console)
        if verbose:
            state = "awake" if reply.get("ok") else f"FAILED: {reply.get('error')}"
            print(f"  + {console['pass']['handle']:<10} {console['id'][:8]}  {state}")
        elif index % 50 == 0 or index == len(added):
            print(f"  registered {index}/{len(added)}"
                  + (f", {len(failed)} failed" if failed else ""))

    if failed:
        # Do not keep identities the server never accepted, or running spawn
        # again after starting the server would pile up ghosts.
        keep = {id(console) for console in failed}
        roster.consoles = [c for c in roster.consoles if id(c) not in keep]
        roster.save()
        print("\nSome consoles could not register, and were dropped from the roster.",
              file=sys.stderr)
        print("Is the server running, and is --server right?", file=sys.stderr)
        return 1

    print()
    print("On the Switch:")
    if roster.place:
        print(f"  Settings > How far a crossing reaches > Same network (or Nearby)")
        print(f"  make sure the console is on the Wi-Fi named {roster.ssid_hint!r}")
    else:
        print("  Settings > How far a crossing reaches > Anywhere")
    print(f"  Settings > Plaza server > {roster.server}")
    print()
    print("Then leave this running:  python3 tools/simulate_consoles.py live")
    return 0


def one_cycle(roster: Roster, do_exchange: bool, verbose: bool) -> dict:
    """One round of check-ins (and optionally exchanges) for every console."""
    summary = {"peers": 0, "received": 0, "from_real": [], "errors": []}
    own_ids = roster.ids()

    for console in roster.consoles:
        body = {
            "id": console["id"],
            "place": roster.place,
            "reach": console.get("reach", 1),
            "district": console.get("district", ""),
            "awake": True,
        }
        reply = post(roster.server, "/v1/checkin", console["id"], console["token"], body)
        if not reply.get("ok"):
            summary["errors"].append(f"{console['pass']['handle']} checkin: {reply.get('error')}")
            continue

        peers = reply.get("peers", [])
        summary["peers"] = max(summary["peers"], len(peers))
        if verbose:
            names = ", ".join(f"{p['handle']}/{p['state']}" for p in peers) or "nobody"
            print(f"    {console['pass']['handle']:<10} sees {names}")

        if not do_exchange:
            continue

        body["max"] = 8
        body["pass"] = console["pass"]
        reply = post(roster.server, "/v1/exchange", console["id"], console["token"], body)
        if not reply.get("ok"):
            if reply.get("status") != 429:  # rate limited is fine, just skip
                summary["errors"].append(
                    f"{console['pass']['handle']} exchange: {reply.get('error')}")
            continue

        for crossing in reply.get("crossings", []):
            summary["received"] += 1
            other = crossing.get("id", "")
            if other not in own_ids:
                summary["from_real"].append((console["pass"]["handle"], crossing))

    return summary


def describe_real_pass(receiver: str, crossing: dict) -> str:
    pass_obj = crossing.get("pass", {})
    bits = [f"\033[1m{pass_obj.get('handle', '?')}\033[0m"]
    if pass_obj.get("playing"):
        bits.append(f"playing {pass_obj['playing']}")
    if crossing.get("place"):
        bits.append(f"at {crossing['place']}")
    if pass_obj.get("carrying"):
        bits.append("carrying " + ", ".join(pass_obj["carrying"]))
    line = f"  YOUR PASS reached {receiver}: " + " - ".join(bits)
    if pass_obj.get("greeting"):
        line += f"\n      “{pass_obj['greeting']}”"
    return line


def cmd_once(args: argparse.Namespace) -> int:
    roster = Roster.load(args.state)
    if not roster.consoles:
        print("No consoles yet. Run `spawn` first.", file=sys.stderr)
        return 2
    if args.server != DEFAULT_SERVER:
        roster.server = args.server

    summary = one_cycle(roster, do_exchange=True, verbose=True)
    print(f"  {len(roster.consoles)} consoles checked in, "
          f"{summary['received']} passes collected between them")
    for receiver, crossing in summary["from_real"]:
        print(describe_real_pass(receiver, crossing))
    for error in summary["errors"]:
        print(f"  ! {error}", file=sys.stderr)
    return 1 if summary["errors"] else 0


def cmd_live(args: argparse.Namespace) -> int:
    roster = Roster.load(args.state)
    if not roster.consoles:
        print("No consoles yet. Run `spawn` first.", file=sys.stderr)
        return 2
    if args.server != DEFAULT_SERVER:
        roster.server = args.server

    print(f"{len(roster.consoles)} consoles awake on {roster.server}")
    print(f"bucket place={roster.place or '(world)'}  "
          f"checking in every {args.interval}s, trading every {args.trade}s")
    print("Ctrl-C to stop. The server forgets a console ~180s after its last check-in.\n")

    seen_real: set[str] = set()
    last_trade = 0.0
    cycle = 0
    matching_note = ""

    def matching_check() -> str:
        """One line about any console that registered but cannot cross.

        A console in the wrong bucket never shows up at all, so without this the
        only symptom is a cycle counter reporting zero for ever.
        """
        data = fetch_debug(roster, quiet=True)
        if data is None:
            return ""
        own = {c["id"][:8] for c in roster.consoles}
        rows = data.get("consoles", [])
        sim_places = {r["place"] for r in rows if r["id"] in own and r["place"]}
        sim_areas = {r["area"] for r in rows if r["id"] in own and r["area"]}

        stuck = []
        for row in rows:
            if row["id"] in own:
                continue
            if row["place"] and row["place"] in sim_places:
                continue
            if row["reach"] >= 1 and row["area"] and row["area"] in sim_areas:
                continue
            stuck.append(row["handle"] or row["id"])
        if not stuck:
            return ""
        return (f"! {', '.join(stuck)} registered here but shares no bucket with the sims, "
                f"so it can never cross them.\n  Run `doctor` for the reason and the fix.")

    try:
        while True:
            now = time.time()
            do_exchange = now - last_trade >= args.trade
            if do_exchange:
                last_trade = now

            summary = one_cycle(roster, do_exchange, verbose=args.verbose)
            cycle += 1

            stamp = time.strftime("%H:%M:%S")
            note = f"peers visible {summary['peers']}"
            if do_exchange:
                note += f", {summary['received']} passes traded"
            print(f"[{stamp}] cycle {cycle}: {note}")

            for receiver, crossing in summary["from_real"]:
                key = crossing.get("id", "")
                print(describe_real_pass(receiver, crossing))
                if key and key not in seen_real:
                    seen_real.add(key)
                    print("      (that is your Switch -- the trade is working both ways)")

            for error in summary["errors"]:
                print(f"  ! {error}")

            # Checked at the start and then occasionally, so a console that
            # joins later is still noticed.
            if cycle == 1 or cycle % 15 == 0:
                note = matching_check()
                if note and note != matching_note:
                    matching_note = note
                    print(note)

            time.sleep(max(2.0, args.interval))
    except KeyboardInterrupt:
        print("\nstopped. The sims stay registered; run `forget` to remove them.")
    return 0


def cmd_list(args: argparse.Namespace) -> int:
    roster = Roster.load(args.state)
    if not roster.consoles:
        print("No consoles yet. Run `spawn` first.")
        return 0

    print(f"server {roster.server}")
    print(f"bucket place={roster.place or '(world)'}  ssid={roster.ssid_hint or '-'}")
    print()

    # Three lines each is right for a handful and unreadable for a thousand.
    shown = roster.consoles if args.all else roster.consoles[:args.limit]
    for console in shown:
        pass_obj = console["pass"]
        print(f"  {pass_obj['handle']:<10} {console['id'][:12]}  "
              f"theme {pass_obj['theme']}  {pass_obj['playing']}  @ {console['district']}")
        print(f"             “{pass_obj['greeting']}”")
        print(f"             {describe_mii(pass_obj.get('mii', ''))}")

    hidden = len(roster.consoles) - len(shown)
    if hidden > 0:
        print(f"\n  ... and {hidden} more. Use --all, or --limit N.")

    stats = None
    try:
        with urllib.request.urlopen(roster.server.rstrip("/") + "/", timeout=5) as response:
            stats = json.loads(response.read()).get("stats")
    except Exception as error:  # noqa: BLE001 - a down server is not fatal here
        print(f"\n(server not reachable: {error})")
    if stats:
        print(f"\nserver stats: {stats}")
    return 0


def cmd_touch(args: argparse.Namespace) -> int:
    """Change what the sims are carrying, so a repeat crossing shows new news."""
    roster = Roster.load(args.state)
    if not roster.consoles:
        print("No consoles yet. Run `spawn` first.", file=sys.stderr)
        return 2

    extras = [
        "Found a second lantern", "Traded the radish. No regrets.",
        "New personal best, barely", "Someone left a note on the pier",
        "The bell rang. It was me.", "Down to 12 pieces",
    ]
    for console in roster.consoles:
        console["pass"]["greeting"] = random.choice(extras)
        console["pass"]["hours"] += random.randrange(1, 20)
        console["pass"]["met"] += random.randrange(1, 30)
        if args.reroll_faces:
            console["pass"]["portrait"] = random.getrandbits(32)
            console["pass"]["mii"] = random_mii()
        reply = announce(roster, console)
        state = "ok" if reply.get("ok") else f"FAILED: {reply.get('error')}"
        print(f"  ~ {console['pass']['handle']:<10} {state}")
    roster.save()
    return 0


def cmd_recross(args: argparse.Namespace) -> int:
    """Clear the six-hour pair cooldown so the same sims can cross you again.

    Done by asking the server to forget the console and saying hello again with
    the same id, which drops its pair ledger. Your Switch keeps the pass it
    already has and will bump its crossing counter ("2nd crossing") next time.
    """
    roster = Roster.load(args.state)
    targets = roster.by_name(args.who) if args.who else roster.consoles
    if not targets:
        print("Nobody matched.", file=sys.stderr)
        return 2

    for console in targets:
        post(roster.server, "/v1/forget", console["id"], console["token"],
             {"id": console["id"]})
        reply = announce(roster, console)
        state = "ready to cross again" if reply.get("ok") else f"FAILED: {reply.get('error')}"
        print(f"  * {console['pass']['handle']:<10} {state}")

    print("\nNow press X on the plaza screen to look for passes.")
    return 0


def cmd_forget(args: argparse.Namespace) -> int:
    roster = Roster.load(args.state)
    if not roster.consoles:
        print("Nothing to forget.")
        return 0

    for console in roster.consoles:
        reply = post(roster.server, "/v1/forget", console["id"], console["token"],
                     {"id": console["id"]})
        state = "removed" if reply.get("ok") else f"FAILED: {reply.get('error')}"
        print(f"  - {console['pass']['handle']:<10} {state}")

    if args.keep_roster:
        print("\nRoster kept; `spawn` would re-register the same ids.")
    else:
        os.remove(roster.path)
        print(f"\nDeleted {roster.path}")
    return 0


def fetch_debug(roster: "Roster", quiet: bool = False) -> dict | None:
    """The server's view of every console's matching buckets."""
    try:
        with urllib.request.urlopen(roster.server.rstrip("/") + "/v1/debug", timeout=6) as reply:
            return json.loads(reply.read())
    except urllib.error.HTTPError as error:
        if quiet:
            return None
        if error.code == 403:
            print("The server was started without --debug, so it will not report buckets.\n"
                  "Restart it as:  python3 server/plaza_server.py --debug", file=sys.stderr)
        else:
            print(f"Server said HTTP {error.code}.", file=sys.stderr)
    except Exception as error:  # noqa: BLE001 - any failure here is just "no data"
        if not quiet:
            print(f"Could not reach {roster.server}: {error}", file=sys.stderr)
    return None


def diagnose(roster: "Roster", rows: list[dict]) -> list[str]:
    """Why is a console not crossing the sims? Returns lines to print."""
    own = {c["id"][:8] for c in roster.consoles}
    sims = [r for r in rows if r["id"] in own]
    others = [r for r in rows if r["id"] not in own]

    lines = []
    if not sims:
        return ["No simulated consoles are registered on this server yet. Run `spawn`."]

    sim_places = {r["place"] for r in sims if r["place"]}
    sim_areas = {r["area"] for r in sims if r["area"]}

    if not others:
        lines.append("No console other than the sims has registered here yet.")
        lines.append("Point the Switch at this server in Settings > Plaza server, open the")
        lines.append("app, and give it twenty seconds to check in.")
        return lines

    for row in others:
        label = f"{row['handle'] or '(no pass yet)'} [{row['id']}]"
        lines.append(f"{label}")
        lines.append(f"  face:         "
                     f"{'yes' if row.get('face') else 'none yet -- derived from the seed'}")
        lines.append(f"  last seen {row['seen']}s ago"
                     f"{'' if row['awake'] else '  -- past the presence window, it looks asleep'}")

        place_match = row["place"] and row["place"] in sim_places
        area_match = row["area"] and row["area"] in sim_areas

        if not row["place"]:
            lines.append("  place bucket: none -- it is on Ethernet, or reported no Wi-Fi name")
        else:
            lines.append(f"  place bucket: {row['place']}  "
                         f"{'MATCHES the sims' if place_match else 'does NOT match the sims'}")
        if not row["area"]:
            lines.append("  area bucket:  none")
        else:
            lines.append(f"  area bucket:  {row['area']}  "
                         f"{'matches the sims' if area_match else 'does NOT match the sims'}")

        reach_name = {0: "Same network", 1: "Nearby", 2: "Anywhere"}.get(row["reach"], "?")
        lines.append(f"  reach:        {row['reach']} ({reach_name})")

        # The verdict, in the order the server actually applies the rules.
        if place_match:
            lines.append("  -> it should be crossing. If it is not, its daily limit may be")
            lines.append("     spent, or the six-hour pair cooldown is still running.")
            continue

        if row["reach"] >= 1 and area_match:
            lines.append("  -> it should be crossing on the area bucket.")
            continue

        lines.append("  -> NOT CROSSING. Nothing lines up:")
        if not place_match:
            lines.append("     * the place bucket differs, so the SSID the sims were given is")
            lines.append(f"       not the one the console is on (sims used {roster.ssid_hint!r}).")
            lines.append("       Read the exact token off the console -- Settings > This")
            lines.append("       console > Wi-Fi match token -- and respawn the sims with")
            lines.append("       `spawn --fresh --place <that token>`.")
        if row["reach"] == 0 and not place_match:
            lines.append("     * reach is 'Same network', so the console will only ever match")
            lines.append("       on the place bucket. Set it to 'Nearby' to allow the area one.")
        if row["reach"] >= 1 and not area_match:
            lines.append("     * the area buckets differ too. That happens when the sims talk")
            lines.append("       to the server over 127.0.0.1 while the console comes in over")
            lines.append("       the LAN: the server buckets by /24, and those are different")
            lines.append("       networks. Run the sims against the machine's LAN address")
            lines.append("       (--server http://<lan-ip>:8080) to share it.")

    return lines


def cmd_doctor(args: argparse.Namespace) -> int:
    roster = Roster.load(args.state)
    if args.server != DEFAULT_SERVER:
        roster.server = args.server

    rows_data = fetch_debug(roster)
    if rows_data is None:
        return 1
    rows = rows_data.get("consoles", [])

    own = {c["id"][:8] for c in roster.consoles}
    print(f"server {roster.server}")
    print(f"sims   {len([r for r in rows if r['id'] in own])} registered"
          f" (roster holds {len(roster.consoles)})")
    print(f"place  sims are in bucket "
          f"{next((r['place'] for r in rows if r['id'] in own and r['place']), '(none)')}"
          f"  from ssid {roster.ssid_hint!r}")
    print(f"area   sims are in bucket "
          f"{next((r['area'] for r in rows if r['id'] in own and r['area']), '(none)')}")
    print()

    for line in diagnose(roster, rows):
        print(line)
    return 0


def cmd_place(args: argparse.Namespace) -> int:
    if not args.ssid:
        print("usage: simulate_consoles.py place --ssid \"Your WiFi\"", file=sys.stderr)
        return 2
    print(place_token(args.ssid))
    return 0


# --------------------------------------------------------------------- entry


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Simulate other consoles so one Switch is enough to test with.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Typical session:")[-1],
    )
    parser.add_argument("--server", default=DEFAULT_SERVER, help="plaza server URL")
    parser.add_argument("--state", default=DEFAULT_STATE,
                        help="where the fake identities are kept")

    sub = parser.add_subparsers(dest="command", required=True)

    spawn = sub.add_parser("spawn", help="create fake consoles and register them")
    spawn.add_argument("--count", type=int, default=6)
    spawn.add_argument("--ssid", help="your Wi-Fi name; hashed the way the console does")
    spawn.add_argument("--place", help="a place token (16 hex chars) to use directly")
    spawn.add_argument("--world", action="store_true",
                       help="no bucket; needs reach 'Anywhere' on the console")
    spawn.add_argument("--district", help="force the place label they all advertise")
    spawn.add_argument("--reach", type=int, default=1, choices=(0, 1, 2))
    spawn.add_argument("--fresh", action="store_true",
                       help="throw away the existing roster and make new ids")
    spawn.set_defaults(func=cmd_spawn)

    live = sub.add_parser("live", help="keep them awake and trading (the main mode)")
    live.add_argument("--interval", type=float, default=20.0, help="seconds between check-ins")
    live.add_argument("--trade", type=float, default=45.0, help="seconds between exchanges")
    live.add_argument("--verbose", action="store_true", help="print what each console sees")
    live.set_defaults(func=cmd_live)

    once = sub.add_parser("once", help="a single check-in and exchange for everyone")
    once.set_defaults(func=cmd_once)

    listing = sub.add_parser("list", help="show the roster and server stats")
    listing.add_argument("--limit", type=int, default=20,
        help="how many consoles to print (default 20)")
    listing.add_argument("--all", action="store_true", help="print every one")
    listing.set_defaults(func=cmd_list)

    touch = sub.add_parser("touch", help="give them new greetings, so repeats look fresh")
    touch.add_argument("--reroll-faces", action="store_true",
                       help="give everyone a new Mii as well as a new greeting")
    touch.set_defaults(func=cmd_touch)

    recross = sub.add_parser("recross", help="clear the 6h cooldown and cross you again")
    recross.add_argument("--who", help="only consoles whose name contains this")
    recross.set_defaults(func=cmd_recross)

    forget = sub.add_parser("forget", help="remove the fake consoles from the server")
    forget.add_argument("--keep-roster", action="store_true")
    forget.set_defaults(func=cmd_forget)

    doctor = sub.add_parser("doctor",
                            help="explain why a console is or is not crossing the sims")
    doctor.set_defaults(func=cmd_doctor)

    place = sub.add_parser("place", help="print the place token for an SSID")
    place.add_argument("--ssid")
    place.set_defaults(func=cmd_place)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
