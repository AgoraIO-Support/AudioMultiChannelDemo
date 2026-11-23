#!/usr/bin/env python3
import array
import json
import math
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from full_regression import FREQUENCIES, SAMPLE_RATE
from semantic_regression import analyze_mixed_output, assert_pcm_trace, write_synthetic_mkv


class SemanticAnalysisTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def write_mixed(self, name, channels, cross_channel=False, missing_source=None):
        path = self.directory / name
        groups = ([0, 2, 4, 6], [1, 3, 5, 7]) if channels == 2 else (list(range(8)),)
        values = array.array("h")
        for frame in range(SAMPLE_RATE * 2):
            for channel, sources in enumerate(groups):
                sample = 0
                for source in sources:
                    if source == missing_source:
                        continue
                    sample += int(1000 * math.sin(2 * math.pi * FREQUENCIES[source] * frame / SAMPLE_RATE))
                if cross_channel and channel == 0:
                    sample += int(3000 * math.sin(2 * math.pi * FREQUENCIES[1] * frame / SAMPLE_RATE))
                values.append(sample)
        if sys.byteorder != "little":
            values.byteswap()
        path.write_bytes(values.tobytes())
        return path

    def test_stereo_odd_even_isolation_passes(self):
        path = self.write_mixed("stereo.pcm", 2)
        result = analyze_mixed_output(path, 2)
        self.assertTrue(result["pass"])

    def test_stereo_cross_channel_leak_fails(self):
        path = self.write_mixed("stereo-leak.pcm", 2, cross_channel=True)
        result = analyze_mixed_output(path, 2)
        self.assertFalse(result["pass"])
        self.assertTrue(result["channels"][0]["leaking_frequencies"])

    def test_mono_contains_all_sources(self):
        path = self.write_mixed("mono.pcm", 1)
        result = analyze_mixed_output(path, 1)
        self.assertTrue(result["pass"])

    def test_mono_missing_source_fails(self):
        path = self.write_mixed("mono-missing.pcm", 1, missing_source=5)
        result = analyze_mixed_output(path, 1)
        self.assertFalse(result["pass"])
        self.assertIn(str(FREQUENCIES[5]), result["channels"][0]["missing_frequencies"])

    def test_pcm_trace_normal_and_wrapped_timestamps(self):
        lines = []
        rtp = 0xfffff000
        for index in range(10):
            lines.append(f"PCM_FRAME_TRACE user=89001 frame={index + 1} samples=480 "
                         f"rtpTimestamp={rtp} renderTimeMs={index * 10}")
            rtp = (rtp + 480) & 0xffffffff
        passed, detail = assert_pcm_trace("\n".join(lines), "89001")
        self.assertTrue(passed)
        self.assertTrue(detail["wrapped_timestamp"])

    def test_pcm_trace_allows_duplicate_and_nonuniform_timestamps(self):
        lines = ["PCM_FRAME_TRACE user=89001 frame=1 samples=480 rtpTimestamp=960 renderTimeMs=0",
                 "PCM_FRAME_TRACE user=89001 frame=2 samples=480 rtpTimestamp=960 renderTimeMs=10",
                 "PCM_FRAME_TRACE user=89001 frame=3 samples=480 rtpTimestamp=1320 renderTimeMs=20"]
        passed, detail = assert_pcm_trace("\n".join(lines), "89001")
        self.assertTrue(passed)
        self.assertEqual(detail["duplicate_timestamp_count"], 1)
        self.assertEqual(detail["nonuniform_delta_count"], 2)

    def test_pcm_trace_rejects_large_gap(self):
        lines = ["PCM_FRAME_TRACE user=89001 frame=1 samples=480 rtpTimestamp=960 renderTimeMs=0",
                 "PCM_FRAME_TRACE user=89001 frame=2 samples=480 rtpTimestamp=1440 renderTimeMs=10",
                 "PCM_FRAME_TRACE user=89001 frame=3 samples=480 rtpTimestamp=5000 renderTimeMs=90"]
        passed, detail = assert_pcm_trace("\n".join(lines), "89001")
        self.assertFalse(passed)
        self.assertFalse(detail["continuity_ok"])
        self.assertEqual(detail["delivery_gap_count"], 1)

    def test_pcm_trace_rejects_timestamp_regression(self):
        lines = ["PCM_FRAME_TRACE user=89001 frame=1 samples=480 rtpTimestamp=5000 renderTimeMs=0",
                 "PCM_FRAME_TRACE user=89001 frame=2 samples=480 rtpTimestamp=1000 renderTimeMs=10"]
        passed, detail = assert_pcm_trace("\n".join(lines), "89001")
        self.assertFalse(passed)
        self.assertFalse(detail["order_ok"])

    def test_pcm_trace_rejects_wrong_sample_count(self):
        lines = ["PCM_FRAME_TRACE user=89001 frame=1 samples=480 rtpTimestamp=960 renderTimeMs=0",
                 "PCM_FRAME_TRACE user=89001 frame=2 samples=960 rtpTimestamp=1440 renderTimeMs=10"]
        passed, detail = assert_pcm_trace("\n".join(lines), "89001")
        self.assertFalse(passed)
        self.assertFalse(detail["samples_ok"])

    def test_pcm_trace_requires_two_frames(self):
        passed, detail = assert_pcm_trace(
            "PCM_FRAME_TRACE user=89001 frame=1 samples=480 rtpTimestamp=960 renderTimeMs=0", "89001")
        self.assertFalse(passed)
        self.assertEqual(detail["frames_logged"], 1)

    @unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg is required")
    def test_synthetic_mkv_has_one_video_and_four_stereo_tracks(self):
        path = self.directory / "four-stereo.mkv"
        write_synthetic_mkv(path)
        completed = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries", "stream=codec_type,channels",
             "-of", "json", str(path)], capture_output=True, text=True, check=True)
        streams = json.loads(completed.stdout)["streams"]
        self.assertEqual(sum(stream["codec_type"] == "video" for stream in streams), 1)
        audio = [stream for stream in streams if stream["codec_type"] == "audio"]
        self.assertEqual(len(audio), 4)
        self.assertTrue(all(stream["channels"] == 2 for stream in audio))


if __name__ == "__main__":
    unittest.main()
