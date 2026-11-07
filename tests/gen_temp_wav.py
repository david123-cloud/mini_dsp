#!/usr/bin/env python3
"""Generate tests/temp.wav: 1 s of 440 Hz, mono, 16-bit PCM, 48000 Hz.

The file is 48000 frames * 2 bytes + a canonical 44-byte header = 96044 bytes.
Only the Python standard library is used; Python is not a runtime dependency of
the DSP code, it just produces a reproducible test signal.

The existing file is never overwritten unless --force is given.
"""

import argparse
import math
import os
import struct
import sys

SAMPLE_RATE = 48000
FRAMES = 48000
FREQUENCY = 440.0
AMPLITUDE = 0.25  # ~25 % of full scale
CHANNELS = 1
BITS = 16
BYTES_PER_SAMPLE = BITS // 8
PEAK = AMPLITUDE * 32768.0  # 8192.0

EXPECTED_SIZE = 44 + FRAMES * CHANNELS * BYTES_PER_SAMPLE  # 96044


def build_wav_bytes() -> bytes:
    samples = bytearray()
    for n in range(FRAMES):
        value = int(round(PEAK * math.sin(2.0 * math.pi * FREQUENCY * n / SAMPLE_RATE)))
        value = max(-32768, min(32767, value))
        samples += struct.pack("<h", value)

    data_size = len(samples)
    block_align = CHANNELS * BYTES_PER_SAMPLE
    byte_rate = SAMPLE_RATE * block_align

    header = b"RIFF"
    header += struct.pack("<I", 36 + data_size)
    header += b"WAVE"
    header += b"fmt "
    header += struct.pack("<IHHIIHH", 16, 1, CHANNELS, SAMPLE_RATE, byte_rate, block_align, BITS)
    header += b"data"
    header += struct.pack("<I", data_size)

    return header + bytes(samples)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "temp.wav"),
        help="output file (default: tests/temp.wav next to this script)",
    )
    parser.add_argument("--force", action="store_true", help="overwrite an existing file")
    args = parser.parse_args()

    if os.path.exists(args.path) and not args.force:
        print(f"refusing to overwrite existing file: {args.path}", file=sys.stderr)
        print("pass --force to replace it", file=sys.stderr)
        return 1

    payload = build_wav_bytes()
    with open(args.path, "wb") as fp:
        fp.write(payload)

    size = os.path.getsize(args.path)
    print(f"wrote {args.path}")
    print(f"  {FRAMES} frames, {CHANNELS} ch, {SAMPLE_RATE} Hz, {BITS} bit")
    print(f"  {FREQUENCY} Hz sine, amplitude {AMPLITUDE} ({int(PEAK)} counts)")
    print(f"  size = {size} bytes (expected {EXPECTED_SIZE})")
    return 0 if size == EXPECTED_SIZE else 1


if __name__ == "__main__":
    sys.exit(main())
