"""Original looping stereo wind/surf; Python standard library only."""
import math
import random
import struct
import wave
from pathlib import Path

root = Path(__file__).resolve().parents[1]
rate = 24000
count = rate * 12
fade = rate // 2
rng = random.Random(0x534541)
channels = [[], []]
wind = 0.0
wash = [0.0, 0.0]
for i in range(count + fade):
    t = i / rate
    wind += .008 * (rng.uniform(-1, 1) - wind)
    for channel in range(2):
        wash[channel] += .19 * (rng.uniform(-1, 1) - wash[channel])
        swell = .25 + .75 * (.5 - .5 * math.cos(2 * math.pi * t / 4 + channel * .4)) ** 2
        channels[channel].append(.65 * wash[channel] * swell + 1.4 * wind)

loop = [values[fade:] for values in channels]
for channel in range(2):
    for i in range(fade):
        # Finish on the sample immediately preceding the start of the loop.
        blend = .5 - .5 * math.cos(math.pi * i / (fade - 1))
        loop[channel][count - fade + i] = (
            channels[channel][count + i] * (1 - blend)
            + channels[channel][i] * blend
        )
rms = math.sqrt(sum(v * v for ch in loop for v in ch) / (2 * count))
gain = .12 / rms
pcm = bytearray()
for i in range(count):
    for channel in range(2):
        value = max(-.95, min(.95, loop[channel][i] * gain))
        pcm.extend(struct.pack('<h', round(value * 32767)))
out = root / 'assets/audio/sea.wav'
out.parent.mkdir(exist_ok=True)
with wave.open(str(out), 'wb') as wav:
    wav.setparams((2, 2, rate, count, 'NONE', 'not compressed'))
    wav.writeframes(pcm)
print(out)
