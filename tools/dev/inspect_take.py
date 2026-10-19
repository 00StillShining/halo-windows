"""Prints the format, length and level of the newest (or given) halo take.

Usage: python tools/dev/inspect_take.py [path.wav]
"""

import glob
import math
import os
import sys
import wave

path = sys.argv[1] if len(sys.argv) > 1 else sorted(
    glob.glob(os.path.expanduser(r"~\Music\Halo\Recordings\*.wav")), key=os.path.getmtime)[-1]

with wave.open(path) as w:
    frames, rate, width, channels = w.getnframes(), w.getframerate(), w.getsampwidth(), w.getnchannels()
    data = w.readframes(frames)

full_scale = 2 ** (8 * width - 1)
peak, energy, count = 0, 0, 0
for i in range(0, len(data), width):
    v = int.from_bytes(data[i:i + width], "little", signed=True)
    peak = max(peak, abs(v))
    energy += v * v
    count += 1

db = lambda x: round(20 * math.log10(max(x, 1e-12)), 1)
print(os.path.basename(path))
print(f"  {channels} ch, {width * 8}-bit, {rate} Hz, {frames} frames = {frames / rate:.3f} s")
print(f"  peak {db(peak / full_scale)} dBFS, rms {db(math.sqrt(energy / max(count, 1)) / full_scale)} dBFS")
