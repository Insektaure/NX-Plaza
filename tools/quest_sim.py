#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Climbs the tower a few thousand times, so the balance can be measured.

A port of the quest: sheetFor() and itemBonus() out of
source/core/quest_rules.cpp, and the fight out of source/scenes/quest.cpp.
Kept literal, because the only thing this is for is answering questions the
app cannot be asked without playing it for a month.

    tools/quest_sim.py depth              the table in the kBudget comment
    tools/quest_sim.py depth --scale      the same, with floor-scaled gear
    tools/quest_sim.py farm               progression over a season of play
    tools/quest_sim.py farm --forge 3      the same, with the forge
    tools/quest_sim.py forge              what an exchange rate is worth
    tools/quest_sim.py blessed            a geared party with blessings, floor
                                          by floor: what each side deals
    tools/quest_sim.py season             from no gear to the top: how many
                                          climbs and hours it takes

`depth` is the one that says whether the port is honest: it reproduces the
table written above kBudget in quest_rules.cpp, which was measured by the app
itself. If those columns drift, this file is wrong and quest_rules.cpp is
right.

Gear scales with the floor it fell on, by the square root of the floor, which
is depthFactor() in quest_rules.cpp and is what the app does. `--flat` turns
it off - the shape gear had before - and `--scale N` is the other candidate
that was measured and not taken: a straight line capped at floor N, which
stops paying exactly where a large collection starts climbing.

So `depth --flat` is the command that reproduces the table in the kBudget
comment, since that table was measured before any of this existed.

`depth`, `farm` and `forge` climb with no blessings at all. `blessed` and
`season` are offered three after every fifth floor, out of quest_boons.cpp,
and pick by a fixed priority (`--picks`) since there is nobody to choose.
`season` is what the copy limits in kPool were set against; `--cap` and
`--hale` change them, to measure a different limit before writing it.

Stdlib only.
"""

from __future__ import annotations

import argparse
import random
import statistics
import sys

# --------------------------------------------------------------- the numbers
# All of these are quest_rules.cpp. Nothing here is invented.

Q_COMMON, Q_UNCOMMON, Q_RARE, Q_EPIC, Q_LEGENDARY, Q_GODLIKE = range(6)
Q_COUNT = 6
QUALITY_NAMES = ["common", "uncommon", "rare", "epic", "legendary", "godlike"]

SLOT_WEAPON, SLOT_ARMOUR, SLOT_RING, SLOT_ACCESSORY = range(4)
SLOT_COUNT = 4

CLS_GUARD, CLS_BLADE, CLS_SPARK, CLS_MENDER = range(4)
CLS_COUNT = 4

BUDGET = [3, 6, 10, 16, 24, 36]
AFFIXES = [1, 2, 2, 3, 4, 5]
HP_PER_POINT = 4

# 0 hp, 1 mp, 2 atk, 3 def, 4 spd
SPLIT = [
    (2, (4, 1, 3, 0)),  # a weapon is attack, then speed, wind, guard
    (0, (3, 1, 2, 4)),  # armour is health, then guard
    (1, (4, 3, 0, 2)),  # a ring is wind
    (4, (2, 0, 1, 3)),  # an accessory is speed
]


def add_to(sheet, which, points):
    """quest_rules.cpp's addTo: health is handed out four to the point."""
    if which == 0:
        sheet[0] += points * HP_PER_POINT
    else:
        sheet[which] += points


def depth_factor(floor, cap, span, root):
    """The floor multiplier on a budget, as a (numerator, denominator) pair so
    the app can do it in integers.

    `root` picks the shape. A capped straight line stops paying at `cap`; a
    square root never stops and never runs away, because the shadow's health
    grows with the floor and this grows with its square root - the gap widens
    for ever, however good the gear gets, which is what keeps the tower from
    having no top.
    """
    if root:
        return (span + isqrt(max(1, floor) * 16), span)
    return (span + min(floor, cap), span)


def isqrt(n):
    x = int(n ** 0.5)
    while x * x > n:
        x -= 1
    while (x + 1) * (x + 1) <= n:
        x += 1
    return x


def item_bonus(quality, slot, seed, floor=0, scale_cap=0, scale_span=48,
               root=False):
    """itemBonus(), with the floor multiplier bolted on behind a flag.

    A sheet here is a list - hp, mp, atk, def, spd - rather than five named
    fields, because everything downstream either sums it or indexes it.
    """
    roll = seed
    turn = (roll >> 2) & 7
    split = 55 + ((roll >> 5) & 31)
    scale = 70 + ((roll >> 10) & 63)

    others = AFFIXES[quality] - 1
    budget = (BUDGET[quality] * scale + 50) // 100

    # The proposal, and the only line in this file the app does not have.
    # Capped, so it stops paying inside the band anybody can actually climb.
    if scale_cap or root:
        num, den = depth_factor(floor, scale_cap, scale_span, root)
        budget = (budget * num + den // 2) // den

    if budget < others + 1:
        budget = others + 1

    primary = (budget * split + 50) // 100
    if budget - primary < others:
        primary = budget - others
    if primary < 1:
        primary = 1
    rest = budget - primary

    sheet = [0, 0, 0, 0, 0]
    prim_stat, rest_stats = SPLIT[slot]
    add_to(sheet, prim_stat, primary)

    weight = others
    total = others * (others + 1) // 2
    given = 0
    for i in range(others):
        if i + 1 == others:
            share = rest - given if rest > given else 1
        else:
            share = max(1, (rest * weight) // total)
        add_to(sheet, rest_stats[(turn + i) % 4], share)
        given += share
        weight -= 1
    return sheet


def item_rating(bonus):
    """itemRating(): health at a quarter, which is how it was handed out."""
    return bonus[0] // HP_PER_POINT + bonus[1] + bonus[2] + bonus[3] + bonus[4]


def drop_weights(floor):
    f = min(999, max(1, floor))
    return [
        max(1, 60 - 3 * f),
        25 + f,
        8 + f,
        max(0, f - 3),
        max(0, (f - 8) // 2),
        max(0, (f - 15) // 4),
    ]


def roll_drop(rng, floor, chance=50):
    """rollDrop(). Returns (quality, slot, seed, floor) or None."""
    if rng.randrange(100) >= max(0, min(100, chance)):
        return None
    f = min(999, max(1, floor))
    weights = drop_weights(f)
    pick = rng.randrange(sum(weights))
    quality = Q_COMMON
    for i, w in enumerate(weights):
        if pick < w:
            quality = i
            break
        pick -= w
    return (quality, rng.randrange(SLOT_COUNT), rng.randrange(65536), f)


def boss_for(floor):
    f = min(999, max(1, floor))
    return {
        "hp": 90 + 62 * f,
        "atk": 12 + 5 * f,
        "def": 2 + f,
        "spd": 11 + f // 2,
    }


def sheet_for(build, height, colour, crossed, travelled, hours, titles):
    """sheetFor(). Returns [hp, mp, atk, def, spd], cls."""
    known = min(crossed, 25)
    build = min(build, 127)
    height = min(height, 127)
    return (
        [
            48 + build // 3 + known * 4,                            # hp
            6 + min(titles, 32) // 4 + min(crossed, 12),            # mp
            8 + known + min(hours, 60) // 6,                        # atk
            3 + build // 24 + min(crossed, 12) // 2,                # def
            10 + (127 - height) // 12 + min(travelled, 60) // 8,    # spd
        ],
        colour % CLS_COUNT,
    )


# ------------------------------------------------------------------ the fight


class Boons:
    """The defaults out of quest_boons.h. The published table was measured
    with no blessings at all, which is what --boons off means and what the
    depth command uses, because a blessing is drawn rather than chosen and
    averages a floor and a half on a run that is already going well."""

    def __init__(self):
        self.atk = 1.0
        self.taken = 1.0
        self.hp = 1.0
        self.spd = 0
        self.crit = 12
        self.mp_per_floor = 4
        self.skill_cost = 6
        self.full_mp = False
        self.second_wind = False
        self.spent_wind = False
        self.rally = 0.0
        self.per_floor = 0.0
        self.since = 0
        self.last_stand = False
        self.guard_sweep = False
        self.mend_power = 3.0
        self.mend_at = 0.6
        self.spark_bite = 0.08
        self.drop_chance = 50

    # The three stacking numbers are sums, held to a quarter where the fight
    # reads them - Boons::attack() and Boons::suffered().
    def attack(self):
        return max(LEAST, self.atk)

    def suffered(self):
        return max(LEAST, self.taken)


LEAST = 0.25
TOP = 999  # kTopFloor: a climb ends here

# kPool, as (id, name, stacks, most, needs). `most` is the copies the pool
# hands out; `needs` is the class it is useless without, or None.
POOL = [
    (1, "Whetstone", True, 5, None), (2, "Ironclad", True, 5, None),
    (3, "Hale", True, 10, None), (4, "Fleet", True, 5, None),
    (5, "Reckless", True, 5, None), (6, "Bulwark", True, 5, None),
    (7, "Keen edge", False, 1, None), (8, "Second wind", False, 1, None),
    (9, "Rally", False, 1, None), (10, "Momentum", False, 1, None),
    (11, "Last stand", False, 1, None), (12, "Deep breath", False, 1, None),
    (13, "Battle rhythm", False, 1, None), (14, "Cheap tricks", False, 1, None),
    (15, "Long watch", False, 1, 0), (16, "Mending hands", False, 1, 3),
    (17, "Bright spark", False, 1, 2), (18, "Scavenger", False, 1, None),
]


def apply_boon(b, bid):
    """applyBoon()."""
    if bid == 1:
        b.atk += 0.20
    elif bid == 2:
        b.taken -= 0.20
    elif bid == 3:
        b.hp += 0.20
    elif bid == 4:
        b.spd += 3
    elif bid == 5:
        b.atk += 0.35
        b.taken += 0.25
    elif bid == 6:
        b.taken -= 0.35
        b.atk -= 0.20
    elif bid == 7:
        b.crit = 36
    elif bid == 8:
        b.second_wind = True
    elif bid == 9:
        b.rally = 0.20
    elif bid == 10:
        b.per_floor = 0.05
        b.since = 0
    elif bid == 11:
        b.last_stand = True
    elif bid == 12:
        b.mp_per_floor = 8
    elif bid == 13:
        b.full_mp = True
    elif bid == 14:
        b.skill_cost = 4
    elif bid == 15:
        b.guard_sweep = True
    elif bid == 16:
        b.mend_power = 4.5
        b.mend_at = 0.75
    elif bid == 17:
        b.spark_bite = 0.16
    elif bid == 18:
        b.drop_chance = 80


def offer_boons(rng, held, classes, most=None):
    """offerBoons(). `most` overrides a blessing's copy limit, by id."""
    pool = []
    for bid, _, stacks, cap, needs in POOL:
        cap = (most or {}).get(bid, cap)
        if not stacks and bid in held:
            continue
        if stacks and held.count(bid) >= cap:
            continue
        if needs is not None and needs not in classes:
            continue
        pool.append(bid)
    out = []
    for _ in range(3):
        if not pool:
            break
        out.append(pool.pop(rng.randrange(len(pool))))
    return out


# What a player who knows the pool takes, first to last. `balanced` puts
# Momentum first, then the one-offs that matter, then attack and survival in
# turn; `damage` leans on Whetstone and Reckless. `random` is somebody who
# has not read the cards.
PICKS = {
    "damage": [10, 13, 14, 7, 16, 15, 8, 17, 9, 12, 1, 5, 3, 2, 6, 4, 11, 18],
    "balanced": [10, 13, 14, 16, 15, 8, 7, 17, 12, 9, 3, 1, 2, 5, 6, 4, 11, 18],
}


def choose_boon(picks, rng, offer):
    if picks == "random":
        return rng.choice(offer)
    order = PICKS[picks]
    return min(offer, key=order.index)


class Member:
    __slots__ = ("base", "cls", "sheet", "max_hp", "hp", "mp")

    def __init__(self, base, cls):
        self.base = base
        self.cls = cls
        self.sheet = list(base)
        self.max_hp = 1
        self.hp = 1
        self.mp = 0

    def wear(self, pieces, scale_cap=0, scale_span=48, root=False):
        """dress(): the gear folded into the sheet, which is addition."""
        self.sheet = list(self.base)
        for quality, slot, seed, floor in pieces:
            bonus = item_bonus(quality, slot, seed, floor, scale_cap, scale_span,
                               root)
            for i in range(5):
                self.sheet[i] += bonus[i]


def hit_for(rng, atk, dfn, mult, crit_pct):
    """hitFor(). The truncation is deliberate - C++ int() rounds toward zero
    and so does Python's, which is the only reason this is a literal port."""
    base = int(float(atk) - float(dfn) * 0.5)
    base = max(1, int(float(base) * mult))
    wobble = max(1, base // 4)
    out = base + rng.randrange(wobble * 2 + 1) - wobble
    big = rng.randrange(100) < crit_pct
    if big:
        out = int(float(out) * 1.7)
    return max(1, out), big


def climb(rng, party, boons, start=1, ceiling=400, drops=None,
          scale_cap=0, scale_span=48, root=False, picks=None, most=None,
          trace=None):
    """One run, bottom to wherever it ends. Returns the deepest floor cleared.

    `drops`, if given, is a list the shadows' leavings are appended to, which
    is what the farm command reads. `picks` turns the blessings on - three
    offered after every fifth floor, chosen by choose_boon() - and `most`
    overrides their copy limits. `trace`, if given, is a Trace that is told
    about every floor. Everything else is quest.cpp.
    """
    held = []
    classes = {u.cls for u in party}
    units = party
    for u in units:
        u.max_hp = max(1, int(float(u.sheet[0]) * boons.hp))
        u.hp = u.max_hp
        u.mp = u.sheet[1]

    fallen = 0
    deepest = 0
    floor = start

    while floor <= ceiling:
        boss = boss_for(floor)
        boss_hp = boss["hp"]
        boss_atk = float(boss["atk"])
        rnd = 0
        guard = -1
        order = []
        turn = 0

        def standing():
            return sum(1 for u in units if u.hp > 0)

        def atk_of(u):
            gain = (1.0 + boons.rally * float(fallen)) * (
                1.0 + boons.per_floor * float(boons.since))
            if boons.last_stand and standing() == 1:
                gain *= 2.0
            return max(1, int(float(u.sheet[2]) * boons.attack() * gain))

        def spd_of(u):
            return u.sheet[4] + boons.spd

        def guarding():
            return 0 <= guard < len(units) and units[guard].hp > 0

        def fell(u):
            nonlocal fallen, guard
            if boons.second_wind and not boons.spent_wind:
                boons.spent_wind = True
                u.hp = max(1, u.max_hp // 2)
                return False
            fallen += 1
            if guard >= 0 and u is units[guard]:
                guard = -1
            return True

        wiped = False
        while True:
            if turn >= len(order):
                rnd += 1
                order = [i for i, u in enumerate(units) if u.hp > 0]
                order.append(-1)  # the shadow
                order.sort(key=lambda i: -(boss["spd"] if i < 0
                                           else spd_of(units[i])))
                turn = 0
            if not order:
                break

            actor = order[turn]
            turn += 1
            if trace:
                trace.actions += 1

            if actor < 0:
                # bossActs()
                atk = int(boss_atk)
                if rnd % 4 == 0:
                    for u in units:
                        if u.hp <= 0:
                            continue
                        bite = 0.55 * boons.suffered() * (
                            0.5 if boons.guard_sweep and guarding() else 1.0)
                        dealt, _ = hit_for(rng, atk, u.sheet[3], bite, boons.crit)
                        if trace:
                            trace.sweep.append(dealt)
                        u.hp = max(0, u.hp - dealt)
                        if u.hp == 0:
                            fell(u)
                else:
                    if guarding():
                        target = units[guard]
                    else:
                        alive = [u for u in units if u.hp > 0]
                        if not alive:
                            break
                        target = rng.choice(alive)
                    guarded = guard >= 0 and target is units[guard]
                    bite = (0.5 if guarded else 1.0) * boons.suffered()
                    dealt, _ = hit_for(rng, atk, target.sheet[3], bite, boons.crit)
                    if trace:
                        trace.single.append(dealt)
                    target.hp = max(0, target.hp - dealt)
                    if target.hp == 0:
                        fell(target)
            else:
                # allyActs()
                u = units[actor]
                if u.hp <= 0:
                    continue
                dealt_before = boss_hp
                has_mp = u.mp >= boons.skill_cost
                done = False
                if u.cls == CLS_BLADE and has_mp:
                    u.mp -= boons.skill_cost
                    dealt, _ = hit_for(rng, atk_of(u), boss["def"], 1.8, boons.crit)
                    boss_hp -= dealt
                    done = True
                elif u.cls == CLS_MENDER and has_mp:
                    hurt, worst = None, 2.0
                    for other in units:
                        if other.hp <= 0:
                            continue
                        share = float(other.hp) / float(max(1, other.max_hp))
                        if share < worst:
                            worst, hurt = share, other
                    if hurt and float(hurt.hp) < float(hurt.max_hp) * boons.mend_at:
                        u.mp -= boons.skill_cost
                        given = int(float(atk_of(u)) * boons.mend_power)
                        hurt.hp = min(hurt.max_hp, hurt.hp + given)
                        done = True
                elif u.cls == CLS_SPARK and has_mp:
                    u.mp -= boons.skill_cost
                    dealt, _ = hit_for(rng, atk_of(u), boss["def"], 1.3, boons.crit)
                    boss_hp -= dealt
                    boss_atk = max(float(boss["atk"]) * 0.6,
                                   boss_atk * (1.0 - boons.spark_bite))
                    done = True
                elif u.cls == CLS_GUARD and has_mp and guard != actor:
                    u.mp -= boons.skill_cost
                    guard = actor
                    done = True

                if not done:
                    dealt, _ = hit_for(rng, atk_of(u), boss["def"], 1.0, boons.crit)
                    boss_hp -= dealt
                if trace and dealt_before != boss_hp:
                    trace.dealt += dealt_before - boss_hp

            if boss_hp <= 0:
                break
            if standing() == 0:
                wiped = True
                break
            # Thirty rounds is two walls, and the shadow is the one that can
            # wait.
            if rnd > 30:
                wiped = True
                break

        if trace:
            trace.floor(floor, rnd, boons, wiped or boss_hp > 0)
        if wiped or boss_hp > 0:
            break

        deepest = floor
        if drops is not None:
            got = roll_drop(rng, floor, boons.drop_chance)
            if got:
                drops.append(got)
        if floor >= TOP:
            break

        # onward(): a blessing after every fifth floor, not counting the one
        # the climb began on. Hale fills the room it opens straight away.
        if picks and floor % 5 == 0 and floor != start:
            offer = offer_boons(rng, held, classes, most)
            if offer:
                bid = choose_boon(picks, rng, offer)
                held.append(bid)
                apply_boon(boons, bid)
                if trace:
                    trace.held.append(bid)
                if bid == 3:
                    for u in units:
                        was = u.max_hp
                        u.max_hp = max(1, int(float(u.sheet[0]) * boons.hp))
                        if u.hp > 0 and u.max_hp > was:
                            u.hp += u.max_hp - was

        # nextFloor(): a breather, not a heal.
        for u in units:
            if u.hp <= 0:
                continue
            u.hp = min(u.max_hp, u.hp + u.max_hp // 3)
            u.mp = u.sheet[1] if boons.full_mp else min(
                u.sheet[1], u.mp + boons.mp_per_floor)
        boons.since += 1
        floor += 1

    return deepest


# ---------------------------------------------------------------- a console


def make_console(rng, people):
    """A collection of `people`, and you.

    The shape rather than the detail is what matters: most people are crossed
    once or twice and a few are crossed constantly, which is what the plaza
    actually produces and what sheetFor() is tuned against - crossings are in
    four of the five stats.

    How often is a function of the size of the collection, which is the part
    that is easy to get wrong: a console that has met twenty-five people has
    been switched on for months, so it has also crossed each of them several
    times. Modelling everybody as a stranger crossed once made the
    twenty-five-person row come out three floors below what the app measured,
    and the fight was not the thing that was wrong.
    """
    roster = []
    total = 0
    for _ in range(people):
        count = 1 + int(rng.expovariate(1.0 / (0.6 + people / 5.0)))
        total += count
        base, cls = sheet_for(
            build=rng.randrange(128), height=rng.randrange(128),
            colour=rng.randrange(12), crossed=count,
            travelled=rng.randrange(61), hours=rng.randrange(61),
            titles=rng.randrange(33))
        roster.append(Member(base, cls))

    base, cls = sheet_for(
        build=rng.randrange(128), height=rng.randrange(128),
        colour=rng.randrange(12), crossed=people,
        travelled=total, hours=40, titles=20)
    you = Member(base, cls)
    return you, roster


def party_slots(people):
    if people >= 25:
        return 5
    if people >= 10:
        return 4
    return 3


def worth(u):
    """The sort in quest.cpp, so the default party is the same one."""
    return u.sheet[0] // 4 + u.sheet[2] * 2 + u.sheet[3] + u.sheet[4] + u.sheet[1]


def party_of(you, roster, people):
    picked = sorted(roster, key=worth, reverse=True)[:party_slots(people) - 1]
    return [you] + picked


def all_four(rng, people):
    """A console whose default party has a Guard, a Blade, a Spark and a
    Mender in it, re-rolled until it does - so the three class blessings are
    on offer and a table is not an average over parties with no healer."""
    while True:
        you, roster = make_console(rng, people)
        party = party_of(you, roster, people)
        if {u.cls for u in party} == {CLS_GUARD, CLS_BLADE, CLS_SPARK,
                                       CLS_MENDER}:
            return party


class Trace:
    """What climb() is told about each floor, gathered across climbs.

    `stats[floor]` holds lists over every climb that fought that floor: the
    shadow's single blows and sweeps, what the party dealt per round, the
    rounds, and the three multipliers as they stood.
    """

    def __init__(self):
        self.stats = {}
        self.actions = 0
        self.held = []
        self.single, self.sweep, self.dealt = [], [], 0

    def floor(self, floor, rounds, boons, lost):
        s = self.stats.setdefault(floor, {
            "fought": 0, "lost": 0, "rounds": [], "per_round": [],
            "single": [], "sweep": [], "atk": [], "taken": [], "hp": []})
        s["fought"] += 1
        s["lost"] += 1 if lost else 0
        s["rounds"].append(rounds)
        s["per_round"].append(self.dealt / max(1, rounds))
        s["single"].extend(self.single)
        s["sweep"].extend(self.sweep)
        s["atk"].append(boons.attack() * (1.0 + boons.per_floor * boons.since))
        s["taken"].append(boons.suffered())
        s["hp"].append(boons.hp)
        self.single, self.sweep, self.dealt = [], [], 0


CLASS_NAMES = {"guard": CLS_GUARD, "blade": CLS_BLADE, "spark": CLS_SPARK,
               "mender": CLS_MENDER}


def party_from(text):
    """--party: a real party, off the gear screen. One member per comma,
    class:HP/MP/ATK/DEF/SPD as the panel under the Mii prints them - with the
    gear on, which is the only sheet the fight reads.

        guard:610/110/138/39/117,blade:394/90/81/31/112
    """
    party = []
    for part in text.split(","):
        cls, _, numbers = part.strip().partition(":")
        stats = [int(n) for n in numbers.split("/")]
        if cls.lower() not in CLASS_NAMES or len(stats) != 5:
            raise SystemExit("--party wants class:HP/MP/ATK/DEF/SPD, not %r" % part)
        u = Member(stats, CLASS_NAMES[cls.lower()])
        u.sheet = list(stats)
        party.append(u)
    return party


def copy_limits(args):
    """--cap and --hale, as the `most` overrides climb() takes."""
    most = {}
    if args.cap is not None:
        for bid, _, stacks, _, _ in POOL:
            if stacks:
                most[bid] = args.cap
    if args.hale is not None:
        most[3] = args.hale
    return most


def seconds_of(trace, floors):
    """What a climb costs in the app with auto-advance on: 0.55 s an action
    (kBeat), 2.5 s on every cleared floor (kWonBeat), and five to choose a
    blessing, which is a guess at somebody reading three cards."""
    return trace.actions * 0.55 + floors * 2.5 + len(trace.held) * 5.0


def spread(values):
    """Median, and the tenth and ninetieth percentiles."""
    v = sorted(values)
    return v[len(v) // 2], v[len(v) // 10], v[len(v) * 9 // 10]


# ------------------------------------------------------------------ commands


def cmd_depth(args):
    """The table above kBudget in quest_rules.cpp, re-measured."""
    rng = random.Random(args.seed)
    tiers = [None] + list(range(Q_COUNT))
    labels = ["bare"] + QUALITY_NAMES

    # Which floor the worn gear fell on has to be said, now that it decides
    # half of what the gear is worth. The table is about the shape of the
    # tiers, so it holds that floor still; `farm` is the command where gear
    # comes from wherever the party actually got to.
    at = args.found_at or 1
    print("floors reached, mean of %d climbs, gear found on floor %d%s" % (
        args.runs, at, "  [depth scaling off]" if args.flat else ""))
    print("%-14s" % "party" + "".join("%10s" % l for l in labels))

    for people, name in ((4, "four met"), (15, "fifteen met"),
                         (25, "twenty-five")):
        row = []
        for tier in tiers:
            reached = []
            for _ in range(args.runs):
                you, roster = make_console(rng, people)
                party = party_of(you, roster, people)
                for u in party:
                    if tier is None:
                        u.wear([])
                    else:
                        # Worn gear is found gear: a piece scaled to the floor
                        # it fell on has to come from a floor this party can
                        # actually reach, or the scaled column is measuring a
                        # console that cannot exist.
                        u.wear([(tier, s, rng.randrange(65536), at)
                                for s in range(SLOT_COUNT)],
                               args.scale, args.span, args.root)
                reached.append(climb(rng, party, Boons(),
                                     scale_cap=args.scale, scale_span=args.span,
                                     root=args.root))
            row.append(statistics.mean(reached))
        print("%-14s" % name + "".join("%10.1f" % v for v in row))


def cmd_farm(args):
    """A season of play: climb, keep what is better, climb again.

    This is the question the depth table cannot answer - not "how deep does
    this gear go" but "does farming go anywhere", which needs the drops to be
    real and the bag to be kept.
    """
    rng = random.Random(args.seed)
    print("%d climbs, a %d-person collection%s"
          % (args.climbs, args.people,
             "  [floor-scaled gear]" if (args.scale or args.root) else ""))
    print("%8s %8s %10s %10s" % ("climb", "deepest", "best tier", "party pts"))

    you, roster = make_console(rng, args.people)
    party = party_of(you, roster, args.people)
    # Four pegs a head, and nothing is worn that is not the best thing free.
    worn = {id(u): {} for u in party}
    bag = []
    best_seen = -1
    deepest_ever = 0

    for n in range(1, args.climbs + 1):
        drops = []
        for u in party:
            u.wear(list(worn[id(u)].values()), args.scale, args.span, args.root)
        d = climb(rng, party, Boons(), drops=drops,
                  scale_cap=args.scale, scale_span=args.span, root=args.root)
        deepest_ever = max(deepest_ever, d)
        bag.extend(drops)
        for piece in drops:
            best_seen = max(best_seen, piece[0])

        # Equip: every peg takes the best free piece, by rating. The app's
        # auto-equip, applied to the whole party because a simulation has
        # nobody to be polite to.
        pool = sorted(bag, key=lambda p: -item_rating(
            item_bonus(p[0], p[1], p[2], p[3], args.scale, args.span, args.root)))
        taken = set()
        for u in party:
            for slot in range(SLOT_COUNT):
                for i, piece in enumerate(pool):
                    if i in taken or piece[1] != slot:
                        continue
                    cur = worn[id(u)].get(slot)
                    new_r = item_rating(item_bonus(
                        piece[0], piece[1], piece[2], piece[3],
                        args.scale, args.span, args.root))
                    cur_r = -1 if cur is None else item_rating(item_bonus(
                        cur[0], cur[1], cur[2], cur[3], args.scale, args.span,
                        args.root))
                    if new_r > cur_r:
                        worn[id(u)][slot] = piece
                        taken.add(i)
                    break

        # The forge: spare pieces of a rank, melted into one of the next.
        # Models somebody using the forge screen until there is nothing left
        # to melt, which is what anybody does with it.
        # Bottom up, so a run of commons can walk all the way to the top in
        # one pass, and the output is rolled at the deepest floor reached -
        # without that, a console stuck at floor six forges gear fit for
        # floor six and the fix fixes nothing.
        if args.forge:
            worn_ids = {id(p) for u in party for p in worn[id(u)].values()}
            for q in range(Q_COUNT - 1):
                while True:
                    spare = [p for p in bag
                             if p[0] == q and id(p) not in worn_ids]
                    if len(spare) < args.forge:
                        break
                    for p in spare[:args.forge]:
                        bag.remove(p)
                    bag.append((q + 1, rng.randrange(SLOT_COUNT),
                                rng.randrange(65536), max(1, deepest_ever)))
                    best_seen = max(best_seen, q + 1)

        if n % max(1, args.climbs // 12) == 0 or n == args.climbs:
            pts = sum(sum(item_rating(item_bonus(
                p[0], p[1], p[2], p[3], args.scale, args.span, args.root))
                for p in worn[id(u)].values()) for u in party)
            print("%8d %8d %10s %10d" % (
                n, deepest_ever,
                QUALITY_NAMES[best_seen] if best_seen >= 0 else "-", pts))


def cmd_forge(args):
    """What a rank costs, and therefore what an exchange rate is worth.

    Counts the climbs behind one piece of each tier at the floor a console is
    actually stuck on, which is the number the forge's rate has to be set
    against - a rate that takes longer than the wall it is meant to get past
    is not a fix.
    """
    rng = random.Random(args.seed)
    for people, floor in ((4, args.floor4), (15, args.floor15),
                          (25, args.floor25)):
        weights = drop_weights(floor)
        total = sum(weights)
        # Half of the floors leave something, and a climb clears `floor` of
        # them.
        per_climb = floor * 0.5
        print("\n%d people met, stuck around floor %d - %.1f drops a climb"
              % (people, floor, per_climb))
        for q in range(Q_COUNT):
            share = weights[q] / total
            if share <= 0:
                print("   %-10s never falls here" % QUALITY_NAMES[q])
                continue
            per = share * per_climb
            print("   %-10s %5.1f%% of drops, %5.2f a climb, one every %6.1f climbs"
                  % (QUALITY_NAMES[q], 100 * share, per,
                     1.0 / per if per else float("inf")))

        # And what a forge would cost, in climbs, for each rate.
        for rate in args.rates:
            print("   at %d:1, a godlike costs:" % rate)
            for q in range(Q_COUNT - 1):
                need = rate ** (Q_GODLIKE - q)
                share = weights[q] / total
                if share <= 0:
                    continue
                climbs = need / (share * per_climb)
                print("      %-10s x%-8d %8.0f climbs" % (
                    QUALITY_NAMES[q], need, climbs))
    _ = rng


def cmd_blessed(args):
    """A party in fixed gear, climbing with blessings: floor by floor, what
    the shadow's blows do and what the party deals back.

    Fixed gear rolled at random is average gear. A console that has farmed is
    wearing the best rolls of hundreds of pieces, which is worth a great deal
    more - `season` is the command for how far that goes.
    """
    rng = random.Random(args.seed)
    most = copy_limits(args)
    trace = Trace()
    reached = []
    for _ in range(args.runs):
        if args.party:
            party = party_from(args.party)
        else:
            party = all_four(rng, args.people)
            for u in party:
                u.wear([(args.tier, s, rng.randrange(65536), args.gear_floor)
                        for s in range(SLOT_COUNT)], args.scale, args.span,
                       args.root)
        trace.held = []
        reached.append(climb(rng, party, Boons(), ceiling=TOP, root=args.root,
                             scale_cap=args.scale, scale_span=args.span,
                             picks=args.picks, most=most, trace=trace))

    med, lo, hi = spread(reached)
    if args.party:
        print("%d climbs, the party given (%s), picks %s" % (
            args.runs, ", ".join("%s %s" % (
                [k for k, v in CLASS_NAMES.items() if v == u.cls][0],
                "/".join(str(n) for n in u.sheet)) for u in party), args.picks))
    else:
        print("%d climbs, a party of %d (all four classes) out of %d met, %s "
              "gear from floor %d, picks %s" % (
                  args.runs, party_slots(args.people), args.people,
                  QUALITY_NAMES[args.tier], args.gear_floor, args.picks))
    print("deepest floor: median %d (10%% %d, 90%% %d), %.0f%% reach the top"
          % (med, lo, hi, 100.0 * sum(1 for d in reached if d >= TOP) / args.runs))
    print()
    print("%6s %6s %8s %7s %6s %6s | %9s %6s %7s | %6s %6s %6s" % (
        "floor", "alive", "boss HP", "bossATK", "hit", "sweep", "party/rd",
        "rounds", "HP/rd", "ATK x", "taken", "HP x"))
    for f in (1, 10, 25, 50, 75, 100, 150, 200, 300, 400, 500, 600, 700, 800,
              900, 999):
        st = trace.stats.get(f)
        if not st:
            continue
        boss = boss_for(f)
        per = statistics.mean(st["per_round"])
        print("%6d %5.0f%% %8d %7d %6s %6s | %9d %6.1f %6.0f%% | %6.2f %6.2f %6.2f" % (
            f, 100.0 * st["fought"] / args.runs, boss["hp"], boss["atk"],
            "%d" % statistics.mean(st["single"]) if st["single"] else "-",
            "%d" % statistics.mean(st["sweep"]) if st["sweep"] else "-",
            per, statistics.mean(st["rounds"]), 100.0 * per / boss["hp"],
            statistics.mean(st["atk"]), statistics.mean(st["taken"]),
            statistics.mean(st["hp"])))


def cmd_season(args):
    """From no gear to the top of the tower.

    Every climb starts on floor one, because that is where the blessings
    are. After each: the bag takes what fell (a thousand at most, a full one
    throwing out its worst spare for anything better, as takeDrop() does),
    every peg takes the best spare that beats it (auto-equip), and the forge
    melts spares three of a rank into one of the next at the deepest floor
    reached, bottom up, until there is nothing left to melt - an attentive
    player, which makes this a little quicker than most will be.
    """
    most = copy_limits(args)
    rating_of = {}

    def rating(p):
        r = rating_of.get(p)
        if r is None:
            r = item_rating(item_bonus(p[0], p[1], p[2], p[3], args.scale,
                                       args.span, args.root))
            rating_of[p] = r
        return r

    def dress(worn, bag):
        bag.sort(key=rating, reverse=True)
        for w in worn:
            for slot in range(SLOT_COUNT):
                for i, p in enumerate(bag):
                    if p[1] != slot:
                        continue
                    cur = w.get(slot)
                    if cur is None or rating(p) > rating(cur):
                        w[slot] = bag.pop(i)
                        if cur is not None:
                            bag.append(cur)
                    break

    print("%d collections of %d met (a party of %d, all four classes), up to "
          "%d climbs each, picks %s" % (args.collections, args.people,
                                         party_slots(args.people), args.climbs,
                                         args.picks))
    rng = random.Random(args.seed)
    marks = [m for m in (1, 5, 10, 15, 20, 30, 40, 60, 80, 100, 150, 200)
             if m <= args.climbs]
    got_there, hours_there, curves = [], [], []
    for _ in range(args.collections):
        party = all_four(rng, args.people)
        worn = [{} for _ in party]
        bag = []
        deepest = 0
        hours = 0.0
        curve = []
        for n in range(1, args.climbs + 1):
            for u, w in zip(party, worn):
                u.wear(list(w.values()), args.scale, args.span, args.root)
            trace = Trace()
            drops = []
            d = climb(rng, party, Boons(), ceiling=TOP, drops=drops,
                      root=args.root, scale_cap=args.scale, scale_span=args.span,
                      picks=args.picks, most=most, trace=trace)
            hours += seconds_of(trace, d) / 3600.0
            deepest = max(deepest, d)
            curve.append((deepest, hours))
            if d >= TOP:
                got_there.append(n)
                hours_there.append(hours)
                break

            for p in drops:
                if len(bag) + sum(len(w) for w in worn) >= 1000:
                    worst = min(bag, key=rating) if bag else None
                    if worst is None or rating(worst) >= rating(p):
                        continue
                    bag.remove(worst)
                bag.append(p)
            dress(worn, bag)
            for q in range(Q_COUNT - 1):
                spare = [p for p in bag if p[0] == q]
                while len(spare) >= 3:
                    for p in spare[:3]:
                        bag.remove(p)
                    spare = spare[3:]
                    bag.append((q + 1, rng.randrange(SLOT_COUNT),
                                rng.randrange(65536), max(1, deepest)))
            dress(worn, bag)
        curves.append(curve)

    print("reached the top: %d of %d" % (len(got_there), args.collections))
    if got_there:
        med, lo, hi = spread(got_there)
        print("climbs to get there: median %d (10%% %d, 90%% %d)" % (med, lo, hi))
        med, lo, hi = spread(hours_there)
        print("hours of climbing:   median %.0f (10%% %.0f, 90%% %.0f)" % (med, lo, hi))
    print()
    print("%8s %14s %10s" % ("climbs", "best floor", "hours"))
    for m in marks:
        # A collection that got to the top stopped climbing; it stays there.
        at = [c[min(m, len(c)) - 1] for c in curves]
        print("%8d %14d %10.0f" % (m, spread([a[0] for a in at])[0],
                                   spread([a[1] for a in at])[0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=1, help="rng seed")
    ap.add_argument("--scale", type=int, default=0, metavar="CAP",
                    help="floor-scale gear, capped at this floor (0 = off, "
                         "as shipped)")
    ap.add_argument("--span", type=int, default=48, metavar="N",
                    help="the divisor: a floor is worth 1/N of a budget")
    ap.add_argument("--flat", action="store_true",
                    help="turn the depth scaling off, which is how gear worked "
                         "before depthFactor() existed")
    sub = ap.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("depth", help="the kBudget table, re-measured")
    d.add_argument("--runs", type=int, default=400)
    d.add_argument("--found-at", type=int, default=0, metavar="FLOOR",
                   help="pretend the worn gear fell on this floor (scaled only)")
    d.set_defaults(fn=cmd_depth)

    f = sub.add_parser("farm", help="progression over many climbs")
    f.add_argument("--climbs", type=int, default=120)
    f.add_argument("--people", type=int, default=15)
    f.add_argument("--forge", type=int, default=0, metavar="RATE",
                   help="melt RATE spare pieces of a rank into one of the next")
    f.set_defaults(fn=cmd_farm)

    g = sub.add_parser("forge", help="what a rank costs in climbs")
    g.add_argument("--floor4", type=int, default=7)
    g.add_argument("--floor15", type=int, default=12)
    g.add_argument("--floor25", type=int, default=20)
    g.add_argument("--rates", type=int, nargs="+", default=[3, 4])
    g.set_defaults(fn=cmd_forge)

    def blessing_options(parser):
        parser.add_argument("--people", type=int, default=15,
                            help="collection size; 10+ is a party of four, "
                                 "25+ of five")
        parser.add_argument("--picks", choices=("balanced", "damage", "random"),
                            default="balanced", help="how blessings are chosen")
        parser.add_argument("--cap", type=int, default=None, metavar="N",
                            help="copies of each stacking blessing (as shipped: 5)")
        parser.add_argument("--hale", type=int, default=None, metavar="N",
                            help="copies of Hale (as shipped: 10)")

    b = sub.add_parser("blessed", help="a geared party with blessings, floor "
                                       "by floor")
    blessing_options(b)
    b.add_argument("--runs", type=int, default=200)
    b.add_argument("--tier", type=int, default=Q_GODLIKE, metavar="Q",
                   help="gear quality, 0 common to 5 godlike")
    b.add_argument("--gear-floor", type=int, default=999, metavar="FLOOR",
                   help="the floor the worn gear fell on")
    b.add_argument("--party", metavar="SHEETS",
                   help="a real party instead of a made-up one: "
                        "class:HP/MP/ATK/DEF/SPD,... as the gear screen shows "
                        "them (overrides --people, --tier, --gear-floor)")
    b.set_defaults(fn=cmd_blessed)

    s = sub.add_parser("season", help="from no gear to the top")
    blessing_options(s)
    s.add_argument("--collections", type=int, default=20)
    s.add_argument("--climbs", type=int, default=100)
    s.set_defaults(fn=cmd_season)

    args = ap.parse_args()
    # Depth scaling is what the app does, so it is what the sim does unless
    # somebody asks for the old shape. --scale is the other experiment: a
    # straight line capped at a floor, which was measured and not taken.
    args.root = not args.flat and not args.scale
    return args.fn(args) or 0


if __name__ == "__main__":
    sys.exit(main())
