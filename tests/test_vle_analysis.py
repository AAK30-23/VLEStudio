import sys
import unittest
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_DIR / "python"))

from vle_calc import calculate_azeotrope, calculate_point
from vle_analysis import (
    compare_with_experiment,
    evaluate_models,
    fit_binary_interaction_parameter,
)


class TestPointCalculation(unittest.TestCase):

    def test_all_mixtures_and_models_are_supported(self):
        conditions = {
            "ETHANOL_WATER": 351.0,
            "ACETONE_CHLOROFORM": 335.0,
        }

        for mixture, temperature in conditions.items():
            for model in ("PR", "UNIFAC_PR"):
                with self.subTest(mixture=mixture, model=model):
                    result = calculate_point(
                        T=temperature,
                        P=101325.0,
                        x1=0.5,
                        mixture=mixture,
                        model=model,
                    )

                    self.assertEqual(result["mixture"], mixture)
                    self.assertEqual(result["model"], model)
                    self.assertAlmostEqual(
                        result["temperature_K"],
                        temperature,
                    )
                    self.assertAlmostEqual(
                        result["pressure_Pa"],
                        101325.0,
                    )
                    self.assertAlmostEqual(sum(result["feed"]), 1.0)

                    for phase in ("liquid", "vapor"):
                        composition = result[phase]

                        if composition is not None:
                            self.assertAlmostEqual(
                                sum(composition),
                                1.0,
                            )

    def test_invalid_arguments_are_rejected(self):
        with self.assertRaises(ValueError):
            calculate_point(x1=0.0)

        with self.assertRaises(ValueError):
            calculate_point(x1=1.0)

        with self.assertRaises(ValueError):
            calculate_point(mixture="UNKNOWN")

        with self.assertRaises(ValueError):
            calculate_point(model="UNKNOWN")

        with self.assertRaises(ValueError):
            calculate_point(T=-1.0)

        with self.assertRaises(ValueError):
            calculate_point(P=0.0)


class TestAzeotrope(unittest.TestCase):

    def test_ethanol_water_azeotrope(self):
        result = calculate_azeotrope(
            pressure_Pa=101325.0,
            mixture="ETHANOL_WATER",
            model="UNIFAC_PR",
            n_points=51,
        )

        self.assertIsNotNone(result)
        self.assertAlmostEqual(result["x1"], 0.91, delta=0.03)
        self.assertAlmostEqual(
            result["temperature_K"],
            350.9,
            delta=1.0,
        )
        self.assertAlmostEqual(result["pressure_Pa"], 101325.0)

    def test_acetone_chloroform_azeotrope(self):
        result = calculate_azeotrope(
            pressure_Pa=101325.0,
            mixture="ACETONE_CHLOROFORM",
            model="UNIFAC_PR",
            n_points=51,
        )

        self.assertIsNotNone(result)
        self.assertAlmostEqual(result["x1"], 0.36, delta=0.04)
        self.assertAlmostEqual(
            result["temperature_K"],
            336.6,
            delta=1.5,
        )


class TestAccuracy(unittest.TestCase):

    def test_unifac_model_agrees_with_experiment(self):
        for mixture in ("ETHANOL_WATER", "ACETONE_CHLOROFORM"):
            with self.subTest(mixture=mixture):
                stats = compare_with_experiment(
                    mixture,
                    "UNIFAC_PR",
                    max_points=8,
                )

                self.assertEqual(stats["point_count"], 8)
                self.assertLess(stats["temperature_mad_K"], 2.0)
                self.assertLess(stats["y1_mad"], 0.05)
                self.assertTrue(stats["points"])

    def test_model_comparison_returns_both_models(self):
        stats = evaluate_models("ETHANOL_WATER", max_points=5)

        self.assertEqual(set(stats), {"PR", "UNIFAC_PR"})

        for model_stats in stats.values():
            self.assertGreater(model_stats["point_count"], 0)

    def test_kij_fitting_improves_description(self):
        result = fit_binary_interaction_parameter(
            "ETHANOL_WATER",
            model="PR",
            max_points=8,
        )

        self.assertGreaterEqual(result["kij"], -0.5)
        self.assertLessEqual(result["kij"], 0.5)
        self.assertGreater(result["point_count"], 0)
        self.assertLess(
            result["fitted_temperature_mad_K"],
            result["initial_temperature_mad_K"],
        )

    def test_unknown_mixture_is_rejected(self):
        with self.assertRaises(ValueError):
            compare_with_experiment("UNKNOWN", "PR")

        with self.assertRaises(ValueError):
            compare_with_experiment("ETHANOL_WATER", "UNKNOWN")


if __name__ == "__main__":
    unittest.main()
