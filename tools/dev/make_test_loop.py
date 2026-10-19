"""Writes a 2-bar, 120 BPM drum-like test loop (16-bit stereo WAV, 44.1 kHz)
for exercising EDIT and CHOP without real recordings.

Usage: python tools/dev/make_test_loop.py build/test-loop.wav
"""

import math
import random
import struct
import sys
import wave

SR = 44100
BPM = 120
BEATS = 8
random.seed(7)

frames = int(SR * 60 / BPM * BEATS)
left = [0.0] * frames
right = [0.0] * frames


def hit(start, kind):
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
        left[i] += v
        right[i] += v * (0.8 if kind == "hat" else 1.0)


eighth = int(SR * 60 / BPM / 2)
for step in range(BEATS * 2):
    pos = step * eighth
    if step % 4 == 0:
        hit(pos, "kick")
    elif step % 4 == 2:
        hit(pos, "snare")
    hit(pos, "hat")

with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    data = bytearray()
    for l, r in zip(left, right):
        data += struct.pack("<hh", int(max(-1, min(1, l)) * 32767), int(max(-1, min(1, r)) * 32767))
    w.writeframes(bytes(data))
print(f"wrote {sys.argv[1]}: {frames} frames, {frames / SR:.2f} s")
