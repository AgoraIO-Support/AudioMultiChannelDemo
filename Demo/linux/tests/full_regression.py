#!/usr/bin/env python3
"""Repeatable Linux multi-channel regression.

The script is intentionally self-contained so it can run in the Ubuntu
Podman image without a host Python environment. It keeps logs and media under
one result directory and checks media semantics in addition to exit codes.
"""

from __future__ import annotations

import argparse
import array
import hashlib
import json
import math
import os
import platform
import re
import signal
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

SAMPLE_RATE = 48000
FREQUENCIES = [300 + 170 * i for i in range(8)]
VIDEO_FRAME_SIZE = 1920 * 1080 * 3 // 2
RESULTS: list[dict] = []


def record(case: str, passed: bool, detail) -> None:
    item = {"case": case, "pass": bool(passed), "detail": detail}
    RESULTS.append(item)
    print(("PASS " if passed else "FAIL ") + case + ": " + str(detail), flush=True)


class Running:
    def __init__(self, proc: subprocess.Popen, log_file, log_path: Path, name: str):
        self.proc = proc
        self.log_file = log_file
        self.log_path = log_path
        self.name = name


def launch(root: Path, result_root: Path, case: str, name: str, binary: str,
           args: list[str], app_id: str) -> Running:
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    log_path = case_dir / f"{name}.log"
    sdk_dir = case_dir / f"{name}-sdk"
    sdk_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["AGORA_LOG_PATH"] = str(sdk_dir / "agorasdk.log")
    log_file = log_path.open("w")
    proc = subprocess.Popen(
        [str(root / binary), "--token", app_id] + args,
        cwd=root,
        env=env,
        stdout=log_file,
        stderr=subprocess.STDOUT,
    )
    return Running(proc, log_file, log_path, name)


def log_text(running: Running) -> str:
    try:
        return running.log_path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def wait_for(predicate, processes: list[Running], timeout: float, what: str) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        for running in processes:
            if running.proc.poll() is not None:
                raise RuntimeError(f"{running.name} exited {running.proc.returncode} while waiting for {what}")
        time.sleep(0.2)
    raise RuntimeError(f"timeout waiting for {what}")


def stop(running: Running | None) -> int | None:
    if running is None:
        return None
    if running.proc.poll() is None:
        running.proc.send_signal(signal.SIGINT)
    try:
        code = running.proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        running.proc.kill()
        code = running.proc.wait(timeout=5)
    if not running.log_file.closed:
        running.log_file.close()
    return code


def stop_all(processes: list[Running]) -> list[int | None]:
    return [stop(process) for process in processes]


def no_application_errors(processes: list[Running]) -> list[str]:
    errors = []
    for running in processes:
        text = log_text(running)
        if "[ APP_LOG_ERROR ]" in text or "spdlog fatal error" in text:
            errors.append(running.name)
    return errors


def write_tone_wav(path: Path, channels: int, bits: int, seconds: float = 8.0) -> None:
    frames = int(SAMPLE_RATE * seconds)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(bits // 8)
        output.setframerate(SAMPLE_RATE)
        data = bytearray()
        for frame in range(frames):
            for channel in range(channels):
                sample = int(5000 * math.sin(2 * math.pi * FREQUENCIES[channel] * frame / SAMPLE_RATE))
                sample += (channel + 1) * 37
                if bits == 16:
                    data.extend(struct.pack("<h", sample))
                else:
                    data.extend((sample << 8).to_bytes(3, "little", signed=True))
        output.writeframes(bytes(data))


def write_tone_raw(path: Path, channels: int, seconds: float = 8.0) -> None:
    frames = int(SAMPLE_RATE * seconds)
    with path.open("wb") as output:
        for frame in range(frames):
            for channel in range(channels):
                sample = int(5000 * math.sin(2 * math.pi * FREQUENCIES[channel] * frame / SAMPLE_RATE))
                sample += (channel + 1) * 37
                output.write(struct.pack("<h", sample))


def pcm_samples(path: Path) -> array.array:
    samples = array.array("h")
    samples.frombytes(path.read_bytes())
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


TONE_WINDOW_SAMPLES = SAMPLE_RATE // 2
TONE_STARTUP_START = SAMPLE_RATE // 4
TONE_STEADY_START = SAMPLE_RATE
TONE_CROSSTALK_MAX_RATIO = 0.2
_FFT_PLANS = {}
_HANN_WINDOWS = {}


def fft_plan(size: int):
    if size in _FFT_PLANS:
        return _FFT_PLANS[size]
    reversed_indices = []
    for index in range(size):
        reversed_index = 0
        value = index
        bits = size.bit_length() - 1
        for _ in range(bits):
            reversed_index = (reversed_index << 1) | (value & 1)
            value >>= 1
        reversed_indices.append(reversed_index)
    stages = []
    length = 2
    while length <= size:
        half = length // 2
        twiddles = [complex(math.cos(-2 * math.pi * index / length),
                            math.sin(-2 * math.pi * index / length))
                    for index in range(half)]
        stages.append((length, half, twiddles))
        length *= 2
    _FFT_PLANS[size] = (reversed_indices, stages)
    return _FFT_PLANS[size]


def fft(values: list[complex]) -> list[complex]:
    size = len(values)
    reversed_indices, stages = fft_plan(size)
    transformed = [values[index] for index in reversed_indices]
    for length, half, twiddles in stages:
        for block in range(0, size, length):
            for offset in range(half):
                even = block + offset
                odd = even + half
                first = transformed[even]
                second = transformed[odd] * twiddles[offset]
                transformed[even] = first + second
                transformed[odd] = first - second
    return transformed


def hann_spectrum(values: list[int]) -> tuple[list[float], int]:
    count = len(values)
    size = 1 << (count - 1).bit_length()
    if count not in _HANN_WINDOWS:
        _HANN_WINDOWS[count] = [0.5 - 0.5 * math.cos(2 * math.pi * index / (count - 1))
                                for index in range(count)]
    window = _HANN_WINDOWS[count]
    padded = [values[index] * window[index] for index in range(count)] + [0j] * (size - count)
    scale = 2 / sum(window)
    amplitudes = [abs(value) * scale for value in fft(padded)]
    return amplitudes, size


def spectral_peak(amplitudes: list[float], fft_size: int, center: float,
                  tolerance: float) -> tuple[float, float]:
    lower = max(100, center - tolerance)
    upper = min(2000, center + tolerance)
    first = max(1, int(math.floor(lower * fft_size / SAMPLE_RATE)))
    last = min(len(amplitudes) - 1, int(math.ceil(upper * fft_size / SAMPLE_RATE)))
    peak = max(range(first, last + 1), key=lambda index: amplitudes[index])
    return peak * SAMPLE_RATE / fft_size, amplitudes[peak]


def crosstalk_violations(values: array.array, channels: int, start: int,
                         count: int, channel: int) -> list[dict]:
    if channel == 0:
        return []
    violations = []
    subwindow = min(SAMPLE_RATE // 4, count)
    for substart in range(start, start + count - subwindow + 1, subwindow):
        channel_values = [values[(substart + index) * channels + channel]
                          for index in range(subwindow)]
        amplitudes, fft_size = hann_spectrum(channel_values)
        expected = FREQUENCIES[channel]
        _, level = spectral_peak(amplitudes, fft_size, expected, max(6, expected * 0.02))
        _, hz300_level = spectral_peak(amplitudes, fft_size, 300, 6)
        ratio = hz300_level / max(level, 1e-9)
        if ratio > TONE_CROSSTALK_MAX_RATIO:
            violations.append({"start_ms": round(substart * 1000 / SAMPLE_RATE, 1),
                               "hz300_ratio": round(ratio, 4),
                               "level": round(level, 1),
                               "hz300_level": round(hz300_level, 1)})
    return violations


def tone_spectrum_samples(values: array.array, channels: int, start: int,
                          count: int) -> tuple[bool, list[dict]]:
    frames = len(values) // channels
    if start < 0 or count <= 0 or start + count > frames:
        return False, [{"error": "not enough samples", "frames": frames,
                        "start_ms": round(start * 1000 / SAMPLE_RATE, 1),
                        "window_ms": round(count * 1000 / SAMPLE_RATE, 1)}]
    result = []
    for channel in range(channels):
        channel_values = [values[(start + index) * channels + channel] for index in range(count)]
        amplitudes, fft_size = hann_spectrum(channel_values)
        expected = FREQUENCIES[channel]
        tolerance = max(6, expected * 0.02)
        frequency, level = spectral_peak(amplitudes, fft_size, expected, tolerance)
        dominant_frequency, dominant_level = spectral_peak(amplitudes, fft_size, 1100, 1000)
        other_levels = []
        for other in range(channels):
            if other == channel:
                continue
            other_expected = FREQUENCIES[other]
            _, other_level = spectral_peak(amplitudes, fft_size, other_expected,
                                           max(6, other_expected * 0.02))
            other_levels.append(other_level)
        next_level = max(other_levels, default=0)
        hz300_frequency, hz300_level = spectral_peak(amplitudes, fft_size, 300, 6)
        hz300_ratio = hz300_level / max(level, 1e-9)
        expected_ok = level > 20
        dominance_ok = level > max(1, next_level) * 1.5
        violations = crosstalk_violations(values, channels, start, count, channel)
        crosstalk_ok = channel == 0 or (hz300_ratio <= TONE_CROSSTALK_MAX_RATIO and not violations)
        passed = expected_ok and dominance_ok and crosstalk_ok
        result.append({
            "channel": channel + 1,
            "expected_frequency": expected,
            "frequency": round(frequency, 2),
            "level": round(level, 1),
            "next_level": round(next_level, 1),
            "dominant_frequency": round(dominant_frequency, 2),
            "dominant_level": round(dominant_level, 1),
            "hz300_frequency": round(hz300_frequency, 2),
            "hz300_level": round(hz300_level, 1),
            "hz300_ratio": round(hz300_ratio, 4),
            "crosstalk_violations": violations,
            "expected_ok": expected_ok,
            "dominance_ok": dominance_ok,
            "crosstalk_ok": crosstalk_ok,
            "pass": passed,
        })
    return all(item["pass"] for item in result), result


def tone_spectrum(path: Path, channels: int, seconds: float = 0.5,
                  start_ms: int = 250) -> tuple[bool, list[dict]]:
    values = pcm_samples(path)
    start = int(SAMPLE_RATE * start_ms / 1000)
    count = int(SAMPLE_RATE * seconds)
    return tone_spectrum_samples(values, channels, start, count)


def tone_spectrum_stages(path: Path, channels: int) -> tuple[bool, dict]:
    values = pcm_samples(path)
    startup_ok, startup = tone_spectrum_samples(values, channels, TONE_STARTUP_START,
                                                TONE_WINDOW_SAMPLES)
    steady_ok, steady = tone_spectrum_samples(values, channels, TONE_STEADY_START,
                                              TONE_WINDOW_SAMPLES)
    return startup_ok and steady_ok, {
        "startup_pass": startup_ok,
        "steady_pass": steady_ok,
        "startup_start_ms": TONE_STARTUP_START * 1000 // SAMPLE_RATE,
        "steady_start_ms": TONE_STEADY_START * 1000 // SAMPLE_RATE,
        "window_ms": TONE_WINDOW_SAMPLES * 1000 // SAMPLE_RATE,
        "hz300_max_ratio": TONE_CROSSTALK_MAX_RATIO,
        "startup": startup,
        "steady": steady,
    }


def assert_encoded_log(text: str, user: str, expected_codec: int | None = None,
                       strict_continuity: bool = False) -> tuple[bool, dict]:
    pattern = re.compile(
        rf"Received encoded audio frame: user={re.escape(user)} .*?codec=(\d+) "
        rf"sendTs=(\d+) delta=(\d+)"
    )
    frames = [(int(codec), int(ts), int(delta)) for codec, ts, delta in pattern.findall(text)]
    if not frames:
        return False, {"error": "no encoded frame metadata", "user": user}
    codecs = sorted(set(codec for codec, _, _ in frames))
    timestamps = [ts for _, ts, _ in frames]
    intervals = [delta for _, _, delta in frames[1:]]
    order_error = re.search(rf"ENCODED_TIMESTAMP_ORDER user={re.escape(user)} ", text) is not None
    monotonic_ok = (not order_error and
                    all(current > previous for previous, current in zip(timestamps, timestamps[1:])))
    gap_pattern = re.compile(
        rf"ENCODED_TIMESTAMP_GAP user={re.escape(user)} previous=\d+ current=\d+ delta=(\d+)"
    )
    delivery_gaps = [int(delta) for delta in gap_pattern.findall(text)]
    # Only the strict transport gate requires gap-free pacing. The codec gate
    # still requires increasing timestamps but tolerates delivery gaps caused
    # by loss, reordering, or delayed packets.
    warmup_ok = not intervals or 0 < intervals[0] <= 3840
    steady_intervals = intervals[1:] if intervals else []
    steady_ok = all(0 < delta <= 1920 for delta in steady_intervals)
    continuity_ok = warmup_ok and steady_ok and not delivery_gaps
    codec_ok = expected_codec is None or codecs == [expected_codec]
    passed = codec_ok and monotonic_ok and (continuity_ok if strict_continuity else True)
    return passed, {"frames_logged": len(frames), "codecs": codecs,
                    "intervals": intervals[:10], "monotonic_ok": monotonic_ok,
                    "warmup_ok": warmup_ok, "steady_ok": steady_ok,
                    "continuity_ok": continuity_ok,
                    "delivery_gap_count": len(delivery_gaps),
                    "max_delivery_delta": max(delivery_gaps, default=0),
                    "strict_continuity": strict_continuity}


def assert_video(path: Path, text: str, user: str) -> tuple[bool, dict]:
    size = path.stat().st_size if path.exists() else 0
    frame_metadata = re.findall(
        rf"VIDEO_FRAME_METADATA uid={re.escape(user)} frame=(\d+) width=(\d+) height=(\d+) "
        rf"renderTimeMs=(-?\d+)", text
    )
    timestamps = [int(item[3]) for item in frame_metadata]
    first_two_differ = False
    if size >= 2 * VIDEO_FRAME_SIZE:
        with path.open("rb") as source:
            first = source.read(VIDEO_FRAME_SIZE)
            second = source.read(VIDEO_FRAME_SIZE)
        first_two_differ = hashlib.sha256(first).digest() != hashlib.sha256(second).digest()
    passed = (size >= 2 * VIDEO_FRAME_SIZE and size % VIDEO_FRAME_SIZE == 0
              and first_two_differ and len(timestamps) >= 2
              and timestamps == sorted(timestamps))
    return passed, {"bytes": size, "complete_frames": size % VIDEO_FRAME_SIZE == 0,
                    "first_two_differ": first_two_differ,
                    "timestamps": timestamps[:5]}


def matrix_case(root: Path, public: Path, result_root: Path, app_id: str,
                sender: str, source: Path, video: bool = False,
                strict_continuity: bool = False) -> None:
    case = f"matrix-{sender}"
    channel = f"full-reg-{sender}-{int(time.time())}"
    sender_uid = "81001"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    common = ["--channelId", channel]
    receivers = {
        "pcm": ("sample_receive_pcm_multiopus", ["--userId", "81002", "--remoteUserId", sender_uid,
                "--numOfChannels", "8", "--sampleRate", "48000", "--audioFile", str(case_dir / "pcm")]),
        "encoded": ("sample_receive_encoded_audio", ["--userId", "81003", "--audioFile", str(case_dir / "encoded")]),
        "mixed": ("sample_receive_mixed_audio", ["--userId", "81004", "--numOfChannels", "2",
                  "--sampleRate", "48000", "--audioFile", str(case_dir / "mixed")]),
        "av": ("sample_receive_av_multiopus", ["--userId", "81005", "--remoteUserId", sender_uid,
               "--numOfChannels", "8", "--sampleRate", "48000", "--audioFile", str(case_dir / "avpcm"),
               "--videoFile", str(case_dir / "video")]),
    }
    if strict_continuity:
        binary, args = receivers["encoded"]
        receivers["encoded"] = (binary, args + ["--strictTimestampContinuity", "1"])
    binaries = {"pcm": "sample_send_pcm_multiopus", "wav": "sample_send_wav_multiopus",
                "opus": "sample_send_opus8", "mkv": "sample_send_mkv_multiopus"}
    send_args = ["--userId", sender_uid]
    if sender == "mkv":
        send_args += ["--videoFile", str(source)]
    else:
        send_args += ["--audioFile", str(source)]
    if sender in ("pcm", "wav"):
        send_args += ["--numOfChannels", "8", "--sampleRate", "48000"]
    running: dict[str, Running] = {}
    try:
        for name, (binary, args) in receivers.items():
            running[name] = launch(root, result_root, case, name, binary, common + args, app_id)
        wait_for(lambda: all("Start receiving" in log_text(item) for item in running.values()),
                 list(running.values()), 30, "all receivers to connect")
        running["sender"] = launch(root, result_root, case, "sender", binaries[sender],
                                    common + send_args, app_id)
        wait_for(lambda: "Start sending" in log_text(running["sender"]),
                 list(running.values()), 120, "sender to start")
        output = {"pcm": case_dir / f"pcm.uid-{sender_uid}",
                  "encoded": case_dir / f"encoded.uid-{sender_uid}",
                  "mixed": case_dir / "mixed", "av": case_dir / f"avpcm.uid-{sender_uid}"}
        video_path = case_dir / f"video.uid-{sender_uid}"

        def ready() -> bool:
            return all(path.exists() and path.stat().st_size >= (4096 if name == "encoded" else 15360)
                       for name, path in output.items()) and (not video or
                       (video_path.exists() and video_path.stat().st_size >= 2 * VIDEO_FRAME_SIZE))

        wait_for(ready, list(running.values()), 60, "all media outputs")
        codes = {name: stop(item) for name, item in running.items()}
        for name, path in output.items():
            details = {"bytes": path.stat().st_size if path.exists() else 0,
                       "sender_exit": codes.get("sender"), "receiver_exit": codes.get(name)}
            good = path.exists() and details["bytes"] >= (4096 if name == "encoded" else 15360)
            good = good and codes.get(name) == 0 and codes.get("sender") == 0
            if name in ("pcm", "mixed", "av"):
                channels = 2 if name == "mixed" else 8
                good = good and details["bytes"] % (channels * 2) == 0
                metadata = log_text(running[name])
                good = good and "FRAME_METADATA" in metadata
            if name == "encoded":
                encoded_ok, encoded_detail = assert_encoded_log(
                    log_text(running[name]), sender_uid, 13, strict_continuity)
                details["encoded"] = encoded_detail
                good = good and encoded_ok
            if name == "av" and video:
                video_ok, video_detail = assert_video(video_path, log_text(running[name]), sender_uid)
                details["video"] = video_detail
                good = good and video_ok
            record(f"{sender}->{name}", good, details)
        errors = no_application_errors(list(running.values()))
        record(f"{case}-logs", not errors, errors or "no application/SDK errors")
    except Exception as exc:
        record(f"{case}-setup", False, str(exc))
    finally:
        for item in running.values():
            if item.proc.poll() is None:
                stop(item)


def tone_case(root: Path, result_root: Path, app_id: str, channels: int, bits: int) -> None:
    case = f"tone-{channels}ch-{bits}bit"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    fixture = case_dir / "tone.wav"
    write_tone_wav(fixture, channels, bits)
    channel = f"tone-{channels}-{bits}-{int(time.time())}"
    receiver = sender = None
    try:
        receiver = launch(root, result_root, case, "receiver", "sample_receive_pcm_multiopus",
                          ["--channelId", channel, "--userId", "82002", "--remoteUserId", "82001",
                           "--numOfChannels", str(channels), "--sampleRate", "48000",
                           "--audioFile", str(case_dir / "output")], app_id)
        wait_for(lambda: "Start receiving" in log_text(receiver), [receiver], 30, "tone receiver")
        sender = launch(root, result_root, case, "sender", "sample_send_wav_multiopus",
                        ["--channelId", channel, "--userId", "82001", "--numOfChannels", str(channels),
                         "--sampleRate", "48000", "--audioFile", str(fixture)], app_id)
        output = case_dir / "output.uid-82001"
        wait_for(lambda: output.exists() and output.stat().st_size >= channels * 2 * SAMPLE_RATE * 2,
                 [receiver, sender], 45, "tone PCM")
        receiver_code, sender_code = stop(receiver), stop(sender)
        spectral_ok, spectral = tone_spectrum_stages(output, channels)
        good = spectral_ok and receiver_code == 0 and sender_code == 0
        record(case, good, {"exit_codes": [receiver_code, sender_code], "spectrum": spectral})
    except Exception as exc:
        record(case, False, str(exc))
    finally:
        stop(receiver)
        stop(sender)


def odd_case(root: Path, public: Path, result_root: Path, app_id: str, channels: int) -> None:
    case = f"odd-{channels}ch"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    source = public / f"odd_{channels}ch.wav"
    channel = f"odd-{channels}-{int(time.time())}"
    receiver = sender = None
    try:
        receiver = launch(root, result_root, case, "receiver", "sample_receive_pcm_multiopus",
                          ["--channelId", channel, "--userId", "84002", "--remoteUserId", "84001",
                           "--numOfChannels", str(channels), "--sampleRate", "48000",
                           "--audioFile", str(case_dir / "output")], app_id)
        wait_for(lambda: "Start receiving" in log_text(receiver), [receiver], 30, "odd receiver")
        sender = launch(root, result_root, case, "sender", "sample_send_wav_multiopus",
                        ["--channelId", channel, "--userId", "84001", "--numOfChannels", str(channels),
                         "--sampleRate", "48000", "--audioFile", str(source)], app_id)
        output = case_dir / "output.uid-84001"
        wait_for(lambda: output.exists() and output.stat().st_size >= channels * 2 * SAMPLE_RATE * 2,
                 [receiver, sender], 45, "odd PCM")
        receiver_code, sender_code = stop(receiver), stop(sender)
        analyzer = subprocess.run(
            [str(root / "pcm_channel_analyzer"), "--source", str(source), "--received", str(output),
             "--channels", str(channels), "--sample-rate", "48000", "--max-seconds", "5",
             "--min-correlation", "0.2"], capture_output=True, text=True, timeout=30)
        spectral_ok, spectral = tone_spectrum_stages(output, channels)
        aligned = output.stat().st_size % (channels * 2) == 0
        good = spectral_ok and aligned and receiver_code == 0 and sender_code == 0
        record(case, good, {"exit_codes": [receiver_code, sender_code], "aligned": aligned,
                            "spectral": spectral, "analyzer_exit": analyzer.returncode,
                            "analyzer": (analyzer.stdout + analyzer.stderr)[-1000:]})
    except Exception as exc:
        record(case, False, str(exc))
    finally:
        stop(receiver)
        stop(sender)


def classify_mismatch_behavior(text: str, receiver_code: int, output_exists: bool,
                               metadata: list[tuple[str, str]], received_channels: int) -> str:
    if "PCM_FRAME_METADATA_MISMATCH" in text:
        return "rejected"
    if receiver_code != 0:
        return "error"
    if output_exists and metadata and int(metadata[0][0]) == received_channels:
        return "degraded_to_requested_channels"
    return "unknown"


def mismatch_case(root: Path, result_root: Path, app_id: str, source_channels: int,
                  received_channels: int) -> None:
    case = f"mismatch-{source_channels}to{received_channels}"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    source = case_dir / "source.pcm"
    write_tone_raw(source, source_channels)
    channel = f"mismatch-{source_channels}-{received_channels}-{int(time.time())}"
    receiver = sender = None
    try:
        receiver = launch(root, result_root, case, "receiver", "sample_receive_pcm_multiopus",
                          ["--channelId", channel, "--userId", "86002", "--remoteUserId", "86001",
                           "--numOfChannels", str(received_channels), "--sampleRate", "48000",
                           "--audioFile", str(case_dir / "output")], app_id)
        wait_for(lambda: "Start receiving" in log_text(receiver), [receiver], 30, "mismatch receiver")
        sender = launch(root, result_root, case, "sender", "sample_send_pcm_multiopus",
                        ["--channelId", channel, "--userId", "86001", "--numOfChannels", str(source_channels),
                         "--sampleRate", "48000", "--audioFile", str(source)], app_id)
        time.sleep(8)
        receiver_code, sender_code = stop(receiver), stop(sender)
        text = log_text(receiver)
        output = case_dir / "output.uid-86001"
        metadata = re.findall(r"PCM_FRAME_METADATA .*?channels=(\d+) .*?sampleRate=(\d+)", text)
        behavior = classify_mismatch_behavior(text, receiver_code, output.exists(), metadata,
                                               received_channels)
        passed = behavior in ("rejected", "degraded_to_requested_channels") and sender_code == 0
        record(case, passed, {"behavior": behavior, "receiver_exit": receiver_code,
                              "sender_exit": sender_code, "metadata": metadata[:2],
                              "bytes": output.stat().st_size if output.exists() else 0})
    except Exception as exc:
        record(case, False, str(exc))
    finally:
        stop(receiver)
        stop(sender)


def fallback_case(root: Path, public: Path, result_root: Path, app_id: str, unfallback: bool,
                  strict_continuity: bool = False) -> None:
    mode = "unfallback" if unfallback else "fallback"
    case = f"codec-{mode}"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    ordinary = case_dir / "ordinary.pcm"
    write_tone_raw(ordinary, 2)
    channel = f"codec-{mode}-{int(time.time())}"
    processes = []
    try:
        encoded_args = ["--channelId", channel, "--userId", "96001",
                        "--audioFile", str(case_dir / "encoded")]
        if strict_continuity:
            encoded_args += ["--strictTimestampContinuity", "1"]
        encoded = launch(root, result_root, case, "encoded", "sample_receive_encoded_audio",
                         encoded_args, app_id)
        processes.append(encoded)
        wait_for(lambda: "Start receiving" in log_text(encoded), processes, 30, "encoded receiver")
        ordinary_sender = launch(root, result_root, case, "ordinary", "sample_send_pcm_opus",
                                 ["--channelId", channel, "--userId", "96002", "--audioFile", str(ordinary),
                                  "--numOfChannels", "2", "--sampleRate", "48000"], app_id)
        processes.append(ordinary_sender)
        multi_args = ["--channelId", channel, "--userId", "96003", "--audioFile",
                      str(public / "8_Channel_ID_signed_16_bit_pcm.raw.pcm"),
                      "--numOfChannels", "8", "--sampleRate", "48000"]
        if unfallback:
            multi_args += ["--codecUnfallback", "1"]
        multi_sender = launch(root, result_root, case, "multi", "sample_send_pcm_multiopus",
                              multi_args, app_id)
        processes.append(multi_sender)
        wait_for(lambda: "Received encoded audio frame" in log_text(encoded), processes, 45,
                 "ordinary and multi encoded frames")
        time.sleep(3)
        codes = stop_all(processes)
        ordinary_ok, ordinary_detail = assert_encoded_log(
            log_text(encoded), "96002", 1, strict_continuity)
        multi_ok, multi_detail = assert_encoded_log(
            log_text(encoded), "96003", 13, strict_continuity)
        setting_seen = "Set che.audio.codec_unfallback=[123,124,125]" in log_text(multi_sender)
        passed = ordinary_ok and multi_ok and (setting_seen if unfallback else True) and all(code == 0 for code in codes)
        record(case, passed, {"ordinary": ordinary_detail, "multi": multi_detail,
                              "codec_unfallback_set": setting_seen, "exit_codes": codes,
                              "interpretation": "ordinary Opus remains codec=1 while the multi-channel peer is codec=13"})
    except Exception as exc:
        record(case, False, str(exc))
    finally:
        for process in processes:
            stop(process)


def negative_case(root: Path, result_root: Path, app_id: str, name: str, binary: str,
                  args: list[str], expected: str, timeout: int = 20) -> None:
    case_dir = result_root / "invalid-inputs"
    case_dir.mkdir(parents=True, exist_ok=True)
    command = [str(root / binary), "--token", app_id, "--channelId", f"invalid-{name}",
               "--userId", "83001"] + args
    try:
        completed = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=timeout)
        combined = completed.stdout + completed.stderr
        (case_dir / f"{name}.log").write_text(combined)
        connected = "onConnecting:" in combined
        passed = completed.returncode != 0 and expected in combined and not connected
        record(f"invalid-{name}", passed, {"exit": completed.returncode,
                                             "expected": expected, "connected": connected})
    except Exception as exc:
        record(f"invalid-{name}", False, str(exc))


def invalid_cases(root: Path, public: Path, result_root: Path, app_id: str) -> None:
    case_dir = result_root / "invalid-inputs"
    case_dir.mkdir(parents=True, exist_ok=True)
    bad_raw = case_dir / "partial.pcm"
    bad_raw.write_bytes(b"\0" * (480 * 8 * 2 + 1))
    bad_opus = case_dir / "partial.opus"
    bad_opus.write_bytes(b"\0" * 641)
    ogg = case_dir / "ogg.opus"
    ogg.write_bytes(b"OggS" + b"\0" * 640)
    malformed = case_dir / "malformed.wav"
    malformed.write_bytes(b"RIFF\x04\0\0\0WAVE")

    negative_case(root, result_root, app_id, "pcm-channels-low", "sample_send_pcm_multiopus",
                  ["--audioFile", str(public / "8_Channel_ID_signed_16_bit_pcm.raw.pcm"), "--numOfChannels", "2"],
                  "numOfChannels must be in [3, 8]")
    negative_case(root, result_root, app_id, "pcm-channels-high", "sample_send_pcm_multiopus",
                  ["--audioFile", str(public / "8_Channel_ID_signed_16_bit_pcm.raw.pcm"), "--numOfChannels", "9"],
                  "numOfChannels must be in [3, 8]")
    negative_case(root, result_root, app_id, "pcm-rate", "sample_send_pcm_multiopus",
                  ["--audioFile", str(public / "8_Channel_ID_signed_16_bit_pcm.raw.pcm"), "--numOfChannels", "8",
                   "--sampleRate", "44100"], "only supports 48000")
    negative_case(root, result_root, app_id, "pcm-partial", "sample_send_pcm_multiopus",
                  ["--audioFile", str(bad_raw), "--numOfChannels", "8"], "partial interleaved sample")
    negative_case(root, result_root, app_id, "pcm-payload", "sample_send_pcm_multiopus",
                  ["--audioFile", str(public / "8_Channel_ID_signed_16_bit_pcm.raw.pcm"), "--numOfChannels", "8",
                   "--custom_payload_type", "123"], "incompatible")
    negative_case(root, result_root, app_id, "wav-channel-mismatch", "sample_send_wav_multiopus",
                  ["--audioFile", str(public / "4_Channel_ID_48k.wav"), "--numOfChannels", "8"], "channel")
    negative_case(root, result_root, app_id, "wav-malformed", "sample_send_wav_multiopus",
                  ["--audioFile", str(malformed), "--numOfChannels", "8"], "WAV")
    negative_case(root, result_root, app_id, "opus-partial", "sample_send_opus8",
                  ["--audioFile", str(bad_opus)], "integral number of frames")
    negative_case(root, result_root, app_id, "opus-ogg", "sample_send_opus8",
                  ["--audioFile", str(ogg)], "Ogg Opus")
    negative_case(root, result_root, app_id, "receiver-channels-low", "sample_receive_pcm_multiopus",
                  ["--numOfChannels", "2", "--sampleRate", "48000"], "must be in [3, 8]")
    negative_case(root, result_root, app_id, "receiver-channels-high", "sample_receive_pcm_multiopus",
                  ["--numOfChannels", "9", "--sampleRate", "48000"], "must be in [3, 8]")
    negative_case(root, result_root, app_id, "receiver-rate", "sample_receive_pcm_multiopus",
                  ["--numOfChannels", "8", "--sampleRate", "44100"], "only supports a 48000")
    negative_case(root, result_root, app_id, "mixed-channels", "sample_receive_mixed_audio",
                  ["--numOfChannels", "8"], "numOfChannels must be 1 or 2")
    negative_case(root, result_root, app_id, "ordinary-opus-channels", "sample_send_pcm_opus",
                  ["--audioFile", str(bad_raw), "--numOfChannels", "3"], "ordinary Opus supports")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample-dir", type=Path, required=True)
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--skip-mkv", action="store_true")
    parser.add_argument("--strict-timestamp-continuity", action="store_true",
                        help="fail on encoded timestamp gaps after subscription warmup; "
                             "use only on a controlled no-loss network")
    args = parser.parse_args()
    root = args.sample_dir.resolve()
    public = (root / "public") if (root / "public").exists() else (root.parent / "public")
    result_root = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix="agora-linux-full."))
    result_root.mkdir(parents=True, exist_ok=True)
    print(json.dumps({"architecture": platform.machine(), "sample_dir": str(root),
                      "public_dir": str(public), "results": str(result_root),
                      "strict_timestamp_continuity": args.strict_timestamp_continuity},
                     ensure_ascii=False), flush=True)
    try:
        matrix_case(root, public, result_root, args.app_id, "pcm",
                    public / "8_Channel_ID_signed_16_bit_pcm.raw.pcm",
                    strict_continuity=args.strict_timestamp_continuity)
        matrix_case(root, public, result_root, args.app_id, "wav",
                    public / "8_Channel_ID_signed_16_bit_pcm.wav",
                    strict_continuity=args.strict_timestamp_continuity)
        matrix_case(root, public, result_root, args.app_id, "opus", public / "8ch_2.opus",
                    strict_continuity=args.strict_timestamp_continuity)
        if not args.skip_mkv:
            matrix_case(root, public, result_root, args.app_id, "mkv",
                        public / "VictoriaMulti.mkv", True,
                        strict_continuity=args.strict_timestamp_continuity)
        for channels in range(3, 9):
            tone_case(root, result_root, args.app_id, channels, 16)
        tone_case(root, result_root, args.app_id, 8, 24)
        for channels in (3, 5, 7):
            odd_case(root, public, result_root, args.app_id, channels)
        fallback_case(root, public, result_root, args.app_id, False,
                      args.strict_timestamp_continuity)
        fallback_case(root, public, result_root, args.app_id, True,
                      args.strict_timestamp_continuity)
        mismatch_case(root, result_root, args.app_id, 8, 3)
        mismatch_case(root, result_root, args.app_id, 3, 8)
        invalid_cases(root, public, result_root, args.app_id)
    finally:
        (result_root / "results.json").write_text(json.dumps(RESULTS, indent=2) + "\n")
    passed = sum(1 for item in RESULTS if item["pass"])
    print(f"SUMMARY {passed}/{len(RESULTS)} passed; artifacts={result_root}", flush=True)
    return 0 if RESULTS and passed == len(RESULTS) else 1


if __name__ == "__main__":
    raise SystemExit(main())
