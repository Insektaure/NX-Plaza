#!/usr/bin/env python3
"""Rewrite the README's trophy tables from the table in the source.

    python3 tools/gen_trophy_list.py          # rewrite README.md in place
    python3 tools/gen_trophy_list.py --check  # fail if it is out of date

The list in the README is a snapshot of `kTrophies` in
source/core/trophies.cpp, and a snapshot rots: a trophy added, renamed or
repriced leaves the README describing a condition the screen does not show.
So the section is generated, and --check makes that verifiable before a
release instead of discoverable after one.

Only the section titled "## Trophies" is touched, from its heading to the next
top-level heading. Everything else in the README is left exactly as it is,
including its own trailing newline.
"""

import re
import sys

SOURCE = "source/core/trophies.cpp"
README = "README.md"
HEADING = "## Trophies"
TIERS = ("Bronze", "Silver", "Gold", "Platinum")

# id, name, hint, tier. The hint is allowed to span string literals, since
# clang-format breaks the long ones across lines.
ENTRY = re.compile(
    r'\{\s*"([a-z_0-9]+)",\s*"([^"]+)",\s*\n?\s*'
    r'"((?:[^"]|"\s*\n\s*")+)",?\s*\n?\s*Tier::(\w+)')


def read_trophies(path: str):
    """[(id, name, hint, tier)] in the order the app lists them."""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    try:
        start = text.index("kTrophies = {")
        end = text.index("const std::vector<Trophy>& trophies()")
    except ValueError:
        raise SystemExit(f"{path}: could not find the trophy table")

    rows = []
    for tid, name, hint, tier in ENTRY.findall(text[start:end]):
        # Literals split across lines come back with the quotes between them.
        hint = re.sub(r'"\s*\n\s*"', "", hint)
        rows.append((tid, name, " ".join(hint.split()), tier))

    ids = [r[0] for r in rows]
    if len(set(ids)) != len(ids):
        raise SystemExit(f"{path}: two trophies share an id")
    unknown = sorted({r[3] for r in rows} - set(TIERS))
    if unknown:
        raise SystemExit(f"{path}: unknown tier {unknown}")
    return rows


def render(rows) -> str:
    lines = [
        HEADING,
        "",
        f"All {len(rows)} of them, as the app lists them. Nearly every one is"
        " worked out from",
        "your collection, your puzzles and your wallet at the moment the screen"
        " draws,",
        "so it cannot disagree with what actually happened; the three about the"
        " plaza",
        "dash read the best distance that game keeps. None of them pays a coin,"
        " and",
        "none unlocks anything.",
        "",
    ]
    for tier in TIERS:
        of_tier = [r for r in rows if r[3] == tier]
        if not of_tier:
            continue
        lines += [f"### {tier} ({len(of_tier)})", "", "| | |", "| --- | --- |"]
        lines += [f"| **{name}** | {hint} |" for _, name, hint, _ in of_tier]
        lines.append("")
    # A blank line after the last row, not just a newline: without it the
    # table runs into the heading that follows, and GFM needs the gap to
    # know the table has ended.
    return "\n".join(lines) + "\n"


def splice(readme: str, section: str) -> str:
    """The README with its trophy section replaced."""
    at = readme.find(HEADING + "\n")
    if at < 0:
        raise SystemExit(f"{README}: no '{HEADING}' section to fill")

    # To the next top-level heading, so the ### tier headings stay inside.
    rest = readme[at + len(HEADING):]
    nxt = re.search(r"^## ", rest, re.M)
    end = at + len(HEADING) + (nxt.start() if nxt else len(rest))
    return readme[:at] + section + readme[end:]


def main(argv):
    check = "--check" in argv
    rows = read_trophies(SOURCE)

    with open(README, encoding="utf-8") as handle:
        readme = handle.read()
    wanted = splice(readme, render(rows))

    counts = "  ".join(
        f"{sum(1 for r in rows if r[3] == t)} {t.lower()}" for t in TIERS)
    print(f"{len(rows)} trophies: {counts}")

    if wanted == readme:
        print(f"{README} is up to date")
        return 0
    if check:
        print(f"{README} is out of date; run tools/gen_trophy_list.py",
              file=sys.stderr)
        return 1

    with open(README, "w", encoding="utf-8") as handle:
        handle.write(wanted)
    print(f"{README} rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
