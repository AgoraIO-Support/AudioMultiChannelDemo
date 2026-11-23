#!/usr/bin/env python3
import argparse
import json
import re
import shutil
import subprocess
import time
from pathlib import Path

from full_regression import (FREQUENCIES, SAMPLE_RATE, hann_spectrum, launch, log_text,
                             no_application_errors, pcm_samples, spectral_peak, stop,
                             tone_spectrum_stages, wait_for, write_tone_raw)


def analyze_mixed_output(path: Path, output_channels: int) -> dict:
    values = pcm_samples(path)
    frames = len(values) // output_channels
    start = SAMPLE_RATE
    count = SAMPLE_RATE // 2
    if start + count > frames:
        return {"pass": False, "error": "not enough samples", "frames": frames}
    expected_groups = ([0, 2, 4, 6], [1, 3, 5, 7]) if output_channels == 2 else (list(range(8)),)
    results = []
    for output_channel, expected in enumerate(expected_groups):
        channel_values = [values[(start + index) * output_channels + output_channel]
                          for index in range(count)]
        amplitudes, fft_size = hann_spectrum(channel_values)
        expected_levels = {}
        for source in expected:
            frequency = FREQUENCIES[source]
            _, level = spectral_peak(amplitudes, fft_size, frequency, max(6, frequency * 0.02))
            expected_levels[str(frequency)] = round(level, 1)
        other_levels = {}
        for source in range(8):
            if source in expected:
                continue
            frequency = FREQUENCIES[source]
            _, level = spectral_peak(amplitudes, fft_size, frequency, max(6, frequency * 0.02))
            other_levels[str(frequency)] = round(level, 1)
        missing = [frequency for frequency, level in expected_levels.items() if level <= 20]
        isolation_threshold = max(20, min(expected_levels.values()) * 0.2)
        leaking = {frequency: level for frequency, level in other_levels.items()
                   if level > isolation_threshold}
        passed = not missing and not leaking
        results.append({"channel": output_channel + 1, "pass": passed,
                        "expected_levels": expected_levels, "other_levels": other_levels,
                        "missing_frequencies": missing, "leaking_frequencies": leaking,
                        "isolation_threshold": round(isolation_threshold, 1)})
    return {"pass": all(item["pass"] for item in results), "channels": results}


def assert_pcm_trace(text: str, user: str) -> tuple[bool, dict]:
    pattern = re.compile(
        rf"PCM_FRAME_TRACE user={re.escape(user)} frame=(\d+) samples=(\d+) "
        rf"rtpTimestamp=(\d+) renderTimeMs=(-?\d+)"
    )
    frames = [(int(frame), int(samples), int(rtp), int(render))
              for frame, samples, rtp, render in pattern.findall(text)]
    if len(frames) < 2:
        return False, {"error": "not enough PCM frame traces", "frames_logged": len(frames)}
    deltas = [(frames[index][2] - frames[index - 1][2]) & 0xffffffff
              for index in range(1, len(frames))]
    order_ok = all(delta <= 0x7fffffff for delta in deltas)
    render_order_ok = all(frames[index][3] >= frames[index - 1][3]
                          for index in range(1, len(frames)))
    samples_ok = all(frame[1] == SAMPLE_RATE // 100 for frame in frames)
    warmup_ok = not deltas or deltas[0] <= 3840
    steady_ok = all(delta <= 1920 for delta in deltas[1:])
    gaps = [delta for delta in deltas[1:] if delta > 1920]
    continuity_ok = order_ok and render_order_ok and samples_ok and warmup_ok and steady_ok
    return continuity_ok, {
        "frames_logged": len(frames), "order_ok": order_ok,
        "render_order_ok": render_order_ok, "samples_ok": samples_ok,
        "warmup_ok": warmup_ok, "steady_ok": steady_ok,
        "continuity_ok": continuity_ok, "delivery_gap_count": len(gaps),
        "duplicate_timestamp_count": deltas.count(0),
        "nonuniform_delta_count": sum(delta != SAMPLE_RATE // 100 for delta in deltas),
        "max_delivery_delta": max(deltas, default=0),
        "wrapped_timestamp": any(frames[index][2] < frames[index - 1][2]
                                 for index in range(1, len(frames))),
    }


def mixed_case(root: Path, result_root: Path, app_id: str) -> list[dict]:
    case = "mixed-semantics"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    source = case_dir / "source.pcm"
    write_tone_raw(source, 8)
    channel = f"mixed-semantics-{time.time_ns()}"
    sender = None
    receivers = {}
    outputs = {"stereo": case_dir / "stereo", "mono": case_dir / "mono"}
    try:
        for name, channels in (("stereo", 2), ("mono", 1)):
            receivers[name] = launch(root, result_root, case, name, "sample_receive_mixed_audio",
                                     ["--channelId", channel, "--userId", "88002" if name == "stereo" else "88003",
                                      "--numOfChannels", str(channels), "--sampleRate", "48000",
                                      "--audioFile", str(outputs[name])], app_id)
        wait_for(lambda: all("Start receiving" in log_text(item) for item in receivers.values()),
                 list(receivers.values()), 30, "mixed receivers")
        sender = launch(root, result_root, case, "sender", "sample_send_pcm_multiopus",
                        ["--channelId", channel, "--userId", "88001", "--numOfChannels", "8",
                         "--sampleRate", "48000", "--audioFile", str(source)], app_id)
        wait_for(lambda: all(path.exists() and path.stat().st_size >= channels * 2 * SAMPLE_RATE * 2
                             for path, channels in ((outputs["stereo"], 2), (outputs["mono"], 1))),
                 list(receivers.values()) + [sender], 45, "mixed media")
        codes = {name: stop(item) for name, item in receivers.items()}
        sender_code = stop(sender)
        stereo = analyze_mixed_output(outputs["stereo"], 2)
        mono = analyze_mixed_output(outputs["mono"], 1)
        errors = no_application_errors(list(receivers.values()) + [sender])
        return [
            {"case": "mixed-stereo-isolation", "pass": stereo["pass"] and not errors and
             codes["stereo"] == 0 and sender_code == 0, "detail": stereo},
            {"case": "mixed-mono-all-sources", "pass": mono["pass"] and not errors and
             codes["mono"] == 0 and sender_code == 0, "detail": mono},
        ]
    except Exception as exc:
        return [{"case": case, "pass": False, "detail": {"error": str(exc)}}]
    finally:
        for item in receivers.values():
            stop(item)
        stop(sender)


def continuity_case(root: Path, result_root: Path, app_id: str) -> dict:
    case = "pcm-av-continuity"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    source = case_dir / "source.pcm"
    write_tone_raw(source, 8)
    channel = f"pcm-av-continuity-{time.time_ns()}"
    sender = None
    receivers = {}
    outputs = {"pcm": case_dir / "pcm.uid-89001", "av": case_dir / "avpcm.uid-89001"}
    try:
        receivers["pcm"] = launch(root, result_root, case, "pcm", "sample_receive_pcm_multiopus",
                                  ["--channelId", channel, "--userId", "89002", "--remoteUserId", "89001",
                                   "--numOfChannels", "8", "--sampleRate", "48000", "--traceFrames", "1",
                                   "--audioFile", str(case_dir / "pcm")], app_id)
        receivers["av"] = launch(root, result_root, case, "av", "sample_receive_av_multiopus",
                                 ["--channelId", channel, "--userId", "89003", "--remoteUserId", "89001",
                                  "--numOfChannels", "8", "--sampleRate", "48000", "--traceFrames", "1",
                                  "--audioFile", str(case_dir / "avpcm"),
                                  "--videoFile", str(case_dir / "video")], app_id)
        wait_for(lambda: all("Start receiving" in log_text(item) for item in receivers.values()),
                 list(receivers.values()), 30, "continuity receivers")
        sender = launch(root, result_root, case, "sender", "sample_send_pcm_multiopus",
                        ["--channelId", channel, "--userId", "89001", "--numOfChannels", "8",
                         "--sampleRate", "48000", "--audioFile", str(source)], app_id)
        wait_for(lambda: all(path.exists() and path.stat().st_size >= 8 * 2 * SAMPLE_RATE * 2
                             for path in outputs.values()),
                 list(receivers.values()) + [sender], 45, "continuity media")
        codes = {name: stop(item) for name, item in receivers.items()}
        sender_code = stop(sender)
        pcm_ok, pcm_detail = assert_pcm_trace(log_text(receivers["pcm"]), "89001")
        av_ok, av_detail = assert_pcm_trace(log_text(receivers["av"]), "89001")
        pcm_spectrum_ok, pcm_spectrum = tone_spectrum_stages(outputs["pcm"], 8)
        av_spectrum_ok, av_spectrum = tone_spectrum_stages(outputs["av"], 8)
        errors = no_application_errors(list(receivers.values()) + [sender])
        passed = (pcm_ok and av_ok and pcm_spectrum_ok and av_spectrum_ok and not errors and
                  sender_code == 0 and all(code == 0 for code in codes.values()))
        return {"case": case, "pass": passed, "detail": {
            "pcm": pcm_detail, "av": av_detail,
            "pcm_spectrum_pass": pcm_spectrum_ok, "av_spectrum_pass": av_spectrum_ok,
            "pcm_spectrum": pcm_spectrum, "av_spectrum": av_spectrum,
            "exit_codes": list(codes.values()) + [sender_code], "application_errors": errors,
        }}
    except Exception as exc:
        return {"case": case, "pass": False, "detail": {"error": str(exc)}}
    finally:
        for item in receivers.values():
            stop(item)
        stop(sender)


def write_synthetic_mkv(path: Path) -> None:
    pairs = ((300, 470), (640, 810), (980, 1150), (1320, 1490))
    command = ["ffmpeg", "-y"]
    for left, right in pairs:
        expression = f"0.15*sin(2*PI*{left}*t)|0.15*sin(2*PI*{right}*t):s=44100:d=8"
        command += ["-f", "lavfi", "-i", f"aevalsrc={expression}"]
    command += ["-f", "lavfi", "-i", "testsrc2=size=320x240:rate=25:duration=8",
                "-map", "4:v", "-map", "0:a", "-map", "1:a", "-map", "2:a", "-map", "3:a",
                "-c:v", "mpeg4", "-c:a", "ac3", "-ar", "44100", "-ac", "2", str(path)]
    completed = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if completed.returncode != 0:
        raise RuntimeError("failed to generate synthetic MKV: " + completed.stdout + completed.stderr)


def mkv_mapping_case(root: Path, result_root: Path, app_id: str) -> dict:
    case = "mkv-four-stereo-mapping"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    source = case_dir / "four-stereo.mkv"
    write_synthetic_mkv(source)
    channel = f"mkv-four-stereo-{time.time_ns()}"
    receiver = sender = None
    output = case_dir / "output.uid-81001"
    try:
        receiver = launch(root, result_root, case, "receiver", "sample_receive_pcm_multiopus",
                          ["--channelId", channel, "--userId", "81002", "--remoteUserId", "81001",
                           "--numOfChannels", "8", "--sampleRate", "48000",
                           "--audioFile", str(case_dir / "output")], app_id)
        wait_for(lambda: "Start receiving" in log_text(receiver), [receiver], 30, "MKV mapping receiver")
        sender = launch(root, result_root, case, "sender", "sample_send_mkv_multiopus",
                        ["--channelId", channel, "--userId", "81001", "--videoFile", str(source)], app_id)
        wait_for(lambda: output.exists() and output.stat().st_size >= 8 * 2 * SAMPLE_RATE * 2,
                 [receiver, sender], 60, "MKV mapping PCM")
        receiver_code, sender_code = stop(receiver), stop(sender)
        spectral_ok, spectral = tone_spectrum_stages(output, 8)
        passed = (spectral_ok and receiver_code == 0 and sender_code == 0 and
                  output.stat().st_size % 16 == 0)
        return {"case": case, "pass": passed, "detail": {
            "exit_codes": [receiver_code, sender_code], "aligned": output.stat().st_size % 16 == 0,
            "spectrum": spectral,
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
    parser.add_argument("--skip-mixed", action="store_true")
    parser.add_argument("--skip-continuity", action="store_true")
    parser.add_argument("--skip-mkv", action="store_true")
    args = parser.parse_args()
    root = args.sample_dir.resolve()
    result_root = args.output_dir.resolve()
    result_root.mkdir(parents=True, exist_ok=True)
    results = []
    if not args.skip_mixed:
        results.extend(mixed_case(root, result_root, args.app_id))
    if not args.skip_continuity:
        results.append(continuity_case(root, result_root, args.app_id))
    if not args.skip_mkv:
        results.append(mkv_mapping_case(root, result_root, args.app_id))
    (result_root / "semantic-results.json").write_text(json.dumps(results, indent=2) + "\n")
    for item in results:
        print(("PASS " if item["pass"] else "FAIL ") + item["case"] + ": " +
              json.dumps(item["detail"], ensure_ascii=False), flush=True)
    passed = sum(item["pass"] for item in results)
    print(f"SEMANTIC_SUMMARY {passed}/{len(results)} passed; artifacts={result_root}", flush=True)
    return 0 if results and passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
