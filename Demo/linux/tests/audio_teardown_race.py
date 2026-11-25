#!/usr/bin/env python3
import argparse
import concurrent.futures
import json
import os
import re
import signal
import subprocess
import threading
import time
from pathlib import Path


def positive(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def nonnegative(value: str) -> int:
    number = int(value)
    if number < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return number


def run_worker(args, batch: int, worker: int, stop_event: threading.Event) -> dict:
    name = f"batch-{batch}-worker-{worker}"
    case_dir = args.artifact_dir / name
    case_dir.mkdir(parents=True, exist_ok=True)
    app_log = case_dir / "receiver.log"
    sdk_log = case_dir / "receiver-sdk" / "agorasdk.log"
    sdk_log.parent.mkdir(exist_ok=True)
    channel = f"audio-race-{args.run_id}-b{batch}-w{worker}"
    if args.binary == "audio_teardown_race":
        command = [
            str(args.sample_dir / args.binary),
            "--token", args.app_id,
            "--channelId", channel,
            "--userId", args.user_id,
            "--remoteUserId", args.remote_user_id,
            "--iterations", str(args.iterations),
            "--numOfChannels", str(args.channels),
            "--connectTimeoutMs", str(args.connect_timeout_ms),
            "--holdMs", str(args.hold_ms),
            "--releaseDelayUs", str(args.release_delay_us),
            "--noConnect", "1" if args.no_connect else "0",
        ]
        if args.expected_build:
            command += ["--expectedBuild", str(args.expected_build)]
    else:
        command = [
            str(args.sample_dir / args.binary),
            "--token", args.app_id,
            "--channelId", channel,
            "--userId", args.user_id,
            "--remoteUserId", args.remote_user_id,
            "--numOfChannels", str(args.channels),
            "--sampleRate", "48000",
            "--audioFile", str(case_dir / "output"),
        ]
    env = os.environ.copy()
    env["AGORA_LOG_PATH"] = str(sdk_log)
    if args.library_dir:
        env["LD_LIBRARY_PATH"] = str(args.library_dir) + os.pathsep + env.get("LD_LIBRARY_PATH", "")

    started = time.monotonic()
    timed_out = False
    cancelled = False
    with app_log.open("w") as output:
        process = subprocess.Popen(command, cwd=args.sample_dir, env=env,
                                   stdout=output, stderr=subprocess.STDOUT)
        signalled = False
        while True:
            try:
                code = process.wait(timeout=0.05 if args.binary != "audio_teardown_race" else 0.1)
                break
            except subprocess.TimeoutExpired:
                if (args.binary != "audio_teardown_race" and not signalled and
                        "Start receiving" in app_log.read_text(errors="replace")):
                    if args.hold_ms:
                        time.sleep(args.hold_ms / 1000)
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                    signalled = True
                if time.monotonic() - started >= args.timeout:
                    timed_out = True
                    process.kill()
                    code = process.wait(timeout=5)
                    break
                if stop_event.is_set():
                    cancelled = True
                    process.send_signal(signal.SIGINT)
                    try:
                        code = process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        code = process.wait(timeout=5)
                    break
    text = app_log.read_text(errors="replace")
    builds = re.findall(r"(?:RACE_SDK_BUILD|SDK version:)\s*(\d+)", text)
    build = int(builds[0]) if builds else None
    version_ok = not args.expected_build or build == args.expected_build
    summaries = re.findall(r"RACE_SUMMARY completed=(\d+) failures=(\d+)", text)
    attempted = [int(item) for item in re.findall(r"RACE_CASE_BEGIN iteration=(\d+)", text)]
    if args.binary == "audio_teardown_race":
        completed = int(summaries[-1][0]) if summaries else len(re.findall("RACE_CASE_DESTROYED", text))
    else:
        completed = int(code == 0)
    attempted_iterations = max(attempted, default=0) if args.binary == "audio_teardown_race" else 1
    segfault = code == -signal.SIGSEGV
    join_timeout = args.expect_join_timeout and "Wait for connection timeout or failure!" in text
    result = {
        "worker": name,
        "channel_prefix": channel,
        "pid": process.pid,
        "exit": code,
        "signal": -code if code < 0 else None,
        "segfault": segfault,
        "join_timeout": join_timeout,
        "timed_out": timed_out,
        "cancelled": cancelled,
        "sdk_build": build,
        "version_ok": version_ok,
        "attempted_iterations": attempted_iterations,
        "completed_iterations": completed,
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "app_log": str(app_log),
        "sdk_log": str(sdk_log),
        "log_tail": text[-12000:],
    }
    if not args.keep_going and not join_timeout and (segfault or timed_out or not version_ok or code != 0):
        stop_event.set()
    if not segfault and (code == 0 or join_timeout) and not timed_out and version_ok and not args.keep_success_logs:
        sdk_log.unlink(missing_ok=True)
        result["sdk_log"] = None
    return result


def write_summary(args, results: list[dict]) -> dict:
    crashes = [item for item in results if item["segfault"]]
    summary = {
        "run_id": args.run_id,
        "configuration": {
            "binary": str(args.sample_dir / args.binary),
            "library_dir": str(args.library_dir) if args.library_dir else None,
            "expected_build": args.expected_build,
            "app_id": args.app_id,
            "user_id": args.user_id,
            "remote_user_id": args.remote_user_id,
            "channels": args.channels,
            "iterations_per_process": args.iterations,
            "batches": args.batches,
            "workers": args.workers,
            "hold_ms": args.hold_ms,
            "release_delay_us": args.release_delay_us,
            "no_connect": args.no_connect,
            "connect_timeout_ms": args.connect_timeout_ms,
            "process_timeout_seconds": args.timeout,
            "keep_going": args.keep_going,
            "expect_join_timeout": args.expect_join_timeout,
            "fault_injection": False,
        },
        "attempted_processes": len(results),
        "attempted_iterations": sum(item["attempted_iterations"] for item in results),
        "completed_iterations": sum(item["completed_iterations"] for item in results),
        "segfaults": len(crashes),
        "join_timeouts": sum(item["join_timeout"] for item in results),
        "unexpected_successful_joins": sum(
            args.expect_join_timeout and not item["join_timeout"] and item["exit"] == 0
            for item in results),
        "operational_failures": sum(
            item["exit"] not in (0, -signal.SIGSEGV) and not item["timed_out"] and
            not item["join_timeout"] for item in results),
        "timeouts": sum(item["timed_out"] for item in results),
        "version_failures": sum(not item["version_ok"] for item in results),
        "natural_reproduced": bool(crashes),
        "results": results,
    }
    summary["reproduction_rate_per_attempted_iteration"] = (
        len(crashes) / summary["attempted_iterations"] if summary["attempted_iterations"] else None)
    (args.artifact_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run real SDK receiver connection/teardown cycles; no fault injection.")
    parser.add_argument("--sample-dir", type=Path, required=True)
    parser.add_argument("--binary", choices=("audio_teardown_race", "sample_receive_pcm_multiopus"),
                        default="audio_teardown_race")
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--library-dir", type=Path)
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--expected-build", type=positive)
    parser.add_argument("--user-id", default="86002")
    parser.add_argument("--remote-user-id", default="86001")
    parser.add_argument("--channels", type=int, choices=range(3, 9), default=4)
    parser.add_argument("--iterations", type=positive, default=1)
    parser.add_argument("--batches", type=positive, default=1)
    parser.add_argument("--workers", type=positive, default=1)
    parser.add_argument("--hold-ms", type=nonnegative, default=0)
    parser.add_argument("--release-delay-us", type=nonnegative, default=0)
    parser.add_argument("--connect-timeout-ms", type=positive, default=3000)
    parser.add_argument("--timeout", type=positive, default=300)
    parser.add_argument("--no-connect", action="store_true")
    parser.add_argument("--expect-join-timeout", action="store_true",
                        help="Require the stock receiver's actual join-timeout log before teardown")
    parser.add_argument("--keep-going", action="store_true")
    parser.add_argument("--keep-success-logs", action="store_true")
    args = parser.parse_args()
    args.sample_dir = args.sample_dir.resolve()
    args.artifact_dir = args.artifact_dir.resolve()
    args.artifact_dir.mkdir(parents=True, exist_ok=True)
    if not (args.sample_dir / args.binary).is_file():
        parser.error(f"{args.binary} is missing; build or select the exact SDK sample first")
    if args.binary != "audio_teardown_race":
        if args.iterations != 1 or args.no_connect or args.release_delay_us or args.connect_timeout_ms != 3000:
            parser.error("the stock receiver supports one connection per process, hold-ms, and batches")
    elif args.expect_join_timeout:
        parser.error("--expect-join-timeout requires the stock receiver")
    args.run_id = time.time_ns()
    stop_event = threading.Event()
    results = []
    for batch in range(1, args.batches + 1):
        if stop_event.is_set():
            break
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
            futures = [executor.submit(run_worker, args, batch, worker, stop_event)
                       for worker in range(1, args.workers + 1)]
            for future in concurrent.futures.as_completed(futures):
                result = future.result()
                results.append(result)
                print(f"RACE_WORKER {json.dumps({key: result[key] for key in (
                    'worker', 'pid', 'exit', 'segfault', 'timed_out', 'sdk_build',
                    'attempted_iterations', 'completed_iterations')})}", flush=True)
                if result["segfault"] and not args.keep_going:
                    stop_event.set()
        summary = write_summary(args, results)
        print(f"RACE_PROGRESS processes={summary['attempted_processes']} "
              f"iterations={summary['attempted_iterations']} segfaults={summary['segfaults']}", flush=True)

    summary = write_summary(args, results)
    print("TEARDOWN_RACE_SUMMARY " + json.dumps({
        key: summary[key] for key in (
            "attempted_processes", "attempted_iterations", "completed_iterations", "segfaults",
            "join_timeouts", "unexpected_successful_joins", "operational_failures", "timeouts",
            "version_failures", "natural_reproduced")
    }), flush=True)
    print(f"Artifacts: {args.artifact_dir}", flush=True)
    if summary["natural_reproduced"]:
        return 10
    if (summary["operational_failures"] or summary["timeouts"] or
            summary["version_failures"] or summary["unexpected_successful_joins"]):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
