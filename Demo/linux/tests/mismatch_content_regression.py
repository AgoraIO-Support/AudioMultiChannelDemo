#!/usr/bin/env python3
import argparse
import json
import math
import re
import time
from collections import Counter
from pathlib import Path

from full_regression import (FREQUENCIES, SAMPLE_RATE, classify_mismatch_behavior, hann_spectrum,
                             launch, log_text, pcm_samples, spectral_peak, stop, wait_for,
                             write_tone_raw)


def analyze_mismatch_content(source_path: Path, received_path: Path, source_channels: int,
                             received_channels: int) -> dict:
    source = pcm_samples(source_path)
    received = pcm_samples(received_path)
    frames = len(received) // received_channels
    start = SAMPLE_RATE
    count = SAMPLE_RATE // 2
    if start + count > frames:
        return {"pass": False, "error": "not enough samples", "frames": frames}
    channels = []
    for channel in range(received_channels):
        values = [received[(start + index) * received_channels + channel]
                  for index in range(count)]
        rms = math.sqrt(sum(value * value for value in values) / count)
        amplitudes, fft_size = hann_spectrum(values)
        dominant_frequency, dominant_level = spectral_peak(amplitudes, fft_size, 1100, 1000)
        candidates = []
        for source_channel in range(source_channels):
            expected = FREQUENCIES[source_channel]
            tolerance = max(6, expected * 0.02)
            _, level = spectral_peak(amplitudes, fft_size, expected, tolerance)
            candidates.append((level, source_channel))
        strong_candidates = [(level, source_channel) for level, source_channel in candidates
                             if level > 20 and level >= dominant_level * 0.5]
        best_level, best_source = max(candidates)
        candidate_sources = [source_channel + 1 for _, source_channel in strong_candidates]
        matched_source = None
        if len(strong_candidates) == 1:
            expected = FREQUENCIES[best_source]
            tolerance = max(6, expected * 0.02)
            if abs(dominant_frequency - expected) <= tolerance:
                matched_source = best_source + 1
        channels.append({
            "channel": channel + 1,
            "rms": round(rms, 2),
            "dominant_frequency": round(dominant_frequency, 2),
            "dominant_level": round(dominant_level, 1),
            "matched_source": matched_source,
            "candidate_sources": candidate_sources,
            "match_level": round(best_level, 1),
        })
    matched = [item["matched_source"] for item in channels if item["matched_source"] is not None]
    counts = Counter(matched)
    duplicated = sorted(source for source, count in counts.items() if count > 1)
    missing = [source for source in range(1, source_channels + 1) if source not in counts]
    mixed = [item["channel"] for item in channels if len(item["candidate_sources"]) > 1]
    unmatched = [item["channel"] for item in channels
                 if not item["candidate_sources"] and item["rms"] >= 5]
    silent = [item["channel"] for item in channels
              if not item["candidate_sources"] and item["rms"] < 5]
    ordered = matched == sorted(matched)
    reasons = []
    if duplicated:
        reasons.append("duplicated_source_channels")
    if mixed:
        reasons.append("mixed_source_channels")
    if unmatched:
        reasons.append("unmatched_non_silent_channels")
    if not ordered:
        reasons.append("reordered_source_channels")
    if received_channels > source_channels and missing:
        reasons.append("missing_source_channels")
    if received_channels <= source_channels and (silent or len(matched) != received_channels):
        reasons.append("missing_received_channels")
    return {
        "pass": not reasons,
        "reasons": reasons,
        "duplicated_source_channels": duplicated,
        "missing_source_channels": missing,
        "unmatched_channels": unmatched,
        "mixed_channels": mixed,
        "silent_channels": silent,
        "ordered": ordered,
        "matched_sequence": matched,
        "channels": channels,
    }


def mismatch_content_case(root: Path, result_root: Path, app_id: str, source_channels: int,
                          received_channels: int) -> dict:
    case = f"mismatch-content-{source_channels}to{received_channels}"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    source = case_dir / "source.pcm"
    write_tone_raw(source, source_channels)
    channel = f"mismatch-content-{source_channels}-{received_channels}-{time.time_ns()}"
    receiver = sender = None
    output = case_dir / "output.uid-86001"
    try:
        receiver = launch(root, result_root, case, "receiver", "sample_receive_pcm_multiopus",
                          ["--channelId", channel, "--userId", "86002", "--remoteUserId", "86001",
                           "--numOfChannels", str(received_channels), "--sampleRate", "48000",
                           "--audioFile", str(case_dir / "output")], app_id)
        wait_for(lambda: "Start receiving" in log_text(receiver), [receiver], 30,
                 "mismatch content receiver")
        sender = launch(root, result_root, case, "sender", "sample_send_pcm_multiopus",
                        ["--channelId", channel, "--userId", "86001", "--numOfChannels",
                         str(source_channels), "--sampleRate", "48000", "--audioFile", str(source)],
                        app_id)
        target_bytes = received_channels * 2 * SAMPLE_RATE * 2
        wait_for(lambda: output.exists() and output.stat().st_size >= target_bytes,
                 [receiver, sender], 45, "mismatch content PCM")
        receiver_code, sender_code = stop(receiver), stop(sender)
        text = log_text(receiver)
        metadata = re.findall(r"PCM_FRAME_METADATA .*?channels=(\d+) .*?sampleRate=(\d+)", text)
        behavior = classify_mismatch_behavior(text, receiver_code, output.exists(), metadata,
                                               received_channels)
        content = (analyze_mismatch_content(source, output, source_channels, received_channels)
                   if behavior == "degraded_to_requested_channels" else {})
        passed = sender_code == 0 and (behavior == "rejected" or
                                      (behavior == "degraded_to_requested_channels" and content["pass"]))
        return {"case": case, "pass": passed, "detail": {
            "behavior": behavior, "receiver_exit": receiver_code, "sender_exit": sender_code,
            "metadata": metadata[:2], "bytes": output.stat().st_size if output.exists() else 0,
            "content": content,
        }}
    except Exception as exc:
        return {"case": case, "pass": False, "detail": {"error": str(exc)}}
    finally:
        stop(receiver)
        stop(sender)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample-dir", type=Path, required=True)
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--pairs", help="comma-separated source/receiver pairs, for example 8to3,3to8")
    args = parser.parse_args()
    if args.pairs:
        pairs = [tuple(map(int, item.split("to"))) for item in args.pairs.split(",")]
    else:
        pairs = [(source, received) for source in range(3, 9)
                 for received in range(3, 9) if source != received]
    root = args.sample_dir.resolve()
    result_root = args.output_dir.resolve()
    result_root.mkdir(parents=True, exist_ok=True)
    results = []
    for source_channels, received_channels in pairs:
        item = mismatch_content_case(root, result_root, args.app_id, source_channels, received_channels)
        results.append(item)
        (result_root / "mismatch-content-results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(("PASS " if item["pass"] else "FAIL ") + item["case"] + ": " +
              json.dumps(item["detail"], ensure_ascii=False), flush=True)
    passed = sum(item["pass"] for item in results)
    print(f"MISMATCH_CONTENT_SUMMARY {passed}/{len(results)} passed; artifacts={result_root}", flush=True)
    return 0 if results and passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
