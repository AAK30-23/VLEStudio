import sys
import unittest
from pathlib import Path

# Добавляем папку python в путь поиска модулей
PROJECT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_DIR / "python"))

from vle_calc import calculate_pr, calculate_txy, calculate_pxy

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

class TestTxyCalculation(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.result = calculate_txy(
            temperature_min_K=300.0,
            temperature_max_K=450.0,
            pressure_Pa=101325.0,
            n_points=21,
            mixture="ETHANOL_WATER",
            model="UNIFAC_PR",
        )

    def test_result_contains_expected_fields(self):
        expected_fields = {
            "temperature_K",
            "x1",
            "y1",
            "azeotrope",
        }

        self.assertEqual(set(self.result.keys()), expected_fields)

    def test_arrays_have_equal_lengths(self):
        self.assertEqual(
            len(self.result["temperature_K"]),
            len(self.result["x1"]),
        )

        self.assertEqual(
            len(self.result["temperature_K"]),
            len(self.result["y1"]),
        )

    def test_composition_grid_covers_full_range(self):
        x1 = self.result["x1"]

        self.assertAlmostEqual(x1[0], 0.0)
        self.assertAlmostEqual(x1[-1], 1.0)

        for left, right in zip(x1, x1[1:]):
            self.assertLess(left, right)

    def test_values_are_physical(self):
        for temperature in self.result["temperature_K"]:
            self.assertGreater(temperature, 300.0)
            self.assertLess(temperature, 450.0)

        for y1 in self.result["y1"]:
            self.assertGreaterEqual(y1, 0.0)
            self.assertLessEqual(y1, 1.0)

    def test_azeotrope_is_found_for_ethanol_water(self):
        azeotrope = self.result["azeotrope"]

        self.assertIsNotNone(azeotrope)
        self.assertAlmostEqual(azeotrope["x1"], 0.91, delta=0.03)
        self.assertAlmostEqual(
            azeotrope["temperature_K"],
            350.9,
            delta=1.0,
        )

    def test_pr_model_is_supported(self):
        result = calculate_txy(
            temperature_min_K=300.0,
            temperature_max_K=450.0,
            pressure_Pa=101325.0,
            n_points=11,
            mixture="ETHANOL_WATER",
            model="PR",
        )

        self.assertEqual(len(result["x1"]), 11)
        self.assertEqual(len(result["temperature_K"]), 11)

    def test_kij_parameter_changes_result(self):
        arguments = dict(
            temperature_min_K=300.0,
            temperature_max_K=450.0,
            pressure_Pa=101325.0,
            n_points=7,
            mixture="ETHANOL_WATER",
            model="PR",
        )

        without_kij = calculate_txy(**arguments)
        with_kij = calculate_txy(**arguments, kij=-0.09)

        self.assertNotEqual(
            without_kij["temperature_K"],
            with_kij["temperature_K"],
        )

    def test_invalid_arguments_are_rejected(self):
        with self.assertRaises(ValueError):
            calculate_txy(450.0, 300.0, 101325.0)

        with self.assertRaises(ValueError):
            calculate_txy(300.0, 450.0, -1.0)

        with self.assertRaises(ValueError):
            calculate_txy(300.0, 450.0, 101325.0, n_points=1)

        with self.assertRaises(ValueError):
            calculate_txy(
                300.0,
                450.0,
                101325.0,
                mixture="UNKNOWN",
            )

        with self.assertRaises(ValueError):
            calculate_txy(
                300.0,
                450.0,
                101325.0,
                model="UNKNOWN",
            )


class TestPxyCalculation(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.result = calculate_pxy(
            temperature_K=350.0,
            pressure_min_Pa=10000.0,
            pressure_max_Pa=1000000.0,
            n_points=21,
            mixture="ETHANOL_WATER",
            model="UNIFAC_PR",
        )

    def test_result_contains_expected_fields(self):
        expected_fields = {
            "pressure_Pa",
            "x1",
            "y1",
            "azeotrope",
        }

        self.assertEqual(set(self.result.keys()), expected_fields)

    def test_arrays_have_equal_lengths(self):
        self.assertEqual(
            len(self.result["pressure_Pa"]),
            len(self.result["x1"]),
        )

        self.assertEqual(
            len(self.result["pressure_Pa"]),
            len(self.result["y1"]),
        )

    def test_composition_grid_covers_full_range(self):
        x1 = self.result["x1"]

        self.assertAlmostEqual(x1[0], 0.0)
        self.assertAlmostEqual(x1[-1], 1.0)

    def test_values_are_physical(self):
        for pressure in self.result["pressure_Pa"]:
            self.assertGreater(pressure, 0.0)

        for y1 in self.result["y1"]:
            self.assertGreaterEqual(y1, 0.0)
            self.assertLessEqual(y1, 1.0)

    def test_azeotrope_is_found_for_ethanol_water(self):
        azeotrope = self.result["azeotrope"]

        self.assertIsNotNone(azeotrope)
        self.assertAlmostEqual(azeotrope["x1"], 0.91, delta=0.03)
        self.assertAlmostEqual(
            azeotrope["pressure_Pa"],
            97800.0,
            delta=2000.0,
        )

    def test_invalid_arguments_are_rejected(self):
        with self.assertRaises(ValueError):
            calculate_pxy(350.0, 1000000.0, 10000.0)

        with self.assertRaises(ValueError):
            calculate_pxy(350.0, 10000.0, 1000000.0, n_points=1)

    def test_pr_model_is_supported(self):
        result = calculate_pxy(
            temperature_K=350.0,
            pressure_min_Pa=10000.0,
            pressure_max_Pa=1000000.0,
            n_points=11,
            mixture="ETHANOL_WATER",
            model="PR",
        )

        self.assertEqual(len(result["x1"]), 11)
        self.assertEqual(len(result["pressure_Pa"]), 11)


class TestAcetoneChloroform(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.result = calculate_txy(
            temperature_min_K=300.0,
            temperature_max_K=450.0,
            pressure_Pa=101325.0,
            n_points=21,
            mixture="ACETONE_CHLOROFORM",
            model="UNIFAC_PR",
        )

    def test_all_points_are_calculated(self):
        self.assertEqual(len(self.result["x1"]), 21)

    def test_azeotrope_is_found(self):
        azeotrope = self.result["azeotrope"]

        self.assertIsNotNone(azeotrope)
        self.assertAlmostEqual(azeotrope["x1"], 0.36, delta=0.03)
        self.assertAlmostEqual(
            azeotrope["temperature_K"],
            336.6,
            delta=1.0,
        )


if __name__ == "__main__":
    unittest.main()
