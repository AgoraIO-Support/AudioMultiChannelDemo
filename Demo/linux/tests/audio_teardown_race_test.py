#!/usr/bin/env python3
import json
import os
import resource
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def fake_participant():
    args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
    expected = int(args.get("--expectedBuild", "0"))
    if expected != 1397089 and expected != 0:
        print("RACE_SDK_BUILD 1", flush=True)
        raise SystemExit(4)
    print("RACE_SDK_BUILD 1397089", flush=True)
    if os.environ.get("FAKE_SLEEP"):
        import time
        time.sleep(float(os.environ["FAKE_SLEEP"]))
    for iteration in range(1, int(args["--iterations"]) + 1):
        print(f"RACE_CASE_BEGIN iteration={iteration}", flush=True)
        if int(os.environ.get("FAKE_SEGV_AT", "0")) == iteration:
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            os.kill(os.getpid(), signal.SIGSEGV)
        print(f"RACE_CASE_CONNECTED iteration={iteration}", flush=True)
        print(f"RACE_CASE_DISCONNECTED iteration={iteration}", flush=True)
        print(f"RACE_CASE_DESTROYED iteration={iteration}", flush=True)
    if os.environ.get("FAKE_OPERATIONAL_FAILURE"):
        raise SystemExit(1)
    print("RACE_SERVICE_RELEASED", flush=True)
    print(f"RACE_SUMMARY completed={args['--iterations']} failures=0", flush=True)


def fake_stock_participant():
    print("SDK version: 1397089", flush=True)
    if os.environ.get("FAKE_JOIN_TIMEOUT"):
        print("Wait for connection timeout or failure!", flush=True)
        print("onDisconnected", flush=True)
        if os.environ.get("FAKE_STOCK_SEGV"):
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            os.kill(os.getpid(), signal.SIGSEGV)
        raise SystemExit(1)
    if os.environ.get("FAKE_STOCK_SEGV"):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        os.kill(os.getpid(), signal.SIGSEGV)
    print("Start receiving multi-channel audio data", flush=True)
    running = [True]
    signal.signal(signal.SIGINT, lambda *_: running.__setitem__(0, False))
    while running[0]:
        signal.pause()
    print("onDisconnected", flush=True)


class TeardownRaceRunnerTest(unittest.TestCase):
    def run_case(self, extra_env=None, *extra_args):
        with tempfile.TemporaryDirectory(prefix="teardown-race-test-") as tmp:
            root = Path(tmp)
            for name in ("audio_teardown_race", "sample_receive_pcm_multiopus"):
                (root / name).symlink_to(Path(__file__).resolve())
            artifact = root / "artifacts"
            env = os.environ.copy()
            env.update(extra_env or {})
            iterations = "1" if "--binary" in extra_args else "3"
            command = [
                sys.executable, str(Path(__file__).with_name("audio_teardown_race.py")),
                "--sample-dir", str(root), "--artifact-dir", str(artifact),
                "--app-id", "fake-app", "--expected-build", "1397089",
                "--iterations", iterations, "--timeout", "2",
            ] + list(extra_args)
            completed = subprocess.run(command, env=env, capture_output=True, text=True,
                                       timeout=20)
            summary = json.loads((artifact / "summary.json").read_text())
            self.assertFalse(summary["configuration"]["fault_injection"])
            return completed, summary

    def test_success(self):
        completed, summary = self.run_case()
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(summary["completed_iterations"], 3)
        self.assertEqual(summary["segfaults"], 0)

    def test_sigsegv_is_reproduction_not_operational_failure(self):
        completed, summary = self.run_case({"FAKE_SEGV_AT": "2"})
        self.assertEqual(completed.returncode, 10, completed.stdout + completed.stderr)
        self.assertTrue(summary["natural_reproduced"])
        self.assertEqual(summary["segfaults"], 1)
        self.assertEqual(summary["operational_failures"], 0)
        self.assertEqual(summary["attempted_iterations"], 2)

    def test_keep_going_completes_batches_after_segfault(self):
        completed, summary = self.run_case({"FAKE_SEGV_AT": "1"}, "--batches", "3", "--keep-going")
        self.assertEqual(completed.returncode, 10, completed.stdout + completed.stderr)
        self.assertEqual(summary["attempted_processes"], 3)
        self.assertEqual(summary["attempted_iterations"], 3)
        self.assertEqual(summary["segfaults"], 3)
        self.assertTrue(summary["configuration"]["keep_going"])

    def test_timeout_is_not_segfault(self):
        completed, summary = self.run_case({"FAKE_SLEEP": "5"}, "--timeout", "1")
        self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
        self.assertEqual(summary["timeouts"], 1)
        self.assertEqual(summary["segfaults"], 0)

    def test_stock_receiver_signal_path(self):
        completed, summary = self.run_case({}, "--binary", "sample_receive_pcm_multiopus")
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(summary["attempted_iterations"], 1)
        self.assertEqual(summary["completed_iterations"], 1)
        self.assertEqual(summary["segfaults"], 0)

    def test_stock_receiver_crash_is_natural_runner_observation(self):
        completed, summary = self.run_case({"FAKE_STOCK_SEGV": "1"},
                                           "--binary", "sample_receive_pcm_multiopus")
        self.assertEqual(completed.returncode, 10, completed.stdout + completed.stderr)
        self.assertTrue(summary["natural_reproduced"])
        self.assertEqual(summary["segfaults"], 1)

    def test_join_timeout_is_expected_scenario(self):
        completed, summary = self.run_case({"FAKE_JOIN_TIMEOUT": "1"},
                                           "--binary", "sample_receive_pcm_multiopus",
                                           "--expect-join-timeout")
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(summary["join_timeouts"], 1)
        self.assertEqual(summary["operational_failures"], 0)
        self.assertFalse(summary["natural_reproduced"])

    def test_join_timeout_crash_is_still_segfault(self):
        completed, summary = self.run_case({"FAKE_JOIN_TIMEOUT": "1", "FAKE_STOCK_SEGV": "1"},
                                           "--binary", "sample_receive_pcm_multiopus",
                                           "--expect-join-timeout")
        self.assertEqual(completed.returncode, 10, completed.stdout + completed.stderr)
        self.assertEqual(summary["join_timeouts"], 1)
        self.assertEqual(summary["segfaults"], 1)
        self.assertEqual(summary["operational_failures"], 0)

    def test_join_timeout_rejects_successful_join(self):
        completed, summary = self.run_case({}, "--binary", "sample_receive_pcm_multiopus",
                                           "--expect-join-timeout")
        self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
        self.assertEqual(summary["unexpected_successful_joins"], 1)
        self.assertEqual(summary["join_timeouts"], 0)

    def test_build_mismatch_is_rejected(self):
        env = os.environ.copy()
        env["FAKE_SEGV_AT"] = "1"
        with tempfile.TemporaryDirectory(prefix="teardown-race-test-") as tmp:
            root = Path(tmp)
            (root / "audio_teardown_race").symlink_to(Path(__file__).resolve())
            completed = subprocess.run([
                sys.executable, str(Path(__file__).with_name("audio_teardown_race.py")),
                "--sample-dir", str(root), "--artifact-dir", str(root / "artifacts"),
                "--app-id", "fake-app", "--expected-build", "1392522",
                "--iterations", "1", "--timeout", "2",
            ], env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(completed.returncode, 1)
            summary = json.loads((root / "artifacts" / "summary.json").read_text())
            self.assertEqual(summary["version_failures"], 1)
            self.assertFalse(summary["natural_reproduced"])


if __name__ == "__main__":
    if Path(sys.argv[0]).name == "audio_teardown_race":
        fake_participant()
    elif Path(sys.argv[0]).name == "sample_receive_pcm_multiopus":
        fake_stock_participant()
    else:
        unittest.main()
