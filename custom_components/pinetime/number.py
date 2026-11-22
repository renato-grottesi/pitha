"""Thermostat target number entity for the PineTime Smartwatch integration."""
from __future__ import annotations

from homeassistant.components.number import NumberDeviceClass, NumberEntity, NumberMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfTemperature
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import PineTimeCoordinator
from .const import DOMAIN


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up the PineTime thermostat target number entity."""
    coordinator: PineTimeCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([PineTimeThermostatNumber(coordinator)])


class PineTimeThermostatNumber(NumberEntity):
    """Representation of the PineTime Thermostat Target (2..21 °C)."""

    _attr_has_entity_name = True
    _attr_name = "Thermostat Target"
    _attr_device_class = NumberDeviceClass.TEMPERATURE
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_native_min_value = 2.0
    _attr_native_max_value = 21.0
    _attr_native_step = 1.0
    _attr_mode = NumberMode.SLIDER

    def __init__(self, coordinator: PineTimeCoordinator) -> None:
        """Initialize the thermostat target entity."""
        self.coordinator = coordinator
        self._attr_unique_id = f"{coordinator.address}_thermostat_target"
        self._attr_device_info = coordinator.device_info

    async def async_added_to_hass(self) -> None:
        """Register state update callback."""
        self.async_on_remove(
            self.coordinator.async_add_listener(self._handle_coordinator_update)
        )

    @callback
    def _handle_coordinator_update(self) -> None:
        self.async_write_ha_state()

    @property
    def native_value(self) -> float:
        """Return the current target temperature."""
        return float(self.coordinator.thermostat_target)

    async def async_set_native_value(self, value: float) -> None:
        """Update the target temperature."""
        await self.coordinator.async_set_thermostat_from_ha(round(value))
