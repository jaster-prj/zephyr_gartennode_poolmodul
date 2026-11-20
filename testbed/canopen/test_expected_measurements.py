import unittest

from expected_measurements import (
    SensorTransfer,
    adc_raw_from_mv,
    expected_orp,
    expected_ph,
    orp_measurement,
    ph_measurement,
)


class ExpectedMeasurementTests(unittest.TestCase):
    def test_adc_quantization_and_full_scale_boundaries(self):
        self.assertEqual(adc_raw_from_mv(0, 12), 0)
        self.assertEqual(adc_raw_from_mv(3300, 12), 4095)
        self.assertEqual(adc_raw_from_mv(3301, 12), 4095)
        self.assertEqual(adc_raw_from_mv(1650, 12), 2047)
        with self.assertRaises(ValueError):
            adc_raw_from_mv(-1, 12)

    def test_ph_calibration_representative_and_clamped_values(self):
        self.assertAlmostEqual(ph_measurement(2048), 7.0)
        self.assertAlmostEqual(ph_measurement(3500), 12.808)
        self.assertEqual(ph_measurement(0), 0.0)
        self.assertEqual(ph_measurement(4095), 14.0)
        self.assertAlmostEqual(expected_ph(7.0), 7.0, delta=0.01)
        self.assertAlmostEqual(expected_ph(0.0), 7.0 + (744 - 2048) * (14 / 3500))

    def test_ph_transfer_averaging_is_stable_for_constant_emulated_input(self):
        transfer = SensorTransfer(150.0, 600)
        raw = adc_raw_from_mv(transfer.voltage_mv(8.0), 12)
        for sample_count in (1, 2, 4, 5, 10, 255):
            # adc_emul is constant per channel, so averagearray returns the same
            # code regardless of its trimmed-mean branch or sample count.
            samples = [raw] * sample_count
            average = sum(samples) / sample_count
            self.assertEqual(expected_ph(8.0), ph_measurement(int(average)))

    def test_orp_adc_conversion_tracks_positive_negative_and_zero_inputs(self):
        self.assertEqual(orp_measurement(adc_raw_from_mv(1320, 12)), 0.0)
        self.assertAlmostEqual(expected_orp(0.0), 0.0, delta=1.0)
        self.assertAlmostEqual(expected_orp(650.0), 650.0, delta=2.0)
        self.assertAlmostEqual(expected_orp(-650.0), -650.0, delta=2.0)
        self.assertEqual(orp_measurement(0), 1000.0)
        self.assertEqual(orp_measurement(4095), -1000.0)


if __name__ == "__main__":
    unittest.main()
