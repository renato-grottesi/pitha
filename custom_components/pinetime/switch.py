"""Switch entities for the PineTime Smartwatch integration."""
from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import PineTimeCoordinator
from .const import DOMAIN, SWITCH_NAMES


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up the 6 PineTime switch entities."""
    coordinator: PineTimeCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(
        [PineTimeSwitch(coordinator, i) for i in range(len(SWITCH_NAMES))]
    )


class PineTimeSwitch(SwitchEntity):
    """Representation of a PineTime Switch."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: PineTimeCoordinator, idx: int) -> None:
        """Initialize the switch entity."""
        self.coordinator = coordinator
        self._idx = idx
        self._attr_unique_id = f"{coordinator.address}_switch_{idx}"
        self._attr_device_info = coordinator.device_info

    @property
    def name(self) -> str:
        """Return the dynamic name configured for this switch slot."""
        return f"Switch {self.coordinator.get_switch_name(self._idx)}"

    async def async_added_to_hass(self) -> None:
        """Register state update callback."""
        self.async_on_remove(
            self.coordinator.async_add_listener(self._handle_coordinator_update)
        )

    @callback
    def _handle_coordinator_update(self) -> None:
        self.async_write_ha_state()

    @property
    def is_on(self) -> bool:
        """Return true if switch is on."""
        return self.coordinator.switch_states[self._idx]

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn the switch on."""
        await self.coordinator.async_set_switch_from_ha(self._idx, True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn the switch off."""
        await self.coordinator.async_set_switch_from_ha(self._idx, False)
