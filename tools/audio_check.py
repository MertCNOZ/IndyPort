"""Checks a sound dump (XBE_AUDIO_DUMP, 48 kHz stereo float WAV): per half second the level, peak, clipped samples,
silent stretches inside sound and sample to sample jumps that sound as clicks.

Usage: python tools/audio_check.py out/audio.wav
"""
import array
import math
import struct
import sys


def main():
    data = open(sys.argv[1], "rb").read()
    rate = struct.unpack_from("<I", data, 24)[0]
    at = data.find(b"data") + 8
    samples = array.array("f")
    samples.frombytes(data[at:at + (len(data) - at) // 8 * 8])
    frames = len(samples) // 2
    print(f"{frames / rate:.1f} s at {rate} Hz")
    block = rate // 2
    for start in range(0, frames - block + 1, block):
        s = samples[start * 2:(start + block) * 2]
        peak = max(abs(v) for v in s)
        rms = math.sqrt(sum(v * v for v in s) / len(s))
        clipped = sum(1 for v in s if abs(v) >= 1.0)
        # a click: a jump between neighbours far above the block's usual step
        steps = [abs(s[i] - s[i - 2]) for i in range(2, len(s))]
        mean_step = sum(steps) / len(steps) or 1e-9
        clicks = sum(1 for d in steps if d > 0.25 and d > mean_step * 40)
        # holes: runs of 5+ ms of exact zeros while the block has sound
        holes, run = 0, 0
        for i in range(0, len(s), 2):
            if s[i] == 0.0 and s[i + 1] == 0.0:
                run += 1
            else:
                if run >= rate // 200 and rms > 0.01:
                    holes += 1
                run = 0
        left = math.sqrt(sum(v * v for v in s[0::2]) / block)
        right = math.sqrt(sum(v * v for v in s[1::2]) / block)
        flag = " CLIP" * (clipped > 0) + " CLICKS" * (clicks > 2) + " HOLES" * (holes > 0)
        print(f"{start / rate:6.1f}s rms {rms:.3f} (L {left:.3f} R {right:.3f}) peak {peak:.2f} clipped {clipped} "
              f"clicks {clicks} holes {holes}{flag}")


if __name__ == "__main__":
    main()
