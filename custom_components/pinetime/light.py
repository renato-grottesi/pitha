"""Light entities for the PineTime Smartwatch integration."""
from __future__ import annotations

from typing import Any

from homeassistant.components.light import (
    ATTR_BRIGHTNESS,
    ATTR_COLOR_TEMP_KELVIN,
    ColorMode,
    LightEntity,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import PineTimeCoordinator, _kelvin_to_pct_mired_interp, _pct_to_kelvin_mired_interp
from .const import DOMAIN, LIGHT_NAMES


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up the 6 PineTime light entities."""
    coordinator: PineTimeCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities(
        [PineTimeLight(coordinator, i) for i in range(len(LIGHT_NAMES))]
    )


class PineTimeLight(LightEntity):
    """Representation of a PineTime Light Group."""

    _attr_has_entity_name = True
    _attr_supported_color_modes = {ColorMode.COLOR_TEMP}
    _attr_color_mode = ColorMode.COLOR_TEMP
    _attr_min_color_temp_kelvin = 2200
    _attr_max_color_temp_kelvin = 6500

    def __init__(self, coordinator: PineTimeCoordinator, idx: int) -> None:
        """Initialize the light entity."""
        self.coordinator = coordinator
        self._idx = idx
        self._attr_unique_id = f"{coordinator.address}_light_{idx}"
        self._attr_device_info = coordinator.device_info

    @property
    def name(self) -> str:
        """Return the dynamic name configured for this light slot."""
        return f"Light {self.coordinator.get_light_name(self._idx)}"

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
        """Return true if light is on."""
        return self.coordinator.light_brightness[self._idx] > 0

    @property
    def brightness(self) -> int:
        """Return the brightness (0..255)."""
        pct = self.coordinator.light_brightness[self._idx]
        return round(pct * 255 / 100)

    @property
    def color_temp_kelvin(self) -> int:
        """Return the color temperature in Kelvin (2200..6500)."""
        pct = self.coordinator.light_color_temp[self._idx]
        return _pct_to_kelvin_mired_interp(pct, 2200, 6500)

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn the light on or adjust brightness/color temp without forcing stale watch attributes."""
        br_pct: int | None = None
        if ATTR_BRIGHTNESS in kwargs:
            br_pct = max(1, round(int(kwargs[ATTR_BRIGHTNESS]) * 100 / 255))

        ct_pct: int | None = None
        if ATTR_COLOR_TEMP_KELVIN in kwargs:
            kelvin = int(kwargs[ATTR_COLOR_TEMP_KELVIN])
            ct_pct = _kelvin_to_pct_mired_interp(kelvin, 2200, 6500)

        await self.coordinator.async_set_light_from_ha(
            self._idx, br_pct, ct_pct, turn_off=False
        )

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn the light off."""
        await self.coordinator.async_set_light_from_ha(
            self._idx, 0, None, turn_off=True
        )
