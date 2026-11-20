"""Expected CANopen measurements based on ADC emulation and firmware formulas."""

from dataclasses import dataclass
import math


@dataclass(frozen=True)
class SensorTransfer:
    gain: float
    offset_mv: int

    def voltage_mv(self, value: float) -> int:
        return int(math.floor(self.offset_mv + self.gain * value + 0.5))


def adc_raw_from_mv(voltage_mv: int, resolution_bits: int, vref_mv: int = 3300) -> int:
    """Match adc_emul's integer conversion from mV to the requested ADC code."""
    if resolution_bits < 1 or voltage_mv < 0 or vref_mv <= 0:
        raise ValueError("invalid ADC conversion arguments")
    maximum = (1 << resolution_bits) - 1
    return min(maximum, voltage_mv * maximum // vref_mv)


def ph_measurement(
    raw_code: int,
    calibration_points: tuple[int, int] = (70, 140),
    calibration_values: tuple[int, int] = (2048, 3500),
) -> float:
    """Mirror ph_measurement_read(), including integer tenths in OD points."""
    point0, point1 = calibration_points
    value0, value1 = calibration_values
    if value1 == 0:
        raise ValueError("pH calibration ADC value must be non-zero")
    measured = (point0 // 10) + ((raw_code - value0) * ((point1 // 10) / value1))
    return min(14.0, max(0.0, measured))


def orp_measurement(
    raw_code: int,
    offset_mv: int = 0,
    adc_reference_mv: int = 3300,
    resolution_bits: int = 12,
) -> float:
    """Mirror the inverting 1.32 V ORP front-end and ADC conversion."""
    maximum = (1 << resolution_bits) - 1
    voltage_mv = raw_code * adc_reference_mv // maximum
    result = 1320.0 - voltage_mv - offset_mv
    return min(1000.0, max(-1000.0, result))


def expected_ph(
    ph: float,
    transfer: SensorTransfer = SensorTransfer(150.0, 600),
    resolution_bits: int = 12,
    calibration_points: tuple[int, int] = (70, 140),
    calibration_values: tuple[int, int] = (2048, 3500),
) -> float:
    voltage = transfer.voltage_mv(ph)
    raw = adc_raw_from_mv(voltage, resolution_bits)
    return ph_measurement(raw, calibration_points, calibration_values)


def expected_orp(
    orp_mv: float,
    transfer: SensorTransfer = SensorTransfer(-1.0, 1320),
    resolution_bits: int = 12,
    offset_mv: int = 0,
) -> float:
    voltage = transfer.voltage_mv(orp_mv)
    raw = adc_raw_from_mv(voltage, resolution_bits)
    return orp_measurement(raw, offset_mv=offset_mv)
