from thermo import GibbsExcessLiquid
from thermo.unifac import UNIFAC, DOUFSG, DOUFIP2016

from thermo import (
    ChemicalConstantsPackage,
    CEOSGas,
    CEOSLiquid,
    PRMIX,
    FlashVL,
)

def calculate_pr(T=350.0, P=101325.0, x_ethanol=0.5):
    """
    Тестовый расчёт равновесия этанол–вода
    по модели Peng–Robinson.

    T — температура, K
    P — давление, Pa
    x_ethanol — мольная доля этанола в исходном составе
    """
    if not 0.0 < x_ethanol < 1.0:
        raise ValueError("Мольная доля должна быть между 0 и 1")

    constants, properties = ChemicalConstantsPackage.from_IDs(
        ["ethanol", "water"]
    )

    eos_kwargs = {
        "Tcs": constants.Tcs,
        "Pcs": constants.Pcs,
        "omegas": constants.omegas,
        "kijs": [[0.0, 0.0], [0.0, 0.0]],
    }

    gas = CEOSGas(
        PRMIX,
        eos_kwargs=eos_kwargs,
        HeatCapacityGases=properties.HeatCapacityGases,
    )

    liquid = CEOSLiquid(
        PRMIX,
        eos_kwargs=eos_kwargs,
        HeatCapacityGases=properties.HeatCapacityGases,
    )

    flasher = FlashVL(
        constants,
        properties,
        gas=gas,
        liquid=liquid,
    )

    zs = [x_ethanol, 1.0 - x_ethanol]
    state = flasher.flash(T=T, P=P, zs=zs)

    return {
        "temperature_K": state.T,
        "pressure_Pa": state.P,
        "phase_count": state.phase_count,
        "feed": zs,
        "liquid": (
            list(state.liquid0.zs)
            if state.liquid_count > 0
            else None
        ),
        "vapor": (
            list(state.gas.zs)
            if state.gas_count > 0
            else None
        ),
        "vapor_fraction": state.VF,
    }


def calculate_gamma_phi(T=350.0, P=101325.0, x_ethanol=0.5):
    """
    Расчёт равновесия этанол–вода по модели gamma-phi:
    жидкость — UNIFAC Dortmund,
    пар — Peng–Robinson.

    T — температура, K
    P — давление, Pa
    x_ethanol — мольная доля этанола в исходном составе
    """
    if not 0.0 < x_ethanol < 1.0:
        raise ValueError("Мольная доля должна быть между 0 и 1")

    constants, properties = ChemicalConstantsPackage.from_IDs(
        ["ethanol", "water"]
    )

    zs = [x_ethanol, 1.0 - x_ethanol]

    eos_kwargs = {
        "Tcs": constants.Tcs,
        "Pcs": constants.Pcs,
        "omegas": constants.omegas,
        "kijs": [[0.0, 0.0], [0.0, 0.0]],
    }

    gas = CEOSGas(
        PRMIX,
        eos_kwargs=eos_kwargs,
        HeatCapacityGases=properties.HeatCapacityGases,
    )

    GE = UNIFAC.from_subgroups(
        chemgroups=constants.UNIFAC_Dortmund_groups,
        version=1,
        T=T,
        xs=zs,
        interaction_data=DOUFIP2016,
        subgroups=DOUFSG,
    )

    liquid = GibbsExcessLiquid(
        VaporPressures=properties.VaporPressures,
        HeatCapacityGases=properties.HeatCapacityGases,
        VolumeLiquids=properties.VolumeLiquids,
        GibbsExcessModel=GE,
        equilibrium_basis="Psat",
        caloric_basis="Psat",
        T=T,
        P=P,
        zs=zs,
    )

    flasher = FlashVL(
        constants,
        properties,
        liquid=liquid,
        gas=gas,
    )

    state = flasher.flash(T=T, P=P, zs=zs)

    return {
        "temperature_K": state.T,
        "pressure_Pa": state.P,
        "phase_count": state.phase_count,
        "feed": zs,
        "liquid": (
            list(state.liquid0.zs)
            if state.liquid_count > 0
            else None
        ),
        "vapor": (
            list(state.gas.zs)
            if state.gas_count > 0
            else None
        ),
        "vapor_fraction": state.VF,
    }

def calculate_bubble_point_gamma_phi(
    P=101325.0,
    x_ethanol=0.5,
    T_guess=350.0,
):
    """
    Расчёт температуры начала кипения смеси этанол–вода
    по модели gamma-phi.

    P — давление, Pa
    x_ethanol — мольная доля этанола в жидкости
    T_guess — начальная температура для настройки модели, K
    """
    if P <= 0:
        raise ValueError("Давление должно быть положительным")

    if not 0.0 <= x_ethanol <= 1.0:
        raise ValueError("Мольная доля должна быть от 0 до 1")

    constants, properties = ChemicalConstantsPackage.from_IDs(
        ["ethanol", "water"]
    )

    zs = [x_ethanol, 1.0 - x_ethanol]

    eos_kwargs = {
        "Tcs": constants.Tcs,
        "Pcs": constants.Pcs,
        "omegas": constants.omegas,
        "kijs": [[0.0, 0.0], [0.0, 0.0]],
    }

    gas = CEOSGas(
        PRMIX,
        eos_kwargs=eos_kwargs,
        HeatCapacityGases=properties.HeatCapacityGases,
    )

    GE = UNIFAC.from_subgroups(
        chemgroups=constants.UNIFAC_Dortmund_groups,
        version=1,
        T=T_guess,
        xs=zs,
        interaction_data=DOUFIP2016,
        subgroups=DOUFSG,
    )

    liquid = GibbsExcessLiquid(
        VaporPressures=properties.VaporPressures,
        HeatCapacityGases=properties.HeatCapacityGases,
        VolumeLiquids=properties.VolumeLiquids,
        GibbsExcessModel=GE,
        equilibrium_basis="Psat",
        caloric_basis="Psat",
        T=T_guess,
        P=P,
        zs=zs,
    )

    flasher = FlashVL(
        constants,
        properties,
        liquid=liquid,
        gas=gas,
    )

    state = flasher.flash(P=P, VF=0.0, zs=zs)

    return {
        "temperature_K": state.T,
        "temperature_C": state.T - 273.15,
        "pressure_Pa": state.P,
        "liquid": list(state.liquid0.zs) if state.liquid_count else None,
        "vapor": list(state.gas.zs) if state.gas_count else None,
    }


# Коды смесей и соответствующие им компоненты thermo
MIXTURES = {
    "ETHANOL_WATER": ("ethanol", "water"),
    "ACETONE_CHLOROFORM": ("acetone", "chloroform"),
}

# Коды поддерживаемых термодинамических моделей
MODELS = ("PR", "UNIFAC_PR")


def _validate_mixture_and_model(mixture, model):
    """Проверяет коды смеси и модели, возвращает компоненты смеси."""
    if mixture not in MIXTURES:
        raise ValueError(
            "Неизвестная смесь: {!r}. Допустимые значения: {}".format(
                mixture, ", ".join(sorted(MIXTURES))
            )
        )

    if model not in MODELS:
        raise ValueError(
            "Неизвестная модель: {!r}. Допустимые значения: {}".format(
                model, ", ".join(MODELS)
            )
        )

    return MIXTURES[mixture]


def _validate_number(value, name, positive=True):
    """Проверяет, что значение является положительным числом."""
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ValueError("{} должно быть числом".format(name))

    if positive and value <= 0.0:
        raise ValueError("{} должно быть положительным".format(name))


def _create_flasher(
    constants,
    properties,
    model,
    temperature,
    pressure,
    zs,
    kij=0.0,
):
    """
    Создаёт объект FlashVL для заданной модели.

    Модель PR — уравнение Peng–Robinson для обеих фаз (phi-phi).
    Модель UNIFAC_PR — UNIFAC Dortmund для жидкости и Peng–Robinson
    для пара (gamma-phi).

    kij — параметр бинарного взаимодействия для Peng–Robinson.
    """
    eos_kwargs = {
        "Tcs": constants.Tcs,
        "Pcs": constants.Pcs,
        "omegas": constants.omegas,
        "kijs": [[0.0, kij], [kij, 0.0]],
    }

    gas = CEOSGas(
        PRMIX,
        eos_kwargs=eos_kwargs,
        HeatCapacityGases=properties.HeatCapacityGases,
    )

    if model == "PR":
        liquid = CEOSLiquid(
            PRMIX,
            eos_kwargs=eos_kwargs,
            HeatCapacityGases=properties.HeatCapacityGases,
        )
    else:
        GE = UNIFAC.from_subgroups(
            chemgroups=constants.UNIFAC_Dortmund_groups,
            version=1,
            T=temperature,
            xs=zs,
            interaction_data=DOUFIP2016,
            subgroups=DOUFSG,
        )

        liquid = GibbsExcessLiquid(
            VaporPressures=properties.VaporPressures,
            HeatCapacityGases=properties.HeatCapacityGases,
            VolumeLiquids=properties.VolumeLiquids,
            GibbsExcessModel=GE,
            equilibrium_basis="Psat",
            caloric_basis="Psat",
            T=temperature,
            P=pressure,
            zs=zs,
        )

    return FlashVL(
        constants,
        properties,
        liquid=liquid,
        gas=gas,
    )


def _bubble_temperature(
    constants,
    properties,
    model,
    pressure_Pa,
    x1,
    temperature_guess,
    kij=0.0,
):
    """
    Температура начала кипения жидкости состава x1 при заданном давлении.

    Возвращает (температура, K; мольная доля первого компонента в паре).
    """
    # thermo иногда не сходится в отдельных точках. Пробуем немного
    # сдвинуть состав, чтобы обойти неудачное состояние решателя.
    for shift in (0.0, -1e-7, 1e-7, -1e-6, 1e-6):
        composition = min(max(x1 + shift, 1e-12), 1.0 - 1e-12)
        zs = [composition, 1.0 - composition]

        flasher = _create_flasher(
            constants,
            properties,
            model,
            temperature_guess,
            pressure_Pa,
            zs,
            kij=kij,
        )

        try:
            state = flasher.flash(P=pressure_Pa, VF=0.0, zs=zs)
        except Exception:
            continue

        if state.gas is None or state.gas_count == 0:
            continue

        return float(state.T), float(state.gas.zs[0])

    raise RuntimeError(
        "Не удалось рассчитать температуру начала кипения "
        "при x1 = {:.6f}".format(x1)
    )


def _bubble_pressure(
    constants,
    properties,
    model,
    temperature_K,
    x1,
    kij=0.0,
):
    """
    Давление начала кипения жидкости состава x1 при заданной температуре.

    Возвращает (давление, Pa; мольная доля первого компонента в паре).
    """
    for shift in (0.0, -1e-7, 1e-7, -1e-6, 1e-6):
        composition = min(max(x1 + shift, 1e-12), 1.0 - 1e-12)
        zs = [composition, 1.0 - composition]

        flasher = _create_flasher(
            constants,
            properties,
            model,
            temperature_K,
            101325.0,
            zs,
            kij=kij,
        )

        try:
            state = flasher.flash(T=temperature_K, VF=0.0, zs=zs)
        except Exception:
            continue

        if state.gas is None or state.gas_count == 0:
            continue

        return float(state.P), float(state.gas.zs[0])

    raise RuntimeError(
        "Не удалось рассчитать давление начала кипения "
        "при x1 = {:.6f}".format(x1)
    )


def _composition_grid(n_points):
    """Равномерная сетка мольных долей первого компонента от 0 до 1."""
    return [i / (n_points - 1) for i in range(n_points)]


def _clamp_vapor_composition(y1, x1):
    """Ограничивает состав пара диапазоном [0, 1] и чистыми концами."""
    if x1 <= 0.0:
        return 0.0

    if x1 >= 1.0:
        return 1.0

    return min(max(y1, 0.0), 1.0)


def _temperature_guess(properties):
    """Средняя нормальная температура кипения компонентов, K."""
    boiling_points = [
        vapor_pressure.Tb
        for vapor_pressure in properties.VaporPressures
        if vapor_pressure.Tb is not None
    ]

    if not boiling_points:
        return 350.0

    return float(sum(boiling_points) / len(boiling_points))


def _detect_azeotrope(xs, ys, values, evaluate=None):
    """
    Ищет азеотропную точку по изменению знака разности y1 - x1.

    xs, ys — составы жидкости и пара, values — температуры или давления.
    evaluate(x) необязательно выполняет расчёт равновесия при заданном
    составе жидкости и возвращает (температура или давление, y1);
    используется только для проверки крайних интервалов.

    Возвращает (значение, x1) или None, если азеотроп не найден.
    """
    points = [
        [x, y - x, value]
        for x, y, value in zip(xs, ys, values)
    ]

    # На границах составов y1 = x1, но азеотропом чистые компоненты
    # не являются. Проверяем середину крайних интервалов, чтобы
    # не пропустить азеотроп вблизи чистого компонента.
    if evaluate is not None and len(points) >= 3:
        probes = [
            0.5 * (points[0][0] + points[1][0]),
            0.5 * (points[-1][0] + points[-2][0]),
        ]

        for x_probe in probes:
            try:
                value, y_probe = evaluate(x_probe)
            except Exception:
                continue

            points.append([x_probe, y_probe - x_probe, value])

        points.sort(key=lambda point: point[0])

    # В чистых компонентах азеотропа нет, а разность y1 - x1 там равна
    # нулю лишь приближённо. Обнуляем её, чтобы не получить ложное
    # пересечение у самой границы состава.
    points[0][1] = 0.0
    points[-1][1] = 0.0

    best = None

    for left, right in zip(points, points[1:]):
        x_left, difference_left, value_left = left
        x_right, difference_right, value_right = right

        if difference_left * difference_right >= 0.0:
            continue

        fraction = difference_left / (
            difference_left - difference_right
        )

        x_az = x_left + fraction * (x_right - x_left)
        value_az = value_left + fraction * (value_right - value_left)

        # Из нескольких пересечений выбираем наиболее плавное:
        # у азеотропа кривые подходят к диагонали почти касаясь.
        scale = abs(difference_left) + abs(difference_right)

        if best is None or scale < best[0]:
            best = (scale, value_az, x_az)

    if best is None:
        return None

    return best[1], best[2]


def calculate_txy(
    temperature_min_K,
    temperature_max_K,
    pressure_Pa,
    n_points=51,
    mixture="ETHANOL_WATER",
    model="PR",
    kij=0.0,
):
    """
    Расчёт диаграммы T–x–y при заданном давлении.

    temperature_min_K, temperature_max_K — границы диапазона температур
    диаграммы, K (используются для проверки аргументов; расчётные точки
    строятся по составу жидкости).

    pressure_Pa — давление, Pa.
    n_points — количество точек по составу жидкости.
    mixture — код смеси: ETHANOL_WATER или ACETONE_CHLOROFORM.
    model — код модели: PR или UNIFAC_PR.
    kij — параметр бинарного взаимодействия для Peng–Robinson.

    Возвращает словарь:
        {
            "temperature_K": [...],
            "x1": [...],
            "y1": [...],
            "azeotrope": {"temperature_K": ..., "x1": ...} или None
        }
    """
    _validate_number(temperature_min_K, "Минимальная температура")
    _validate_number(temperature_max_K, "Максимальная температура")
    _validate_number(pressure_Pa, "Давление")

    if temperature_min_K >= temperature_max_K:
        raise ValueError(
            "Минимальная температура должна быть меньше максимальной"
        )

    if not isinstance(n_points, int) or isinstance(n_points, bool):
        raise ValueError("Количество точек должно быть целым числом")

    if n_points < 2:
        raise ValueError("Количество точек должно быть не меньше 2")

    components = _validate_mixture_and_model(mixture, model)

    constants, properties = ChemicalConstantsPackage.from_IDs(
        list(components)
    )

    temperature_guess = _temperature_guess(properties)

    temperatures = []
    xs = []
    ys = []

    for x1 in _composition_grid(n_points):
        try:
            temperature, y1 = _bubble_temperature(
                constants,
                properties,
                model,
                pressure_Pa,
                x1,
                temperature_guess,
                kij=kij,
            )
        except Exception:
            # Не сошедшиеся точки пропускаем, чтобы не прерывать расчёт
            continue

        temperatures.append(temperature)
        xs.append(float(x1))
        ys.append(_clamp_vapor_composition(y1, x1))

    if len(temperatures) < 2:
        raise RuntimeError(
            "Не удалось рассчитать диаграмму T–x–y: "
            "нет сошедшихся точек"
        )

    azeotrope = _detect_azeotrope(
        xs,
        ys,
        temperatures,
        lambda x: _bubble_temperature(
            constants,
            properties,
            model,
            pressure_Pa,
            x,
            temperature_guess,
            kij=kij,
        ),
    )

    return {
        "temperature_K": temperatures,
        "x1": xs,
        "y1": ys,
        "azeotrope": (
            {
                "temperature_K": azeotrope[0],
                "x1": azeotrope[1],
            }
            if azeotrope is not None
            else None
        ),
    }


def calculate_pxy(
    temperature_K,
    pressure_min_Pa,
    pressure_max_Pa,
    n_points=51,
    mixture="ETHANOL_WATER",
    model="PR",
    kij=0.0,
):
    """
    Расчёт диаграммы P–x–y при заданной температуре.

    temperature_K — температура, K.
    pressure_min_Pa, pressure_max_Pa — границы диапазона давлений
    диаграммы, Pa (используются для проверки аргументов; расчётные точки
    строятся по составу жидкости).

    n_points — количество точек по составу жидкости.
    mixture — код смеси: ETHANOL_WATER или ACETONE_CHLOROFORM.
    model — код модели: PR или UNIFAC_PR.
    kij — параметр бинарного взаимодействия для Peng–Robinson.

    Возвращает словарь:
        {
            "pressure_Pa": [...],
            "x1": [...],
            "y1": [...],
            "azeotrope": {"pressure_Pa": ..., "x1": ...} или None
        }
    """
    _validate_number(temperature_K, "Температура")
    _validate_number(pressure_min_Pa, "Минимальное давление")
    _validate_number(pressure_max_Pa, "Максимальное давление")

    if pressure_min_Pa >= pressure_max_Pa:
        raise ValueError(
            "Минимальное давление должно быть меньше максимального"
        )

    if not isinstance(n_points, int) or isinstance(n_points, bool):
        raise ValueError("Количество точек должно быть целым числом")

    if n_points < 2:
        raise ValueError("Количество точек должно быть не меньше 2")

    components = _validate_mixture_and_model(mixture, model)

    constants, properties = ChemicalConstantsPackage.from_IDs(
        list(components)
    )

    pressures = []
    xs = []
    ys = []

    for x1 in _composition_grid(n_points):
        try:
            pressure, y1 = _bubble_pressure(
                constants,
                properties,
                model,
                temperature_K,
                x1,
                kij=kij,
            )
        except Exception:
            # Не сошедшиеся точки пропускаем, чтобы не прерывать расчёт
            continue

        pressures.append(pressure)
        xs.append(float(x1))
        ys.append(_clamp_vapor_composition(y1, x1))

    if len(pressures) < 2:
        raise RuntimeError(
            "Не удалось рассчитать диаграмму P–x–y: "
            "нет сошедшихся точек"
        )

    azeotrope = _detect_azeotrope(
        xs,
        ys,
        pressures,
        lambda x: _bubble_pressure(
            constants,
            properties,
            model,
            temperature_K,
            x,
            kij=kij,
        ),
    )

    return {
        "pressure_Pa": pressures,
        "x1": xs,
        "y1": ys,
        "azeotrope": (
            {
                "pressure_Pa": azeotrope[0],
                "x1": azeotrope[1],
            }
            if azeotrope is not None
            else None
        ),
    }


def calculate_point(
    T=350.0,
    P=101325.0,
    x1=0.5,
    mixture="ETHANOL_WATER",
    model="PR",
    kij=0.0,
):
    """
    Расчёт равновесия в одной точке для выбранной смеси и модели.

    T — температура, K.
    P — давление, Pa.
    x1 — мольная доля первого компонента в исходном составе.
    mixture — код смеси: ETHANOL_WATER или ACETONE_CHLOROFORM.
    model — код модели: PR или UNIFAC_PR.
    kij — параметр бинарного взаимодействия для Peng–Robinson.

    Возвращает словарь с параметрами состояния и составами фаз.
    """
    _validate_number(T, "Температура")
    _validate_number(P, "Давление")

    if not 0.0 < x1 < 1.0:
        raise ValueError("Мольная доля должна быть между 0 и 1")

    components = _validate_mixture_and_model(mixture, model)

    constants, properties = ChemicalConstantsPackage.from_IDs(
        list(components)
    )

    zs = [x1, 1.0 - x1]

    flasher = _create_flasher(
        constants,
        properties,
        model,
        T,
        P,
        zs,
        kij=kij,
    )

    state = flasher.flash(T=T, P=P, zs=zs)

    return {
        "mixture": mixture,
        "model": model,
        "temperature_K": state.T,
        "pressure_Pa": state.P,
        "phase_count": state.phase_count,
        "feed": zs,
        "liquid": (
            list(state.liquid0.zs)
            if state.liquid_count > 0
            else None
        ),
        "vapor": (
            list(state.gas.zs)
            if state.gas_count > 0
            else None
        ),
        "vapor_fraction": state.VF,
    }


def calculate_azeotrope(
    pressure_Pa=101325.0,
    mixture="ETHANOL_WATER",
    model="PR",
    n_points=101,
    kij=0.0,
):
    """
    Определяет азеотропную точку при заданном давлении.

    Возвращает словарь с температурой, составом и давлением азеотропа
    или None, если азеотроп в выбранной модели отсутствует.
    """
    _validate_number(pressure_Pa, "Давление")

    result = calculate_txy(
        250.0,
        700.0,
        pressure_Pa,
        n_points,
        mixture,
        model,
        kij=kij,
    )

    azeotrope = result["azeotrope"]

    if azeotrope is None:
        return None

    return {
        "temperature_K": azeotrope["temperature_K"],
        "pressure_Pa": pressure_Pa,
        "x1": azeotrope["x1"],
    }


if __name__ == "__main__":
    result = calculate_pr()

    print("Модель: Peng–Robinson (phi-phi)")
    print(f"Температура: {result['temperature_K']:.2f} K")
    print(f"Давление: {result['pressure_Pa']:.2f} Pa")
    print(f"Количество фаз: {result['phase_count']}")
    print(f"Исходный состав: {result['feed']}")
    print(f"Состав жидкости: {result['liquid']}")
    print(f"Состав пара: {result['vapor']}")
    print(f"Доля пара: {result['vapor_fraction']}")
