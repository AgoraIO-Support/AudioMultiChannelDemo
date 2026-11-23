#!/usr/bin/env python3
import unittest

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


if __name__ == "__main__":
    unittest.main()
