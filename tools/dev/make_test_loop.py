"""Writes a drum-like test file (16-bit stereo WAV, 44.1 kHz) for exercising
EDIT and CHOP without real recordings.

Usage:
  python tools/dev/make_test_loop.py build/test-loop.wav          # a 2-bar, 120 BPM loop (4 s)
  python tools/dev/make_test_loop.py build/fixtures/longsong.wav 64
      # a 64 s "song" in sections (drums, held bass notes, pad swells, breaks),
      # so CHOP finds cuts of very different lengths. Deterministic.
"""

import math
import random
import struct
import sys
import wave

SR = 44100
BPM = 120
random.seed(7)

seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
frames = int(SR * seconds)
left = [0.0] * frames
right = [0.0] * frames


def hit(start, kind, gain=1.0):
    for k in range(int(SR * 0.25)):
        i = start + k
        if i >= frames:
            return
        t = k / SR
        if kind == "kick":
            v = 0.9 * math.exp(-t * 18) * math.sin(2 * math.pi * (55 + 120 * math.exp(-t * 40)) * t)
        elif kind == "snare":
            v = 0.6 * math.exp(-t * 25) * (random.uniform(-1, 1) * 0.8 + 0.2 * math.sin(2 * math.pi * 190 * t))
        else:  # hat
            v = 0.25 * math.exp(-t * 90) * random.uniform(-1, 1)
        left[i] += v * gain
        right[i] += v * gain * (0.8 if kind == "hat" else 1.0)


def tone(start, length, freq, gain, attack=0.01, release=0.3):
    n = int(SR * length)
    for k in range(n):
        i = start + k
        if i >= frames:
            return
        t = k / SR
        env = min(1.0, t / attack) * min(1.0, (length - t) / release)
        v = gain * env * (math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(2 * math.pi * freq * 2.01 * t))
        left[i] += v
        right[i] += v * 0.9


eighth = int(SR * 60 / BPM / 2)
if seconds <= 4.5:
    for step in range(int(frames / eighth)):
        pos = step * eighth
        if step % 4 == 0:
            hit(pos, "kick")
        elif step % 4 == 2:
            hit(pos, "snare")
        hit(pos, "hat")
else:
    # Sections of 8 s: drums, held bass, drums, pad swell, half-time, break...
    section = 8 * SR
    for s0 in range(0, frames, section):
        kind = (s0 // section) % 4
        if kind == 0:  # full drums
            for step in range(16):
                pos = s0 + step * eighth
                if step % 4 == 0:
                    hit(pos, "kick")
                elif step % 4 == 2:
                    hit(pos, "snare")
                hit(pos, "hat")
        elif kind == 1:  # one held bass note with a single kick: a long slice
            hit(s0, "kick")
            tone(s0, 7.6, 55.0, 0.35, attack=0.02, release=1.5)
        elif kind == 2:  # sparse half-time
            for step in range(0, 16, 4):
                hit(s0 + step * eighth, "kick" if step % 8 == 0 else "snare", 0.9)
        else:  # a pad swell after two hits
            hit(s0, "kick")
            hit(s0 + 2 * eighth, "snare")
            tone(s0 + 4 * eighth, 6.5, 220.0, 0.12, attack=2.5, release=2.0)

with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    data = bytearray()
    for l, r in zip(left, right):
        data += struct.pack("<hh", int(max(-1, min(1, l)) * 32767), int(max(-1, min(1, r)) * 32767))
    w.writeframes(bytes(data))
print(f"wrote {sys.argv[1]}: {frames} frames, {frames / SR:.2f} s")
