"""Config flow for PineTime Smartwatch integration."""
from __future__ import annotations

from typing import Any
import voluptuous as vol

from homeassistant.components.bluetooth import (
    BluetoothServiceInfoBleak,
    async_discovered_service_info,
)
from homeassistant.config_entries import ConfigEntry, ConfigFlow, OptionsFlow
from homeassistant.const import CONF_ADDRESS
from homeassistant.core import callback
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers import selector

from .const import DOMAIN, LIGHT_NAMES, SWITCH_NAMES, TEMP_DEFAULT_NAMES


class PineTimeConfigFlow(ConfigFlow, domain=DOMAIN):
    """Handle a config flow for PineTime Smartwatch."""

    VERSION = 1

    def __init__(self) -> None:
        """Initialize the config flow."""
        self._discovery_info: BluetoothServiceInfoBleak | None = None
        self._discovered_devices: dict[str, BluetoothServiceInfoBleak] = {}

    async def async_step_bluetooth(
        self, discovery_info: BluetoothServiceInfoBleak
    ) -> FlowResult:
        """Handle the bluetooth discovery step."""
        await self.async_set_unique_id(discovery_info.address.upper())
        self._abort_if_unique_id_configured()
        self._discovery_info = discovery_info
        self.context["title_placeholders"] = {
            "name": f"{discovery_info.name} ({discovery_info.address})"
        }
        return await self.async_step_bluetooth_confirm()

    async def async_step_bluetooth_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Confirm discovery."""
        assert self._discovery_info is not None
        if user_input is not None:
            return self.async_create_entry(
                title=f"PineTime ({self._discovery_info.address})",
                data={CONF_ADDRESS: self._discovery_info.address.upper()},
            )

        self._set_confirm_only()
        return self.async_show_form(
            step_id="bluetooth_confirm",
            description_placeholders={
                "name": f"{self._discovery_info.name} ({self._discovery_info.address})"
            },
        )

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Handle the user step to pick discovered device or enter MAC."""
        if user_input is not None:
            address = user_input[CONF_ADDRESS].strip().upper()
            await self.async_set_unique_id(address, raise_on_progress=False)
            self._abort_if_unique_id_configured()
            return self.async_create_entry(
                title=f"PineTime ({address})",
                data={CONF_ADDRESS: address},
            )

        current_addresses = self._async_current_ids()
        for info in async_discovered_service_info(self.hass):
            addr = info.address.upper()
            if addr in current_addresses:
                continue
            if info.name and ("infinitime" in info.name.lower() or "pinetime" in info.name.lower()):
                self._discovered_devices[addr] = info

        if self._discovered_devices:
            options = {
                addr: f"{info.name} ({addr})"
                for addr, info in self._discovered_devices.items()
            }
            schema = vol.Schema({vol.Required(CONF_ADDRESS): vol.In(options)})
        else:
            schema = vol.Schema({vol.Required(CONF_ADDRESS): str})

        return self.async_show_form(step_id="user", data_schema=schema)

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> OptionsFlow:
        """Get the options flow for this handler."""
        return PineTimeOptionsFlow(config_entry)


class PineTimeOptionsFlow(OptionsFlow):
    """Handle options for custom names (up to 8 chars) and linked HA entities."""

    def __init__(self, config_entry: ConfigEntry) -> None:
        """Initialize options flow."""
        self._config_entry = config_entry

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Manage the PineTime names and entity mappings."""
        if user_input is not None:
            cleaned: dict[str, Any] = {}
            for k, v in user_input.items():
                if v in ("", None):
                    continue
                if k.endswith("_name") and isinstance(v, str):
                    cleaned[k] = v.strip()[:8]
                else:
                    cleaned[k] = v
            return self.async_create_entry(title="", data=cleaned)

        opts = self._config_entry.options
        schema_dict: dict[Any, Any] = {}

        def _opt_entity(key: str) -> vol.Optional:
            val = opts.get(key)
            if val:
                return vol.Optional(key, description={"suggested_value": val})
            return vol.Optional(key)

        # 6 Lights (Name + Entity)
        for i, default_name in enumerate(LIGHT_NAMES):
            name_key = f"light_{i}_name"
            ent_key = f"light_{i}"
            schema_dict[
                vol.Optional(name_key, default=opts.get(name_key, default_name))
            ] = str
            schema_dict[_opt_entity(ent_key)] = selector.EntitySelector(
                selector.EntitySelectorConfig(domain=["light"], multiple=False)
            )

        # 6 Switches (Name + Entity)
        for i, default_name in enumerate(SWITCH_NAMES):
            name_key = f"switch_{i}_name"
            ent_key = f"switch_{i}"
            schema_dict[
                vol.Optional(name_key, default=opts.get(name_key, default_name))
            ] = str
            schema_dict[_opt_entity(ent_key)] = selector.EntitySelector(
                selector.EntitySelectorConfig(
                    domain=["switch", "input_boolean", "light", "script", "automation"],
                    multiple=False,
                )
            )

        # Thermostat climate entity & optional current temperature sensor
        schema_dict[_opt_entity("climate_entity")] = selector.EntitySelector(
            selector.EntitySelectorConfig(domain=["climate"], multiple=False)
        )
        schema_dict[_opt_entity("thermostat_temp_sensor")] = selector.EntitySelector(
            selector.EntitySelectorConfig(domain=["sensor", "input_number"], multiple=False)
        )

        # 6 Temperature Reader slots (Name + Sensor)
        for i, default_name in enumerate(TEMP_DEFAULT_NAMES):
            name_key = f"temp_{i}_name"
            sensor_key = f"temp_{i}_sensor"
            schema_dict[
                vol.Optional(name_key, default=opts.get(name_key, default_name))
            ] = str
            schema_dict[_opt_entity(sensor_key)] = selector.EntitySelector(
                selector.EntitySelectorConfig(domain=["sensor", "input_number", "weather"], multiple=False)
            )

        return self.async_show_form(
            step_id="init",
            data_schema=vol.Schema(schema_dict),
        )
