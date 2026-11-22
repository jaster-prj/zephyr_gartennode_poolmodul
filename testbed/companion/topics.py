"""Module-scoped MQTT topic construction."""

from dataclasses import dataclass
import re

_IDENTIFIER = re.compile(r"^[A-Za-z0-9_-]+$")


def _validate_identifier(value: str, name: str) -> str:
    if not _IDENTIFIER.fullmatch(value):
        raise ValueError(f"{name} must contain only letters, digits, '_' or '-'")
    return value


@dataclass(frozen=True)
class ModuleTopics:
    temperature_environment: str
    temperature_rom_id: str
    ph_input: str
    orp_input: str
    ph_state: str
    orp_state: str
    actor_state: str

    @classmethod
    def for_module(cls, garden_id: str, module_id: str) -> "ModuleTopics":
        garden = _validate_identifier(garden_id, "garden_id")
        module = _validate_identifier(module_id, "module_id")
        root = f"garden/{garden}/modules/{module}"
        return cls(
            temperature_environment=f"garden/{garden}/environment/temperature",
            temperature_rom_id=f"{root}/input/temperature_rom_id",
            ph_input=f"{root}/input/ph",
            orp_input=f"{root}/input/orp",
            ph_state=f"{root}/state/measurement/ph",
            orp_state=f"{root}/state/measurement/orp",
            actor_state=f"{root}/state/actor/#",
        )
