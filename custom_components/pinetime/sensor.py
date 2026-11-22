"""Battery sensor entity for the PineTime Smartwatch integration."""
from __future__ import annotations

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import PERCENTAGE
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import PineTimeCoordinator
from .const import DOMAIN


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up the PineTime battery sensor."""
    coordinator: PineTimeCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([PineTimeBatterySensor(coordinator)])


class PineTimeBatterySensor(SensorEntity):
    """Representation of the PineTime Battery Level."""

    _attr_has_entity_name = True
    _attr_name = "Battery"
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_native_unit_of_measurement = PERCENTAGE

    def __init__(self, coordinator: PineTimeCoordinator) -> None:
        """Initialize the battery sensor."""
        self.coordinator = coordinator
        self._attr_unique_id = f"{coordinator.address}_battery"
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
    def native_value(self) -> int | None:
        """Return the battery level."""
        return self.coordinator.battery_level
