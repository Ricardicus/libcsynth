#!/usr/bin/env python3
"""Generate an original three-recording sample instrument using only Python."""
import math
from pathlib import Path
import struct
import sys
import wave

folder = Path(sys.argv[1] if len(sys.argv) > 1 else "sample-demo")
folder.mkdir(parents=True, exist_ok=True)
rate = 48000
rows = ["csynth-samples 1", "# Base Hz, gain, loop start/end frames, filename"]
for name, hz, overtone in [("warm-a3.wav", 220, .3),
                          ("warm-a4.wav", 440, .2),
                          ("warm-a5.wav", 880, .1)]:
    data = bytearray()
    for frame in range(rate):
        phase = 2 * math.pi * hz * frame / rate
        value = .5 * math.sin(phase) + overtone * math.sin(2 * phase)
        data.extend(struct.pack("<h", round(value * 32767)))
    with wave.open(str(folder / name), "wb") as recording:
        recording.setnchannels(1)
        recording.setsampwidth(2)
        recording.setframerate(rate)
        recording.writeframes(data)
    rows.append(f'sample {hz} 1.00 0 {rate} "{name}"')
(folder / "warm.csamples").write_text("\n".join(rows) + "\n", encoding="utf-8")
print(folder / "warm.csamples")
