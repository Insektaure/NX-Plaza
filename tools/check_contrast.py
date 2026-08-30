#!/usr/bin/env python3
"""
Check both palettes in source/ui/theme.cpp for readable contrast.

The light and dark themes are not each other's inverse -- amber that glows on
near-black turns muddy on paper, and a colour that carries text has to hold a
contrast ratio that a colour used only for a glow does not. This reads the real
values out of the source and checks every foreground/background pair the
interface actually draws, so a palette edit cannot quietly make text
unreadable.

    python3 tools/check_contrast.py

Exits non-zero if any pair falls under its bar. Stdlib only.
"""

from __future__ import annotations

import os
import re
import sys

THEME = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     os.pardir, "source", "ui", "theme.cpp")

# (foreground, background, what it is on screen, the ratio it has to clear)
#
# 4.5 is the WCAG AA bar for normal text, 3.0 for large text and for a
# non-text element that still has to be distinguishable. Tokens that only ever
# appear as a wash (accentTint, accentGlow, the strokes) are not listed: they
# carry no information on their own.
PAIRS = [
    ("fg1", "bg0", "body text on the page", 4.5),
    ("fg1", "bg1", "body text on the rail", 4.5),
    ("fg1", "bg2", "body text on a card", 4.5),
    ("fg1", "bg3", "body text on a focused row", 4.5),
    ("fg2", "bg2", "secondary text on a card", 4.5),
    ("fg3", "bg2", "meta text on a card", 3.0),
    ("fg3", "bg0", "meta text on the page", 3.0),
    ("fg4", "bg2", "faint text on a card", 2.0),
    ("accent", "bg0", "eyebrow label on the page", 4.5),
    ("accent", "bg2", "accent text on a card", 4.5),
    ("accent", "bg1", "selected rail icon", 3.0),
    ("teal", "bg2", "the teal 'new' flag", 3.0),
    ("danger", "bg2", "a danger label", 4.5),
    ("bg0", "accent", "text on an accent fill", 4.5),
    ("bg0", "teal", "text on the 'new' pill", 4.5),
    ("bg0", "danger", "text on the danger button", 4.5),
    ("mark", "bg1", "the lantern mark on the rail", 1.5),
]

CARD_TINT_BAR = 3.0


def read_palettes(path: str) -> dict[str, dict]:
    with open(path, encoding="utf-8") as handle:
        source = handle.read()

    palettes = {}
    for name in ("kLight", "kDark"):
        marker = f"const Palette {name} = {{"
        if marker not in source:
            raise SystemExit(f"{path}: could not find {name}")
        start = source.index(marker)
        end = source.index("};", start)
        body = source[start:end]

        fields = {}
        for field, value, alpha in re.findall(
                r"/\*\s*(\w+)\s*\*/\s*Color::hex\(0x([0-9A-Fa-f]{6})(?:\s*,\s*([0-9.]+)f)?\)",
                body):
            fields[field] = (int(value, 16), float(alpha) if alpha else 1.0)

        fields["_cards"] = [
            (label, int(value, 16))
            for label, value in re.findall(r'\{\s*"([^"]+)",\s*Color::hex\(0x([0-9A-Fa-f]{6})\)',
                                           body)
        ]
        palettes[name] = fields
    return palettes


def channels(value: int) -> tuple[float, float, float]:
    return ((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255


def composite(colour: tuple[int, float], background: tuple[int, float]) -> tuple[float, ...]:
    """Flatten a translucent token onto an opaque surface, the way the GPU does."""
    (rgb, alpha), (back_rgb, _) = colour, background
    front, back = channels(rgb), channels(back_rgb)
    return tuple(front[i] * alpha + back[i] * (1.0 - alpha) for i in range(3))


def luminance(rgb: tuple[float, ...]) -> float:
    def linear(value: float) -> float:
        return value / 12.92 if value <= 0.03928 else ((value + 0.055) / 1.055) ** 2.4

    red, green, blue = (linear(channel) for channel in rgb)
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def ratio(foreground: tuple[int, float], background: tuple[int, float]) -> float:
    front = luminance(composite(foreground, background))
    back = luminance(composite(background, background))
    high, low = max(front, back), min(front, back)
    return (high + 0.05) / (low + 0.05)


def main() -> int:
    palettes = read_palettes(THEME)
    failures = 0

    for name, palette in palettes.items():
        print(f"\n=== {name} ===")
        for foreground, background, description, bar in PAIRS:
            missing = [f for f in (foreground, background) if f not in palette]
            if missing:
                print(f"  ??  {', '.join(missing)} is not in {name}")
                failures += 1
                continue

            value = ratio(palette[foreground], palette[background])
            passed = value >= bar
            failures += 0 if passed else 1
            print(f"  {'ok ' if passed else 'LOW'} {value:5.2f}:1 (needs {bar:.1f})  "
                  f"{foreground} on {background} - {description}")

        print("  card tints on a card:")
        for label, tint in palette["_cards"]:
            value = ratio((tint, 1.0), palette["bg2"])
            passed = value >= CARD_TINT_BAR
            failures += 0 if passed else 1
            print(f"  {'ok ' if passed else 'LOW'} {value:5.2f}:1 (needs {CARD_TINT_BAR:.1f})  "
                  f"{label}")

    if failures:
        print(f"\n{failures} pair(s) under the bar.")
        return 1

    print("\nEvery pair clears its bar in both palettes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
