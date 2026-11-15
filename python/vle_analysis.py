"""
Сравнение расчётных диаграмм с экспериментальными данными
и оценка точности моделей.
"""

from scipy.optimize import minimize_scalar

from experimental_data import get_experimental_data
from vle_calc import (
    MIXTURES,
    MODELS,
    ChemicalConstantsPackage,
    _bubble_temperature,
    _temperature_guess,
)


def _select_points(points, max_points):
    """Равномерно выбирает не более max_points экспериментальных точек."""
    if max_points is None or max_points >= len(points):
        return list(points)

    if max_points < 2:
        raise ValueError("max_points должно быть не меньше 2")

    step = (len(points) - 1) / (max_points - 1)

    indices = sorted({
        round(i * step)
        for i in range(max_points)
    })

    return [points[index] for index in indices]


def compare_with_experiment(
    mixture,
    model,
    kij=0.0,
    max_points=None,
):
    """
    Сравнивает расчёт T–x–y с экспериментальными данными.

    mixture — код смеси.
    model — код модели.
    kij — параметр бинарного взаимодействия для Peng–Robinson.
    max_points — ограничение числа экспериментальных точек
    (для ускорения расчёта), None — использовать все точки.

    Для каждой экспериментальной точки состава жидкости вычисляется
    температура начала кипения и состав пара при экспериментальном
    давлении. Возвращаются отклонения по температуре и составу пара.
    """
    if mixture not in MIXTURES:
        raise ValueError("Неизвестная смесь: {!r}".format(mixture))

    if model not in MODELS:
        raise ValueError("Неизвестная модель: {!r}".format(model))

    data = get_experimental_data(mixture)
    pressure = data["pressure_Pa"]

    constants, properties = ChemicalConstantsPackage.from_IDs(
        list(MIXTURES[mixture])
    )

    temperature_guess = _temperature_guess(properties)

    rows = []

    for temperature_exp, x1, y1_exp in _select_points(
        data["points"],
        max_points,
    ):
        row = {
            "x1": x1,
            "temperature_exp_K": temperature_exp,
            "y1_exp": y1_exp,
        }

        try:
            temperature_calc, y1_calc = _bubble_temperature(
                constants,
                properties,
                model,
                pressure,
                x1,
                temperature_guess,
                kij=kij,
            )
        except Exception as error:
            row["error"] = str(error)
            rows.append(row)
            continue

        row["temperature_calc_K"] = temperature_calc
        row["y1_calc"] = y1_calc
        row["delta_temperature_K"] = temperature_calc - temperature_exp
        row["delta_y1"] = y1_calc - y1_exp
        rows.append(row)

    calculated = [
        row for row in rows
        if "delta_temperature_K" in row
    ]

    if calculated:
        temperature_errors = [
            abs(row["delta_temperature_K"])
            for row in calculated
        ]
        y1_errors = [
            abs(row["delta_y1"])
            for row in calculated
        ]

        temperature_mad = sum(temperature_errors) / len(temperature_errors)
        temperature_max = max(temperature_errors)
        y1_mad = sum(y1_errors) / len(y1_errors)
        y1_max = max(y1_errors)
    else:
        temperature_mad = None
        temperature_max = None
        y1_mad = None
        y1_max = None

    return {
        "mixture": mixture,
        "model": model,
        "kij": kij,
        "pressure_Pa": pressure,
        "source": data["source"],
        "point_count": len(calculated),
        "requested_point_count": len(rows),
        "temperature_mad_K": temperature_mad,
        "temperature_max_error_K": temperature_max,
        "y1_mad": y1_mad,
        "y1_max_error": y1_max,
        "points": rows,
    }


def evaluate_models(mixture, max_points=None):
    """Сравнивает с экспериментом обе модели для выбранной смеси."""
    return {
        model: compare_with_experiment(
            mixture,
            model,
            max_points=max_points,
        )
        for model in MODELS
    }


def fit_binary_interaction_parameter(
    mixture,
    model="PR",
    max_points=12,
    lower_bound=-0.5,
    upper_bound=0.5,
):
    """
    Подбирает параметр бинарного взаимодействия k_ij по эксперименту.

    Минимизируется среднее абсолютное отклонение температуры
    начала кипения. Возвращает подобранное значение и показатели
    точности до и после подбора.
    """
    if mixture not in MIXTURES:
        raise ValueError("Неизвестная смесь: {!r}".format(mixture))

    if model not in MODELS:
        raise ValueError("Неизвестная модель: {!r}".format(model))

    def objective(kij):
        stats = compare_with_experiment(
            mixture,
            model,
            kij=kij,
            max_points=max_points,
        )

        if stats["temperature_mad_K"] is None:
            return 1e6

        return stats["temperature_mad_K"]

    result = minimize_scalar(
        objective,
        bounds=(lower_bound, upper_bound),
        method="bounded",
        options={"xatol": 1e-3},
    )

    initial = compare_with_experiment(
        mixture,
        model,
        kij=0.0,
        max_points=max_points,
    )

    fitted = compare_with_experiment(
        mixture,
        model,
        kij=float(result.x),
        max_points=max_points,
    )

    return {
        "mixture": mixture,
        "model": model,
        "kij": float(result.x),
        "point_count": fitted["point_count"],
        "initial_temperature_mad_K": initial["temperature_mad_K"],
        "fitted_temperature_mad_K": fitted["temperature_mad_K"],
        "initial_y1_mad": initial["y1_mad"],
        "fitted_y1_mad": fitted["y1_mad"],
    }


def _format_number(value, digits=3):
    if value is None:
        return "—"

    return "{:.{}f}".format(value, digits)


if __name__ == "__main__":
    for mixture in ("ETHANOL_WATER", "ACETONE_CHLOROFORM"):
        print("=" * 70)
        print("Смесь:", mixture)

        for model in MODELS:
            stats = compare_with_experiment(mixture, model)

            print(
                "  {:10s} точек: {:2d}  MAD(T) = {:>6s} K  "
                "max|dT| = {:>6s} K  MAD(y1) = {:>6s}".format(
                    model,
                    stats["point_count"],
                    _format_number(stats["temperature_mad_K"]),
                    _format_number(stats["temperature_max_error_K"]),
                    _format_number(stats["y1_mad"]),
                )
            )

        fit = fit_binary_interaction_parameter(mixture)

        print(
            "  k_ij ({}) = {:+.4f}: MAD(T) {:s} -> {:s} K".format(
                fit["model"],
                fit["kij"],
                _format_number(fit["initial_temperature_mad_K"]),
                _format_number(fit["fitted_temperature_mad_K"]),
            )
        )
