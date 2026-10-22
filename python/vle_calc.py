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
