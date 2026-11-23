#!/usr/bin/env python3
import array
import math
import sys
import tempfile
import unittest
from pathlib import Path

from full_regression import FREQUENCIES, SAMPLE_RATE
from mismatch_content_regression import analyze_mismatch_content


class MismatchContentAnalysisTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def write_pcm(self, name, sequence, seconds=2):
        path = self.directory / name
        values = array.array("h")
        channels = len(sequence)
        for frame in range(int(SAMPLE_RATE * seconds)):
            for channel, source_index in enumerate(sequence):
                if source_index is None:
                    values.append(0)
                    continue
                source_indices = source_index if isinstance(source_index, tuple) else (source_index,)
                sample = 0
                for index in source_indices:
                    frequency = FREQUENCIES[index] if isinstance(index, int) else index
                    sample += int(5000 * math.sin(2 * math.pi * frequency * frame / SAMPLE_RATE))
                values.append(sample)
        if sys.byteorder != "little":
            values.byteswap()
        path.write_bytes(values.tobytes())
        return path

    def analyze(self, source_channels, sequence):
        source = self.write_pcm("source.pcm", list(range(source_channels)))
        received = self.write_pcm("received.pcm", sequence)
        return analyze_mismatch_content(source, received, source_channels, len(sequence))

    def test_identical_channel_mapping_passes(self):
        result = self.analyze(3, [0, 1, 2])
        self.assertTrue(result["pass"])
        self.assertEqual(result["matched_sequence"], [1, 2, 3])

    def test_ordered_downsampling_subset_passes(self):
        result = self.analyze(8, [0, 2, 5])
        self.assertTrue(result["pass"])
        self.assertEqual(result["matched_sequence"], [1, 3, 6])

    def test_reordered_downsampling_subset_fails(self):
        result = self.analyze(8, [0, 7, 2])
        self.assertFalse(result["pass"])
        self.assertIn("reordered_source_channels", result["reasons"])

    def test_duplicated_source_channel_fails(self):
        result = self.analyze(3, [0, 0, 1, 2, 2, 2, 2, 2])
        self.assertFalse(result["pass"])
        self.assertIn("duplicated_source_channels", result["reasons"])

    def test_unmatched_tone_fails(self):
        result = self.analyze(3, [800.0] * 8)
        self.assertFalse(result["pass"])
        self.assertIn("unmatched_non_silent_channels", result["reasons"])

    def test_upscaling_with_silent_extra_channels_passes(self):
        result = self.analyze(3, [0, 1, 2, None, None, None, None, None])
        self.assertTrue(result["pass"])
        self.assertEqual(result["silent_channels"], [4, 5, 6, 7, 8])

    def test_upscaling_with_unmatched_extra_channels_fails(self):
        result = self.analyze(3, [0, 1, 2, 800.0, 800.0, 800.0, 800.0, 800.0])
        self.assertFalse(result["pass"])
        self.assertIn("unmatched_non_silent_channels", result["reasons"])

    def test_mixed_source_channels_fail(self):
        result = self.analyze(8, [(0, 3), 7, 2])
        self.assertFalse(result["pass"])
        self.assertIn("mixed_source_channels", result["reasons"])
        self.assertEqual(result["mixed_channels"], [1])

    def test_downsampling_with_silent_received_channel_fails(self):
        result = self.analyze(8, [0, None, 2])
        self.assertFalse(result["pass"])
        self.assertIn("missing_received_channels", result["reasons"])


if __name__ == "__main__":
    unittest.main()
