#!/usr/bin/env python3
"""Exercise the shell harness with fake processes; this is not an SDK test."""
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time


def participant():
    args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
    shared = pathlib.Path(os.environ["FAKE_SHARED_DIR"])
    is_sender = "send_pcm" in pathlib.Path(sys.argv[0]).name
    online = shared / "online"
    published = shared / "published"
    running = True
    toggle = False

    def terminate(_signum, _frame):
        nonlocal running
        running = False

    def control(_signum, _frame):
        nonlocal toggle
        toggle = True

    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGUSR1, control)
    uid = args.get("--remoteUserId", args.get("--userId"))
    output = pathlib.Path(args.get("--audioFile", "unused") + ".uid-" + uid)
    subscribed = True
    seen = False
    if is_sender:
        online.touch()
        published.touch()
    print("Start sending multi-channel audio data" if is_sender else
          "Start receiving multi-channel audio data", flush=True)
    try:
        while running:
            if toggle:
                toggle = False
                if is_sender:
                    if published.exists():
                        published.unlink()
                    else:
                        published.touch()
                    print(f"LIFECYCLE publish={int(published.exists())} ret=0", flush=True)
                else:
                    subscribed = not subscribed
                    print(f"LIFECYCLE subscribe={int(subscribed)} ret=0", flush=True)
            if not is_sender:
                if online.exists() and not seen:
                    seen = True
                    print(f"onUserAudioTrackSubscribed: userId {uid}", flush=True)
                elif seen and not online.exists():
                    seen = False
                    print(f"onUserLeft: userId {uid}", flush=True)
                if online.exists() and published.exists() and (subscribed or os.getenv("FAKE_IGNORE_UNSUBSCRIBE")):
                    with output.open("ab") as pcm:
                        pcm.write(b"\0" * 128)
            time.sleep(0.05)
    finally:
        if is_sender:
            online.unlink(missing_ok=True)
            published.unlink(missing_ok=True)
            if os.getenv("FAKE_FATAL_LOG"):
                print("spdlog fatal error: simulated logging failure", flush=True)


def test_harness():
    script = pathlib.Path(__file__).resolve()
    harness = script.with_name("lifecycle_regression.sh")
    with tempfile.TemporaryDirectory(prefix="lifecycle-harness-test-") as tmp:
        root = pathlib.Path(tmp)
        for name in ("sample_send_pcm_multiopus", "sample_receive_pcm_multiopus"):
            (root / name).symlink_to(script)
        pcm = root / "input.pcm"
        pcm.touch()
        env = os.environ.copy()
        env.update(APP_TOKEN="fake", PCM_INPUT=str(pcm), FAKE_SHARED_DIR=str(root),
                   CYCLES="1", TIMEOUT_SEC="8", SETTLE_SEC="1", OBSERVE_SEC="1", MIN_GROWTH_BYTES="256",
                   TMPDIR=str(root))
        for scenario, expected in (("success", 0), ("audio", 1), ("logging", 1)):
            env.pop("FAKE_IGNORE_UNSUBSCRIBE", None)
            env.pop("FAKE_FATAL_LOG", None)
            if scenario == "audio":
                env["FAKE_IGNORE_UNSUBSCRIBE"] = "1"
            if scenario == "logging":
                env["FAKE_FATAL_LOG"] = "1"
            run = subprocess.run(["bash", str(harness), str(root)], env=env,
                                 capture_output=True, text=True, timeout=40)
            if run.returncode != expected:
                raise AssertionError(f"expected exit {expected}, got {run.returncode}\n{run.stdout}\n{run.stderr}")
            if scenario == "audio" and "PCM kept growing while stopped" not in run.stderr:
                raise AssertionError(f"missing negative assertion: {run.stderr}")
            if scenario == "logging" and "participant logs contain" not in run.stderr:
                raise AssertionError(f"missing logging failure assertion: {run.stderr}")
            if scenario == "success" and "PASS lifecycle regression" not in run.stdout:
                raise AssertionError(f"missing successful completion: {run.stdout}")
            print("PASS harness " + {"success": "lifecycle sequence", "audio": "rejects continued audio", "logging": "rejects fatal logging errors"}[scenario])


if __name__ == "__main__":
    if pathlib.Path(sys.argv[0]).name.startswith("sample_"):
        participant()
    else:
        test_harness()
