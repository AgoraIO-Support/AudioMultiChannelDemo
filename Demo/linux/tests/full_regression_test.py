#!/usr/bin/env python3
import array
import math
import sys
import tempfile
import unittest
from pathlib import Path

import full_regression as regression
from full_regression import assert_encoded_log


USER = "96003"


def encoded_log(*timestamps, codec=13):
    lines = []
    previous = None
    for index, timestamp in enumerate(timestamps, 1):
        delta = max(timestamp - previous, 0) if previous is not None else 0
        lines.append(
            f"[ APP_LOG_INFO ] Received encoded audio frame: user={USER} length=464 "
            f"codec={codec} sendTs={timestamp} delta={delta} (frame #{index})"
        )
        previous = timestamp
    return "\n".join(lines) + "\n"


class EncodedLogAssertionTest(unittest.TestCase):
    def test_monitor_mode_records_but_allows_delivery_gap(self):
        text = encoded_log(1000, 1960, 2920, 3880, 4840)
        text += (f"[ APP_LOG_WARNING ] ENCODED_TIMESTAMP_GAP user={USER} "
                 "previous=5800 current=8680 delta=2880\n")
        passed, detail = assert_encoded_log(text, USER, 13)
        self.assertTrue(passed)
        self.assertTrue(detail["monotonic_ok"])
        self.assertFalse(detail["continuity_ok"])
        self.assertEqual(detail["delivery_gap_count"], 1)
        self.assertEqual(detail["max_delivery_delta"], 2880)

    def test_strict_mode_rejects_delivery_gap(self):
        text = encoded_log(1000, 1960, 2920, 3880, 4840)
        text += (f"[ APP_LOG_ERROR ] ENCODED_TIMESTAMP_GAP user={USER} "
                 "previous=5800 current=8680 delta=2880\n")
        passed, detail = assert_encoded_log(text, USER, 13, strict_continuity=True)
        self.assertFalse(passed)
        self.assertFalse(detail["continuity_ok"])
        self.assertEqual(detail["delivery_gap_count"], 1)

    def test_non_monotonic_logged_timestamps_always_fail(self):
        text = encoded_log(1000, 1960, 2920, 2500, 4840)
        passed, detail = assert_encoded_log(text, USER, 13)
        self.assertFalse(passed)
        self.assertFalse(detail["monotonic_ok"])

    def test_order_error_always_fails_even_when_sampled_frames_are_monotonic(self):
        text = encoded_log(1000, 1960, 2920, 3880, 4840)
        text += (f"[ APP_LOG_ERROR ] ENCODED_TIMESTAMP_ORDER user={USER} "
                 "previous=5800 current=5700 delta=0\n")
        passed, detail = assert_encoded_log(text, USER, 13)
        self.assertFalse(passed)
        self.assertFalse(detail["monotonic_ok"])

    def test_strict_mode_allows_one_subscription_warmup_gap(self):
        text = encoded_log(1000, 3880, 4840, 5800, 6760)
        passed, detail = assert_encoded_log(text, USER, 13, strict_continuity=True)
        self.assertTrue(passed)
        self.assertTrue(detail["warmup_ok"])
        self.assertTrue(detail["continuity_ok"])

    def test_strict_mode_rejects_later_sampled_gap(self):
        text = encoded_log(1000, 1960, 2920, 5800, 6760)
        passed, detail = assert_encoded_log(text, USER, 13, strict_continuity=True)
        self.assertFalse(passed)
        self.assertFalse(detail["steady_ok"])

    def test_codec_mismatch_still_fails(self):
        text = encoded_log(1000, 1960, 2920, codec=1)
        passed, _ = assert_encoded_log(text, USER, 13)
        self.assertFalse(passed)


class ToneSpectrumAssertionTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def write_tone(self, frequencies, seconds=2.0, hz300_amplitude=0, hz300_since=0, hz300_until=0):
        path = self.directory / "tone.pcm"
        values = array.array("h")
        frames = int(regression.SAMPLE_RATE * seconds)
        for frame in range(frames):
            for channel, frequency in enumerate(frequencies):
                sample = int(5000 * math.sin(2 * math.pi * frequency * frame / regression.SAMPLE_RATE))
                if (channel > 0 and hz300_since * regression.SAMPLE_RATE <= frame <
                        hz300_until * regression.SAMPLE_RATE):
                    sample += int(hz300_amplitude * math.sin(
                        2 * math.pi * 300 * frame / regression.SAMPLE_RATE))
                values.append(sample)
        if sys.byteorder != "little":
            values.byteswap()
        path.write_bytes(values.tobytes())
        return path

    def test_full_spectrum_local_peak_allows_neteq_frequency_shift(self):
        path = self.write_tone([300, 475, 633, 808, 983, 1158, 1317, 1492])
        passed, detail = regression.tone_spectrum(path, 8)
        self.assertTrue(passed)
        self.assertAlmostEqual(detail[1]["frequency"], 475, delta=3)
        self.assertAlmostEqual(detail[2]["frequency"], 633, delta=3)

    def test_strong_300hz_crosstalk_fails_even_when_expected_tone_dominates(self):
        path = self.write_tone(regression.FREQUENCIES, hz300_amplitude=3000, hz300_until=2)
        passed, detail = regression.tone_spectrum(path, 8)
        self.assertFalse(passed)
        self.assertFalse(detail[1]["crosstalk_ok"])
        self.assertGreater(detail[1]["hz300_ratio"], 0.2)

    def test_small_300hz_codec_residual_is_allowed(self):
        path = self.write_tone(regression.FREQUENCIES, hz300_amplitude=100, hz300_until=2)
        passed, detail = regression.tone_spectrum(path, 8)
        self.assertTrue(passed)
        self.assertTrue(all(item["crosstalk_ok"] for item in detail[1:]))

    def test_short_300hz_burst_fails_even_when_full_window_dilutes_it(self):
        path = self.write_tone(regression.FREQUENCIES[:3], hz300_amplitude=3000,
                               hz300_until=0.375)
        passed, detail = regression.tone_spectrum(path, 3)
        self.assertFalse(passed)
        self.assertLess(detail[1]["hz300_ratio"], 0.2)
        self.assertTrue(detail[1]["crosstalk_violations"])

    def test_startup_and_steady_windows_are_reported_separately(self):
        self.assertTrue(hasattr(regression, "tone_spectrum_stages"))
        path = self.write_tone(regression.FREQUENCIES[:3], hz300_amplitude=3000,
                               hz300_until=0.75)
        passed, detail = regression.tone_spectrum_stages(path, 3)
        self.assertFalse(passed)
        self.assertFalse(detail["startup_pass"])
        self.assertTrue(detail["steady_pass"])

    def test_steady_300hz_burst_fails_even_when_startup_is_clean(self):
        path = self.write_tone(regression.FREQUENCIES[:3], hz300_amplitude=3000,
                               hz300_since=1.0, hz300_until=1.25)
        passed, detail = regression.tone_spectrum_stages(path, 3)
        self.assertFalse(passed)
        self.assertTrue(detail["startup_pass"])
        self.assertFalse(detail["steady_pass"])

    def test_steady_window_requires_enough_samples(self):
        self.assertTrue(hasattr(regression, "tone_spectrum_stages"))
        path = self.write_tone(regression.FREQUENCIES[:3], seconds=1)
        passed, detail = regression.tone_spectrum_stages(path, 3)
        self.assertFalse(passed)
        self.assertFalse(detail["steady_pass"])
        self.assertIn("not enough samples", detail["steady"][0]["error"])

    def test_tone_outside_frequency_tolerance_fails(self):
        path = self.write_tone([300, 550, 640])
        passed, _ = regression.tone_spectrum(path, 3)
        self.assertFalse(passed)


if __name__ == "__main__":
    unittest.main()
