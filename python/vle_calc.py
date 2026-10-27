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
