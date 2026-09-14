#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Renders the app's sounds to a .wav, so they can be heard without a Switch.

A port of source/platform/audio.cpp - the same timbres, the same envelopes,
the same room - kept deliberately literal so the two can be compared line by
line. The point of it is the tuning loop: a build, a copy to the SD card and a
reboot is a slow way to find out that a tick is too bright.

    tools/sfx_preview.py                 every sound, in order, into sfx.wav
    tools/sfx_preview.py --sound coin    just that one
    tools/sfx_preview.py --out /tmp/x.wav

If the numbers here and the numbers in audio.cpp ever disagree, audio.cpp is
the one that is right.
"""

import argparse
import math
import struct
import wave

RATE = 48000

# ---------------------------------------------------------------- timbres
# (partials, attack seconds, one-pole tilt)
TIMBRES = {
    "bell": ([(1.00, 0.60), (2.01, 0.24), (3.02, 0.11), (5.43, 0.05)], 0.003, 0.55),
    "wood": ([(1.00, 0.74), (3.92, 0.19), (9.10, 0.07)], 0.002, 0.45),
    "soft": ([(1.00, 0.90), (2.00, 0.10)], 0.012, 0.35),
    "body": ([(1.00, 0.88), (1.50, 0.12)], 0.002, 0.25),
    "air": ([(1.00, 1.00)], 0.001, 0.18),
}

# ----------------------------------------------------------------- sounds
# (delay, pitch, bend, decay, gain, timbre)
SOUNDS = {
    "move": [(0.000, 1760.0, 0, 0.055, 0.16, "wood")],
    "select": [(0.000, 1046.5, 0, 0.090, 0.22, "wood"),
               (0.045, 1568.0, 0, 0.130, 0.17, "wood")],
    "back": [(0.000, 784.0, 0, 0.100, 0.20, "wood"),
             (0.040, 523.3, 0, 0.140, 0.16, "wood")],
    "toast": [(0.000, 1046.5, 0, 0.500, 0.21, "bell"),
              (0.100, 1568.0, 0, 0.750, 0.17, "bell")],
    "coin": [(0.000, 1568.0, 0, 0.180, 0.20, "bell"),
             (0.055, 2349.3, 0, 0.320, 0.17, "bell")],
    "trophy": [(0.000, 1046.5, 0, 0.340, 0.19, "bell"),
               (0.090, 1318.5, 0, 0.340, 0.19, "bell"),
               (0.180, 1568.0, 0, 0.340, 0.19, "bell"),
               (0.270, 2093.0, 0, 0.950, 0.22, "bell")],
    "hit": [(0.000, 150.0, 70.0, 0.130, 0.32, "body"),
            (0.000, 0.0, 0, 0.050, 0.10, "air")],
    "crit": [(0.000, 190.0, 72.0, 0.190, 0.34, "body"),
             (0.000, 0.0, 0, 0.070, 0.13, "air"),
             (0.010, 1318.5, 0, 0.240, 0.11, "bell")],
    "heal": [(0.000, 880.0, 0, 0.360, 0.16, "soft"),
             (0.070, 1318.5, 0, 0.460, 0.13, "soft")],
    "fall": [(0.000, 330.0, 90.0, 0.460, 0.26, "body")],
    "win": [(0.000, 1046.5, 0, 0.300, 0.19, "bell"),
            (0.085, 1318.5, 0, 0.300, 0.19, "bell"),
            (0.170, 1568.0, 0, 0.300, 0.19, "bell"),
            (0.260, 2093.0, 0, 1.000, 0.22, "bell")],
    "lose": [(0.000, 392.0, 0, 0.420, 0.19, "soft"),
             (0.160, 293.7, 0, 0.780, 0.19, "soft")],
    "tick": [(0.000, 1318.5, 0, 0.045, 0.18, "wood")],
}
ORDER = ["move", "select", "back", "toast", "coin", "trophy", "hit", "crit",
         "heal", "fall", "win", "lose", "tick"]

MASTER = 2.8
ROOM_SEND, ROOM_FEED, ROOM_DAMP = 0.26, 0.30, 0.38


def render_note(note, frames):
    """One note into a mono float list `frames` long, dry."""
    delay, pitch, bend, decay, gain, timbre = note
    partials, attack, tilt = TIMBRES[timbre]
    out = [0.0] * frames
    start = int(delay * RATE)
    length = max(2, int(decay * RATE))
    step = math.exp(-6.9 / length)
    rise = max(1, min(int(attack * RATE), length // 3))

    env, phase, lp, seed = 1.0, 0.0, 0.0, 0x9E3779B9
    to = bend if bend > 0 else pitch
    for i in range(length):
        f = start + i
        if f >= frames:
            break
        if timbre == "air":
            seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
            white = ((seed >> 9) & 0xFFFF) / 32768.0 - 1.0
            lp += (white - lp) * tilt
            shape = lp
        else:
            u = i / length
            freq = pitch if pitch == to else pitch * (to / pitch) ** u
            phase = (phase + freq / RATE) % 1.0
            total = sum(math.sin(2 * math.pi * phase * mul) * g for mul, g in partials)
            lp += (total - lp) * tilt
            shape = lp
        out[f] += shape * env * min(1.0, i / rise) * gain
        env *= step
    return out


def render(name, tail=0.9):
    notes = SOUNDS[name]
    span = max(d + dec for d, _, _, dec, _, _ in notes)
    frames = int((span + tail) * RATE)

    dry = [0.0] * frames
    for note in notes:
        for i, v in enumerate(render_note(note, frames)):
            dry[i] += v

    # the room, exactly as the mixer does it
    room_l = [0.0] * 8192
    room_r = [0.0] * 8192
    lag_l, lag_r = int(0.071 * RATE), int(0.097 * RATE)
    at, damp_l, damp_r = 0, 0.0, 0.0
    left, right = [], []
    for value in dry:
        d = value * MASTER
        tail_l = room_l[(at - lag_l) % 8192]
        tail_r = room_r[(at - lag_r) % 8192]
        damp_l += (tail_l - damp_l) * ROOM_DAMP
        damp_r += (tail_r - damp_r) * ROOM_DAMP
        room_l[at] = d * ROOM_SEND + damp_r * ROOM_FEED
        room_r[at] = d * ROOM_SEND + damp_l * ROOM_FEED
        at = (at + 1) % 8192
        left.append(max(-1.0, min(1.0, d + tail_l)))
        right.append(max(-1.0, min(1.0, d + tail_r)))
    return left, right


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="sfx.wav")
    ap.add_argument("--sound", help="just one of them")
    ap.add_argument("--gap", type=float, default=0.35)
    args = ap.parse_args()

    names = [args.sound] if args.sound else ORDER
    left, right = [], []
    for name in names:
        if name not in SOUNDS:
            raise SystemExit("no such sound: %s" % name)
        l, r = render(name)
        peak = max(max(abs(v) for v in l), max(abs(v) for v in r))
        print("%-7s %5.2f s  peak %.2f%s"
              % (name, len(l) / RATE, peak, "  CLIPS" if peak >= 0.999 else ""))
        left += l + [0.0] * int(args.gap * RATE)
        right += r + [0.0] * int(args.gap * RATE)

    with wave.open(args.out, "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(RATE)
        f.writeframes(b"".join(
            struct.pack("<hh", int(a * 32000), int(b * 32000))
            for a, b in zip(left, right)))
    print("wrote %s" % args.out)


if __name__ == "__main__":
    main()
