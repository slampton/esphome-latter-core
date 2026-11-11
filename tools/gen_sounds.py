#!/usr/bin/env python3
"""Render Latter's chimes and game sounds to sounds/<rate>/<id>.wav.

Latter plays its sounds as embedded audio files through latter_sound_player at the board's output rate
(${audio_out_rate}), so the device never resamples (a full-duplex I2S speaker only accepts its configured
format, and ESPHome's rtttl synthesizes at a fixed 16 kHz). The melodies are RTTTL strings and are rendered
with the same algorithm as esphome/components/rtttl (2026.11): full-scale sine, the same note table, notes
rounded up to whole cycles, a 10 ms gap between repeated notes, and a 2 ms fade at each note edge.

Usage:
  python tools/gen_sounds.py            # write sounds/48000
  python tools/gen_sounds.py --check    # exit 1 if any committed file differs from a fresh render
"""

from __future__ import annotations

import argparse
import io
import math
from pathlib import Path
import struct
import sys
import wave

RATES = (48000,)

# id -> RTTTL. Game sounds are sfx_<n>; play_game_sfx maps its sfx_id parameter to these ids.
SOUNDS: dict[str, str] = {
    "alarm": "alarm:d=16,o=6,b=160:c,e,g,c7,p,c,e,g,c7,p,c,e,g,c7",
    "find_my": "findmy:d=16,o=6,b=180:e,a,e7,a7,p,e,a,e7,a7,p,e,a,e7,a7",
    "sfx_1": "c1:d=16,o=5,b=260:g,c6,p,a,d6",
    "sfx_2": "c2:d=16,o=5,b=260:d6,g,p,c6,f",
    "sfx_3": "c3:d=16,o=5,b=270:f,c6,p,d6,g",
    "sfx_4": "c4:d=16,o=5,b=285:g,b,d6,p,a,c6",
    "sfx_5": "win:d=16,o=6,b=220:c,e,g,c7,p,g,8c7",
    "sfx_6": "sink:d=32,o=6,b=280:c,e,g,16c7",
    "sfx_7": "peg:d=32,o=6,b=320:a,e7",
    "sfx_8": "bow:d=32,o=5,b=320:g,d6",
    "sfx_9": "bull:d=16,o=6,b=260:e,g,c7,8e7",
    "sfx_10": "hit:d=32,o=5,b=280:a,e6",
    "sfx_11": "pad:d=32,o=5,b=320:f,a",
    "sfx_12": "brk:d=32,o=6,b=300:c,g",
    "sfx_13": "arm:d=32,o=5,b=320:a,e",
    "sfx_14": "lose:d=16,o=5,b=180:g,e,8c",
}

# esphome/components/rtttl NOTES (Tone.h): index 0 is a rest, then C4..B7.
NOTES = (
    0, 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494, 523, 554, 587, 622,
    659, 698, 740, 784, 831, 880, 932, 988, 1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661,
    1760, 1865, 1976, 2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951,
)
MIN_OCTAVE = 4
MAX_OCTAVE = 7
SEMITONES_IN_OCTAVE = 12
REPEATING_NOTE_GAP_MS = 10
NOTE_FADE_MS = 2
NOTE_INDEX = {"c": 1, "d": 3, "e": 5, "f": 6, "g": 8, "a": 10, "b": 12, "h": 12, "p": 0}


def _int_at(s: str, pos: int) -> tuple[int, int]:
    start = pos
    while pos < len(s) and s[pos].isdigit():
        pos += 1
    return (int(s[start:pos]) if pos > start else 0), pos


def render(rtttl: str, rate: int) -> list[int]:
    """Render one RTTTL string to 16-bit mono samples at `rate`."""
    _name, header, body = rtttl.split(":", 2)
    defaults = {"d": 4, "o": 6, "b": 63}
    for part in header.split(","):
        key, _, value = part.strip().partition("=")
        if key in defaults and value:
            defaults[key] = int(value)
    wholenote_ms = 60 * 1000 * 4 // defaults["b"]
    fade = max(1, rate * NOTE_FADE_MS // 1000)

    samples: list[int] = []
    output_freq = 0
    pos = 0
    while pos < len(body):
        while pos < len(body) and body[pos] in ", ":
            pos += 1
        if pos >= len(body):
            break
        denom, pos = _int_at(body, pos)
        note_ms = wholenote_ms // (denom or defaults["d"])
        index_in_octave = NOTE_INDEX[body[pos].lower()]
        pos += 1
        if pos < len(body) and body[pos] == "#":
            index_in_octave += 1
            pos += 1
        scale, pos = _int_at(body, pos)
        scale = scale or defaults["o"]
        if not MIN_OCTAVE <= scale <= MAX_OCTAVE:
            raise ValueError(f"octave {scale} out of range in {rtttl!r}")
        if pos < len(body) and body[pos] == ".":
            note_ms += note_ms // 2
            pos += 1

        need_gap = False
        if index_in_octave == 0:
            output_freq = 0
        else:
            freq = NOTES[(scale - MIN_OCTAVE) * SEMITONES_IN_OCTAVE + index_in_octave]
            need_gap = freq == output_freq
            output_freq = freq

        count = rate * note_ms // 1000
        gap = rate * REPEATING_NOTE_GAP_MS // 1000 if need_gap else 0
        if output_freq == 0:
            samples.extend([0] * count)
            continue
        # Round the note up to whole cycles so it ends at a zero crossing (rtttl does the same in 22.10 fixed point)
        per_wave = (rate << 10) // output_freq
        count = ((((count << 10) // per_wave) + 1) * per_wave) >> 10
        for n in range(count):
            if n < gap:
                samples.append(0)
                continue
            value = 32767.0 * math.sin(2.0 * math.pi * output_freq * n / rate)
            edge = min(n - gap, count - 1 - n)
            if edge < fade:
                value *= edge / fade
            samples.append(int(round(value)))
    return samples


def to_wav(samples: list[int], rate: int) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    return buf.getvalue()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="verify committed files match a fresh render")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent / "sounds"
    stale: list[str] = []
    total = {rate: 0 for rate in RATES}
    for rate in RATES:
        out_dir = root / str(rate)
        out_dir.mkdir(parents=True, exist_ok=True)
        for sound_id, rtttl in SOUNDS.items():
            data = to_wav(render(rtttl, rate), rate)
            total[rate] += len(data)
            path = out_dir / f"{sound_id}.wav"
            if args.check:
                if not path.exists() or path.read_bytes() != data:
                    stale.append(str(path.relative_to(root.parent)))
            else:
                path.write_bytes(data)
    if args.check:
        expected_dirs = {str(r) for r in RATES}
        for child in sorted(root.iterdir()):
            if child.is_dir() and child.name not in expected_dirs:
                stale.append(f"unexpected rate directory: {child.relative_to(root.parent)}")
        if stale:
            print("Stale sounds (run python tools/gen_sounds.py):\n  " + "\n  ".join(stale))
            return 1
        print(f"sounds up to date ({len(SOUNDS)} per rate)")
        return 0
    for rate, size in total.items():
        print(f"sounds/{rate}: {len(SOUNDS)} files, {size / 1024:.0f} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
