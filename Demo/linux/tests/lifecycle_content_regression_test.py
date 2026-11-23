#!/usr/bin/env python3
import array
import math
import sys
import tempfile
import unittest
from pathlib import Path

from full_regression import FREQUENCIES, SAMPLE_RATE
from lifecycle_content_regression import analyze_recovery_markers, analyze_recovery_offset


class LifecycleContentAnalysisTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def write_tone(self, name, seconds=2, hz300_amplitude=0, hz300_until=0):
        path = self.directory / name
        values = array.array("h")
        for frame in range(int(SAMPLE_RATE * seconds)):
            for channel, frequency in enumerate(FREQUENCIES):
                sample = int(5000 * math.sin(2 * math.pi * frequency * frame / SAMPLE_RATE))
                if channel > 0 and frame < hz300_until * SAMPLE_RATE:
                    sample += int(hz300_amplitude * math.sin(2 * math.pi * 300 * frame / SAMPLE_RATE))
                values.append(sample)
        if sys.byteorder != "little":
            values.byteswap()
        path.write_bytes(values.tobytes())
        return path

    def test_recovery_content_passes_with_frequency_shift(self):
        path = self.write_tone("receiver-1.pcm.uid-71001")
        result = analyze_recovery_offset(path, 8, 0)
        self.assertTrue(result["pass"])
        self.assertTrue(result["startup_pass"])
        self.assertTrue(result["steady_pass"])

    def test_recovery_content_rejects_strong_300hz(self):
        path = self.write_tone("receiver-1.pcm.uid-71001", hz300_amplitude=3000,
                               hz300_until=0.5)
        result = analyze_recovery_offset(path, 8, 0)
        self.assertFalse(result["pass"])
        self.assertFalse(result["startup_pass"])
        self.assertTrue(result["steady_pass"])

    def test_recovery_content_rejects_short_capture(self):
        path = self.write_tone("receiver-1.pcm.uid-71001", seconds=1)
        result = analyze_recovery_offset(path, 8, 0)
        self.assertFalse(result["pass"])
        self.assertFalse(result["steady_pass"])

    def test_recovery_content_rejects_unaligned_offset(self):
        path = self.write_tone("receiver-1.pcm.uid-71001")
        result = analyze_recovery_offset(path, 8, 3)
        self.assertFalse(result["pass"])
        self.assertIn("not frame aligned", result["error"])

    def test_recovery_markers_cover_both_receivers(self):
        self.write_tone("receiver-1.pcm.uid-71001")
        self.write_tone("receiver-2.pcm.uid-71001")
        (self.directory / "recovery-offsets.txt").write_text("initial 0\nreceiver-rejoin 0\n")
        results = analyze_recovery_markers(self.directory)
        self.assertEqual([item["label"] for item in results], ["initial", "receiver-rejoin"])
        self.assertTrue(all(item["pass"] for item in results))


if __name__ == "__main__":
    unittest.main()
