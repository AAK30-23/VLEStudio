import sys
import unittest
from pathlib import Path

# Добавляем папку python в путь поиска модулей
PROJECT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_DIR / "python"))

from vle_calc import calculate_pr

class TestVLECalculation(unittest.TestCase):

    def test_result_contains_expected_fields(self):
        result = calculate_pr()

        expected_fields = {
            "temperature_K",
            "pressure_Pa",
            "phase_count",
            "feed",
            "liquid",
            "vapor",
            "vapor_fraction",
        }

        self.assertTrue(expected_fields.issubset(result.keys()))

    def test_temperature_and_pressure(self):
        result = calculate_pr()

        self.assertAlmostEqual(result["temperature_K"], 350.0)
        self.assertAlmostEqual(result["pressure_Pa"], 101325.0)

    def test_feed_composition_sums_to_one(self):
        result = calculate_pr()

        self.assertAlmostEqual(sum(result["feed"]), 1.0)

    def test_phase_compositions_are_valid(self):
        result = calculate_pr()

        for phase_name in ("liquid", "vapor"):
            composition = result[phase_name]

            if composition is not None:
                self.assertAlmostEqual(sum(composition), 1.0)

                for fraction in composition:
                    self.assertGreaterEqual(fraction, 0.0)
                    self.assertLessEqual(fraction, 1.0)

    def test_vapor_fraction_is_in_valid_range(self):
        result = calculate_pr()
        vapor_fraction = result["vapor_fraction"]

        self.assertGreaterEqual(vapor_fraction, 0.0)
        self.assertLessEqual(vapor_fraction, 1.0)

    def test_invalid_feed_composition_is_rejected(self):
        for x in (0.0, 1.0, -0.1, 1.1):
            with self.subTest(x_ethanol=x):
                with self.assertRaises(ValueError):
                    calculate_pr(x_ethanol=x)

if __name__ == "__main__":
    unittest.main()
