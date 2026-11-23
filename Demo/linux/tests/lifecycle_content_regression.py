#!/usr/bin/env python3
import argparse
import json
import os
import re
import signal
import subprocess
import time
from pathlib import Path

from full_regression import (SAMPLE_RATE, assert_encoded_log, launch, log_text, no_application_errors,
                             pcm_samples, stop, tone_spectrum_samples, wait_for, write_tone_raw)


def analyze_recovery_offset(path: Path, channels: int, byte_offset: int) -> dict:
    values = pcm_samples(path)
    if byte_offset % (channels * 2):
        return {"pass": False, "error": "recovery offset is not frame aligned",
                "byte_offset": byte_offset}
    frame_offset = byte_offset // (channels * 2)
    startup_ok, startup = tone_spectrum_samples(values, channels, frame_offset + SAMPLE_RATE // 4,
                                                SAMPLE_RATE // 2)
    steady_ok, steady = tone_spectrum_samples(values, channels, frame_offset + SAMPLE_RATE,
                                              SAMPLE_RATE // 2)
    return {"pass": startup_ok and steady_ok, "byte_offset": byte_offset,
            "startup_pass": startup_ok, "steady_pass": steady_ok,
            "startup": startup, "steady": steady}


def analyze_recovery_markers(work_dir: Path, channels: int = 8) -> list[dict]:
    results = []
    for line in (work_dir / "recovery-offsets.txt").read_text().splitlines():
        label, offset_text = line.split()
        path = work_dir / (f"receiver-2.pcm.uid-71001" if label == "receiver-rejoin"
                           else "receiver-1.pcm.uid-71001")
        item = analyze_recovery_offset(path, channels, int(offset_text))
        item["label"] = label
        results.append(item)
    return results


def run_pcm_lifecycle(root: Path, result_root: Path, app_id: str, cycles: int) -> dict:
    case_dir = result_root / "pcm-lifecycle"
    case_dir.mkdir(parents=True, exist_ok=True)
    source = case_dir / "input.pcm"
    write_tone_raw(source, 8)
    env = os.environ.copy()
    env.update(APP_TOKEN=app_id, PCM_INPUT=str(source), CYCLES=str(cycles),
               TMPDIR=str(case_dir))
    completed = subprocess.run(["bash", str(root.parent / "tests/lifecycle_regression.sh"), str(root)],
                               cwd=root, env=env, capture_output=True, text=True,
                               timeout=cycles * 18 + 180)
    (case_dir / "run.log").write_text(completed.stdout + completed.stderr)
    match = re.search(r"^Artifacts: (.+)$", completed.stdout, re.MULTILINE)
    if not match:
        return {"case": "pcm-lifecycle", "pass": False,
                "detail": {"error": "lifecycle artifact directory not found",
                           "script_exit": completed.returncode}}
    work_dir = Path(match.group(1))
    try:
        recoveries = analyze_recovery_markers(work_dir)
        passed = completed.returncode == 0 and recoveries and all(item["pass"] for item in recoveries)
        return {"case": "pcm-lifecycle", "pass": passed,
                "detail": {"script_exit": completed.returncode, "cycles": cycles,
                           "artifacts": str(work_dir), "recovery_count": len(recoveries),
                           "recoveries": recoveries}}
    except Exception as exc:
        return {"case": "pcm-lifecycle", "pass": False,
                "detail": {"error": str(exc), "script_exit": completed.returncode,
                           "artifacts": str(work_dir)}}


def file_size(path: Path) -> int:
    return path.stat().st_size if path.exists() else 0


def nonzero_tail(path: Path, channels: int) -> bool:
    if not path.exists():
        return False
    values = pcm_samples(path)
    tail = values[-channels * (SAMPLE_RATE // 2):]
    return any(value != 0 for value in tail)


def run_receiver_matrix(root: Path, result_root: Path, app_id: str) -> dict:
    case = f"receiver-matrix-lifecycle-{time.time_ns()}"
    case_dir = result_root / case
    case_dir.mkdir(parents=True, exist_ok=True)
    source = case_dir / "source.pcm"
    write_tone_raw(source, 8)
    channel = f"receiver-matrix-lifecycle-{time.time_ns()}"
    sender_uid = "87001"
    receivers = {
        "pcm": ("sample_receive_pcm_multiopus", ["--userId", "87002", "--remoteUserId", sender_uid,
                "--numOfChannels", "8", "--sampleRate", "48000", "--lifecycleControl", "1",
                "--audioFile", str(case_dir / "pcm")]),
        "encoded": ("sample_receive_encoded_audio", ["--userId", "87003",
                    "--audioFile", str(case_dir / "encoded")]),
        "mixed": ("sample_receive_mixed_audio", ["--userId", "87004", "--numOfChannels", "2",
                  "--sampleRate", "48000", "--audioFile", str(case_dir / "mixed")]),
        "av": ("sample_receive_av_multiopus", ["--userId", "87005", "--remoteUserId", sender_uid,
               "--numOfChannels", "8", "--sampleRate", "48000",
               "--audioFile", str(case_dir / "avpcm"), "--videoFile", str(case_dir / "video")]),
    }
    running = {}
    sender = None
    outputs = {"pcm": case_dir / f"pcm.uid-{sender_uid}",
               "encoded": case_dir / f"encoded.uid-{sender_uid}",
               "mixed": case_dir / "mixed", "av": case_dir / f"avpcm.uid-{sender_uid}"}
    try:
        for name, (binary, args) in receivers.items():
            running[name] = launch(root, result_root, case, name, binary,
                                   ["--channelId", channel] + args, app_id)
        wait_for(lambda: all("Start receiving" in log_text(item) for item in running.values()),
                 list(running.values()), 30, "matrix receivers")
        sender = launch(root, result_root, case, "sender", "sample_send_pcm_multiopus",
                        ["--channelId", channel, "--userId", sender_uid, "--numOfChannels", "8",
                         "--sampleRate", "48000", "--audioFile", str(source), "--lifecycleControl", "1"],
                        app_id)
        wait_for(lambda: all(file_size(path) >= (4096 if name == "encoded" else 15360)
                             for name, path in outputs.items()),
                 list(running.values()) + [sender], 60, "initial matrix media")
        sender.proc.send_signal(signal.SIGUSR1)
        wait_for(lambda: "LIFECYCLE publish=0 ret=0" in log_text(sender), [sender], 20,
                 "matrix unpublish")
        time.sleep(2)
        stopped_sizes = {name: file_size(path) for name, path in outputs.items()}
        time.sleep(1)
        quiet = all(file_size(outputs[name]) == stopped_sizes[name] for name in ("pcm", "encoded"))
        before = stopped_sizes["pcm"]
        sender.proc.send_signal(signal.SIGUSR1)
        wait_for(lambda: "LIFECYCLE publish=1 ret=0" in log_text(sender), [sender], 20,
                 "matrix republish")
        wait_for(lambda: all(file_size(path) >= stopped_sizes[name] + (4096 if name == "encoded" else 15360)
                             for name, path in outputs.items()) and
                        file_size(outputs["pcm"]) >= before + 8 * 2 * SAMPLE_RATE * 2,
                 list(running.values()) + [sender], 45, "matrix media after republish")
        republish = analyze_recovery_offset(outputs["pcm"], 8, before)
        pcm = running["pcm"]
        pcm.proc.send_signal(signal.SIGUSR1)
        wait_for(lambda: "LIFECYCLE subscribe=0 ret=0" in log_text(pcm), [pcm], 20,
                 "matrix unsubscribe")
        time.sleep(2)
        pcm_stopped = file_size(outputs["pcm"])
        other_before = {name: file_size(path) for name, path in outputs.items() if name != "pcm"}
        time.sleep(1)
        pcm_quiet = file_size(outputs["pcm"]) == pcm_stopped
        others_active = all(file_size(outputs[name]) > other_before[name]
                            for name in ("encoded", "mixed", "av"))
        before = file_size(outputs["pcm"])
        pcm.proc.send_signal(signal.SIGUSR1)
        wait_for(lambda: "LIFECYCLE subscribe=1 ret=0" in log_text(pcm), [pcm], 20,
                 "matrix resubscribe")
        wait_for(lambda: file_size(outputs["pcm"]) >= before + 8 * 2 * SAMPLE_RATE * 2,
                 list(running.values()) + [sender], 45, "PCM after resubscribe")
        resubscribe = analyze_recovery_offset(outputs["pcm"], 8, before)
        codes = {name: stop(item) for name, item in running.items()}
        sender_code = stop(sender)
        encoded_ok, encoded_detail = assert_encoded_log(log_text(running["encoded"]), sender_uid, 13)
        other_media = nonzero_tail(outputs["mixed"], 2) and nonzero_tail(outputs["av"], 8)
        errors = no_application_errors(list(running.values()) + [sender])
        passed = (quiet and pcm_quiet and others_active and republish["pass"] and resubscribe["pass"] and
                  encoded_ok and other_media and not errors and sender_code == 0 and
                  all(code == 0 for code in codes.values()))
        return {"case": case, "pass": passed, "detail": {
            "quiet_after_unpublish": quiet, "quiet_after_unsubscribe": pcm_quiet,
            "other_receivers_active": others_active, "republish": republish,
            "resubscribe": resubscribe, "encoded": encoded_detail,
            "mixed_and_av_nonzero": other_media, "exit_codes": list(codes.values()) + [sender_code],
            "application_errors": errors,
        }}
    except Exception as exc:
        return {"case": case, "pass": False, "detail": {"error": str(exc)}}
    finally:
        for item in running.values():
            stop(item)
        stop(sender)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample-dir", type=Path, required=True)
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--skip-pcm-lifecycle", action="store_true")
    parser.add_argument("--skip-matrix", action="store_true")
    args = parser.parse_args()
    root = args.sample_dir.resolve()
    result_root = args.output_dir.resolve()
    result_root.mkdir(parents=True, exist_ok=True)
    results = []
    if not args.skip_pcm_lifecycle:
        item = run_pcm_lifecycle(root, result_root, args.app_id, args.cycles)
        results.append(item)
        (result_root / "extended-lifecycle-results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(("PASS " if item["pass"] else "FAIL ") + item["case"] + ": " +
              json.dumps(item["detail"], ensure_ascii=False), flush=True)
    if not args.skip_matrix:
        item = run_receiver_matrix(root, result_root, args.app_id)
        results.append(item)
        (result_root / "extended-lifecycle-results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(("PASS " if item["pass"] else "FAIL ") + item["case"] + ": " +
              json.dumps(item["detail"], ensure_ascii=False), flush=True)
    passed = sum(item["pass"] for item in results)
    print(f"EXTENDED_LIFECYCLE_SUMMARY {passed}/{len(results)} passed; artifacts={result_root}", flush=True)
    return 0 if results and passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
