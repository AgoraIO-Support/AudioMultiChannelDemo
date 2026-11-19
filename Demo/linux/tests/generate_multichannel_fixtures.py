#!/usr/bin/env python3
"""Generate deterministic multi-channel WAV fixtures for Linux regression.

Each channel has a distinct tone, so a content assertion can detect channel
copying, reordering, loss, or interleaving mistakes. The 24-bit fixture stores
the same signed 16-bit reference values left-shifted by eight bits.
"""

import argparse
import math
import struct
import wave
from pathlib import Path

SAMPLE_RATE = 48000
FREQUENCIES = [300 + 170 * i for i in range(8)]


def samples(channels: int, seconds: float, bits: int) -> bytes:
    count = int(SAMPLE_RATE * seconds)
    output = bytearray()
    for frame in range(count):
        for channel in range(channels):
            value = int(5000 * math.sin(2 * math.pi * FREQUENCIES[channel] * frame / SAMPLE_RATE))
            value += (channel + 1) * 37
            if bits == 16:
                output.extend(struct.pack("<h", value))
            else:
                value24 = value << 8
                output.extend(value24.to_bytes(3, "little", signed=True))
    return bytes(output)


def write_wav(path: Path, channels: int, seconds: float, bits: int) -> None:
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(bits // 8)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(samples(channels, seconds, bits))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=10.0)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    write_wav(args.output_dir / "8_Channel_ID_signed_16_bit_pcm.wav", 8, args.seconds, 16)
    write_wav(args.output_dir / "8_Channel_ID_24bit.wav", 8, args.seconds, 24)
    write_wav(args.output_dir / "4_Channel_ID_48k.wav", 4, args.seconds, 16)
    write_wav(args.output_dir / "6_Channel_ID_48k.wav", 6, args.seconds, 16)
    for channels in (3, 5, 7):
        write_wav(args.output_dir / f"odd_{channels}ch.wav", channels, args.seconds, 16)
    (args.output_dir / "8_Channel_ID_signed_16_bit_pcm.raw.pcm").write_bytes(
        samples(8, args.seconds, 16)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
