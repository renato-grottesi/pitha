"""The PineTime Smartwatch Bluetooth integration for Home Assistant."""
from __future__ import annotations

import asyncio
from collections import deque
from datetime import datetime, timedelta
import logging
import struct
import time
from typing import Any, Callable

from bleak import BleakClient
from bleak.exc import BleakError
from bleak_retry_connector import establish_connection

from homeassistant.components import bluetooth
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import (
    ATTR_ENTITY_ID,
    CONF_ADDRESS,
    EVENT_HOMEASSISTANT_STOP,
    STATE_ON,
    STATE_UNAVAILABLE,
    STATE_UNKNOWN,
    Platform,
)
from homeassistant.core import CALLBACK_TYPE, Event, HomeAssistant, State, callback
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.event import (
    async_call_later,
    async_track_state_change_event,
    async_track_time_interval,
)
from homeassistant.util import dt as dt_util

from .const import (
    BATTERY_LEVEL_UUID,
    CMD_LIGHT,
    CMD_REFRESH,
    CMD_SWITCH,
    CMD_THERMOSTAT,
    CTS_CURRENT_TIME_UUID,
    DOMAIN,
    EVENT_PINETIME_COMMAND,
    HA_CMD_CHAR_UUID,
    HA_STATE_CHAR_UUID,
    LIGHT_NAMES,
    SWITCH_NAMES,
    TEMP_DEFAULT_NAMES,
)

_LOGGER = logging.getLogger(__name__)

PLATFORMS: list[Platform] = [
    Platform.LIGHT,
    Platform.SWITCH,
    Platform.NUMBER,
    Platform.SENSOR,
]


def _light_kelvin_bounds(st: State | None) -> tuple[int, int]:
    """Return (min_kelvin, max_kelvin) for a light state, using mireds if needed."""
    min_k = 2200
    max_k = 4000
    if st is not None:
        attr_min_k = st.attributes.get("min_color_temp_kelvin")
        attr_max_k = st.attributes.get("max_color_temp_kelvin")
        if attr_min_k is not None and attr_max_k is not None:
            min_k = int(attr_min_k)
            max_k = int(attr_max_k)
        else:
            # Fallback to mireds attributes if present
            max_mireds = st.attributes.get("max_mireds")
            min_mireds = st.attributes.get("min_mireds")
            if max_mireds:
                min_k = max(1500, round(1_000_000 / int(max_mireds)))
            if min_mireds:
                max_k = min(9000, round(1_000_000 / int(min_mireds)))
    if max_k <= min_k:
        max_k = min_k + 1000
    return min_k, max_k


def _pct_to_kelvin_mired_interp(pct: int, min_k: int, max_k: int) -> int:
    """Convert 0..100% (0=warmest/red-ish, 100=coolest) to Kelvin via linear Mired interpolation."""
    pct = max(0, min(100, pct))
    if pct <= 2:
        return min_k
    if pct >= 98:
        return max_k
    max_mireds = 1_000_000.0 / max(1000, min_k)
    min_mireds = 1_000_000.0 / max(min_k + 1, max_k)
    target_mireds = max_mireds - (pct / 100.0) * (max_mireds - min_mireds)
    kelvin = round(1_000_000.0 / max(1.0, target_mireds))
    return max(min_k, min(max_k, kelvin))


def _kelvin_to_pct_mired_interp(kelvin: int, min_k: int, max_k: int) -> int:
    """Convert Kelvin to 0..100% (0=warmest, 100=coolest) via linear Mired interpolation."""
    if kelvin <= min_k:
        return 0
    if kelvin >= max_k:
        return 100
    max_mireds = 1_000_000.0 / max(1000, min_k)
    min_mireds = 1_000_000.0 / max(min_k + 1, max_k)
    cur_mireds = 1_000_000.0 / max(1000, kelvin)
    span = max(1.0, max_mireds - min_mireds)
    pct = round(((max_mireds - cur_mireds) * 100.0) / span)
    return max(0, min(100, pct))


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up PineTime Smartwatch from a config entry."""
    address: str = entry.data[CONF_ADDRESS].upper()
    coordinator = PineTimeCoordinator(hass, entry, address)
    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = coordinator

    await coordinator.async_start()
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    entry.async_on_unload(entry.add_update_listener(_async_update_options))
    return True


async def _async_update_options(hass: HomeAssistant, entry: ConfigEntry) -> None:
    """Handle options update."""
    coordinator: PineTimeCoordinator = hass.data[DOMAIN][entry.entry_id]
    await coordinator.async_reload_options()


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    if unload_ok := await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        coordinator: PineTimeCoordinator = hass.data[DOMAIN].pop(entry.entry_id)
        await coordinator.async_stop()
    return unload_ok


class PineTimeCoordinator:
    """Manages the BLE connection and state synchronization with the PineTime watch."""

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry, address: str) -> None:
        """Initialize the coordinator."""
        self.hass = hass
        self.entry = entry
        self.address = address

        self.device_info = DeviceInfo(
            identifiers={(DOMAIN, address)},
            name=f"PineTime ({address[-5:]})",
            manufacturer="Pine64",
            model="PineTime (InfiniTime)",
        )

        # Internal state mirrored with the watch
        self.light_brightness: list[int] = [80, 80, 100, 50, 90, 80]
        self.light_color_temp: list[int] = [40, 30, 60, 20, 65, 40]  # 0=Warm, 100=Cool
        self.switch_states: list[bool] = [False, False, True, False, True, False]
        self.thermostat_target: int = 19
        self.thermostat_current: float | None = None
        self.battery_level: int | None = None
        self.connected: bool = False

        self._client: BleakClient | None = None
        self._connect_lock = asyncio.Lock()
        self._write_lock = asyncio.Lock()
        self._cmd_lock = asyncio.Lock()
        self._connect_task: asyncio.Task[None] | None = None
        self._Stopping = False

        # Strict packet ID deduplication so each watch packet ID is processed at most once
        self._processed_seqs: deque[int] = deque(maxlen=256)
        self._pending_light_cmds: dict[int, tuple[int, int]] = {}
        self._light_worker_task: asyncio.Task[None] | None = None

        # Debounce & echo-suppression for overlapping light groups (A, B, C, A+B+C, D+E+F, A..F)
        self._ignore_state_changes_until: float = 0.0
        self._sync_debounce_cancel: CALLBACK_TYPE | None = None
        self._reconnect_later_cancel: CALLBACK_TYPE | None = None
        self._last_sent_payloads: dict[str, str | bytes] = {}

        self._listeners: list[Callable[[], None]] = []
        self._unsub_bluetooth: Callable[[], None] | None = None
        self._unsub_state_listeners: Callable[[], None] | None = None
        self._unsub_watchdog: Callable[[], None] | None = None
        self._unsub_stop: Callable[[], None] | None = None

    def get_light_name(self, idx: int) -> str:
        """Return the configured name (up to 8 chars) for light slot idx."""
        raw = self.entry.options.get(f"light_{idx}_name") or LIGHT_NAMES[idx]
        return str(raw).strip()[:8] or LIGHT_NAMES[idx]

    def get_switch_name(self, idx: int) -> str:
        """Return the configured name (up to 8 chars) for switch slot idx."""
        raw = self.entry.options.get(f"switch_{idx}_name") or SWITCH_NAMES[idx]
        return str(raw).strip()[:8] or SWITCH_NAMES[idx]

    def get_temp_name(self, idx: int) -> str:
        """Return the configured name (up to 8 chars) for temp slot idx."""
        raw = self.entry.options.get(f"temp_{idx}_name") or TEMP_DEFAULT_NAMES[idx]
        return str(raw).strip()[:8] or TEMP_DEFAULT_NAMES[idx]

    @callback
    def async_add_listener(self, update_callback: Callable[[], None]) -> Callable[[], None]:
        """Listen for state updates."""
        self._listeners.append(update_callback)

        @callback
        def _remove() -> None:
            if update_callback in self._listeners:
                self._listeners.remove(update_callback)

        return _remove

    @callback
    def _notify_listeners(self) -> None:
        for listener in list(self._listeners):
            listener()

    async def async_start(self) -> None:
        """Start listening for the PineTime over Bluetooth."""
        self._Stopping = False
        self._setup_entity_tracking()

        self._unsub_bluetooth = bluetooth.async_register_callback(
            self.hass,
            self._async_on_bluetooth_adv,
            {"address": self.address, "connectable": True},
            bluetooth.BluetoothScanningMode.ACTIVE,
        )

        self._unsub_stop = self.hass.bus.async_listen_once(
            EVENT_HOMEASSISTANT_STOP, self._async_on_ha_stop
        )

        # Periodic 30s watchdog so we recover even if BlueZ holds a silent connection without advertisements
        self._unsub_watchdog = async_track_time_interval(
            self.hass, self._async_watchdog_tick, timedelta(seconds=30)
        )

        # Always attempt connection on startup (even if BlueZ kept the device connected across an HA restart)
        self._schedule_connect()

    @callback
    def _async_watchdog_tick(self, _now: datetime) -> None:
        """Periodically verify connection health and retry if BlueZ holds an idle link."""
        if self._Stopping:
            return
        if self._client and not self._client.is_connected:
            self._client = None
            self.connected = False
        if not self.connected:
            self._schedule_connect()

    async def async_stop(self) -> None:
        """Disconnect and clean up callbacks."""
        self._Stopping = True
        if self._sync_debounce_cancel:
            self._sync_debounce_cancel()
            self._sync_debounce_cancel = None
        if self._reconnect_later_cancel:
            self._reconnect_later_cancel()
            self._reconnect_later_cancel = None
        if self._unsub_bluetooth:
            self._unsub_bluetooth()
            self._unsub_bluetooth = None
        if self._unsub_state_listeners:
            self._unsub_state_listeners()
            self._unsub_state_listeners = None
        if self._unsub_watchdog:
            self._unsub_watchdog()
            self._unsub_watchdog = None
        if self._unsub_stop:
            self._unsub_stop()
            self._unsub_stop = None
        if self._connect_task and not self._connect_task.done():
            self._connect_task.cancel()
        if self._client and self._client.is_connected:
            try:
                await self._client.disconnect()
            except BleakError:
                pass
        self._client = None
        self.connected = False

    async def _async_on_ha_stop(self, _event: Event) -> None:
        await self.async_stop()

    async def async_reload_options(self) -> None:
        """Reload tracked Home Assistant entities when options change."""
        self._setup_entity_tracking()
        self._notify_listeners()
        self._last_sent_payloads.clear()
        await self.async_sync_all_to_watch(force=True)

    def _setup_entity_tracking(self) -> None:
        """Track state changes of linked Home Assistant entities."""
        if self._unsub_state_listeners:
            self._unsub_state_listeners()
            self._unsub_state_listeners = None

        opts = self.entry.options
        tracked_entities: list[str] = []

        for i in range(6):
            if ent := opts.get(f"light_{i}"):
                tracked_entities.append(ent)
            if ent := opts.get(f"switch_{i}"):
                tracked_entities.append(ent)
            if ent := opts.get(f"temp_{i}_sensor"):
                tracked_entities.append(ent)

        if ent := opts.get("climate_entity"):
            tracked_entities.append(ent)
        if ent := opts.get("thermostat_temp_sensor"):
            tracked_entities.append(ent)

        if tracked_entities:
            self._unsub_state_listeners = async_track_state_change_event(
                self.hass,
                list(set(tracked_entities)),
                self._async_on_linked_entity_change,
            )

    @callback
    def _async_on_linked_entity_change(self, _event: Event) -> None:
        """Schedule a debounced state push to the watch when a linked HA entity changes."""
        now_mono = time.monotonic()
        if now_mono < self._ignore_state_changes_until:
            # While a watch-initiated group transition is in flight, wait until it settles
            delay = max(0.6, self._ignore_state_changes_until - now_mono + 0.2)
        else:
            delay = 0.6
        self._schedule_debounced_sync(delay)

    @callback
    def _schedule_debounced_sync(self, delay: float) -> None:
        """Coalesce rapid state_changed events (e.g. from light groups) into a single watch sync."""
        if self._sync_debounce_cancel is not None:
            self._sync_debounce_cancel()
            self._sync_debounce_cancel = None

        @callback
        def _fire(_now: datetime) -> None:
            self._sync_debounce_cancel = None
            self._pull_states_from_linked_entities()
            self._notify_listeners()
            if self._client and self._client.is_connected:
                self.hass.async_create_task(self.async_sync_all_to_watch(force=False))

        self._sync_debounce_cancel = async_call_later(self.hass, delay, _fire)

    @callback
    def _async_on_bluetooth_adv(
        self,
        service_info: bluetooth.BluetoothServiceInfoBleak,
        change: bluetooth.BluetoothChange,
    ) -> None:
        """Connect to the PineTime when a BLE advertisement is seen (never replay advertisement data)."""
        if not self.connected and not self._Stopping:
            self._schedule_connect()

    @callback
    def _schedule_connect(self) -> None:
        if self._connect_task is None or self._connect_task.done():
            self._connect_task = self.hass.async_create_task(self._async_connect_loop())

    async def _async_connect_loop(self) -> None:
        """Establish BLE GATT connection to the watch when in range."""
        if self._Stopping or self._connect_lock.locked():
            return

        async with self._connect_lock:
            if self._client and self._client.is_connected:
                return

            ble_device = bluetooth.async_ble_device_from_address(
                self.hass, self.address, connectable=True
            ) or bluetooth.async_ble_device_from_address(
                self.hass, self.address, connectable=False
            )

            client: BleakClient | None = None
            try:
                if ble_device is not None:
                    _LOGGER.info("Connecting to PineTime at %s via HA Bluetooth", self.address)
                    client = await establish_connection(
                        BleakClient,
                        ble_device,
                        self.address,
                        disconnected_callback=self._on_disconnected,
                        max_attempts=2,
                    )
                else:
                    # Fallback if BlueZ already holds the connection across an HA Core restart
                    _LOGGER.info("Connecting to PineTime at %s via direct BlueZ address", self.address)
                    client = BleakClient(
                        self.address,
                        disconnected_callback=self._on_disconnected,
                        timeout=10.0,
                    )
                    await client.connect()

                self._client = client
                self.connected = True
                self._processed_seqs.clear()
                self._last_sent_payloads.clear()
                _LOGGER.info("Connected to PineTime at %s", self.address)

                # 1. Sync current local time to the watch's Current Time Service
                await self._async_sync_time(client)

                # 2. Read battery level if available
                try:
                    bat_bytes = await client.read_gatt_char(BATTERY_LEVEL_UUID)
                    if bat_bytes:
                        self.battery_level = int(bat_bytes[0])
                except Exception:  # noqa: BLE001
                    pass

                # 3. Push current Home Assistant states, names & temperatures to the watch first
                await self.async_sync_all_to_watch(force=True)
                self._notify_listeners()

                # 4. Subscribe to real-time commands from the watch
                await client.start_notify(HA_CMD_CHAR_UUID, self._on_gatt_notify)

            except Exception as err:  # noqa: BLE001
                _LOGGER.warning("PineTime %s connect attempt failed: %s", self.address, err)
                if client is not None:
                    try:
                        await client.disconnect()
                    except Exception:  # noqa: BLE001
                        pass
                self._client = None
                self.connected = False

    def _on_disconnected(self, _client: BleakClient) -> None:
        """Handle watch going out of range, disconnecting, or triggering BT Restart."""
        _LOGGER.info("PineTime %s disconnected", self.address)
        self._client = None
        self.connected = False
        self._processed_seqs.clear()
        self._last_sent_payloads.clear()
        self._notify_listeners()

        # Schedule a quick reconnect attempt (2s) so watch-initiated BT Restart reconnects immediately
        if not self._Stopping:
            if self._reconnect_later_cancel is not None:
                self._reconnect_later_cancel()

            @callback
            def _retry_connect(_now: datetime) -> None:
                self._reconnect_later_cancel = None
                if not self.connected and not self._Stopping:
                    self._schedule_connect()

            self._reconnect_later_cancel = async_call_later(self.hass, 2.0, _retry_connect)

    async def _async_sync_time(self, client: BleakClient) -> None:
        """Write Raspberry Pi local time to the PineTime BLE Current Time Service."""
        try:
            now: datetime = dt_util.now()
            cts_payload = struct.pack(
                "<HBBBBBBBB",
                now.year,
                now.month,
                now.day,
                now.hour,
                now.minute,
                now.second,
                now.isoweekday(),
                int(now.microsecond * 256 / 1_000_000) & 0xFF,
                0x01,
            )
            await client.write_gatt_char(CTS_CURRENT_TIME_UUID, cts_payload, response=True)
            _LOGGER.debug("Synced time %s to PineTime", now.isoformat())
        except Exception as err:  # noqa: BLE001
            _LOGGER.debug("Could not sync time to PineTime: %s", err)

    def _on_gatt_notify(self, _sender: Any, data: bytearray) -> None:
        """Callback when the watch sends a notification on HA_CMD_CHAR_UUID."""
        if len(data) < 5:
            return
        if len(data) >= 6:
            seq = int(data[4]) | (int(data[5]) << 8)
        else:
            seq = int(data[4])

        # Guarantee each packet ID is processed at most once
        if seq != 0:
            if seq in self._processed_seqs:
                _LOGGER.debug("Ignoring duplicate PineTime packet seq=%d", seq)
                return
            self._processed_seqs.append(seq)

        self.hass.async_create_task(self._async_handle_command_bytes(bytes(data[:5])))

    async def _async_turn_on_single_or_group_members(
        self,
        target_entities: list[str],
        brightness: int | None,
        color_temp: int | None,
        st_Hint: State | None,
    ) -> None:
        """Send light.turn_on to one or more entities using their supported color temperature range."""
        service_data: dict[str, Any] = {
            ATTR_ENTITY_ID: target_entities if len(target_entities) > 1 else target_entities[0]
        }
        supported_modes = (
            set(st_Hint.attributes.get("supported_color_modes") or [])
            if st_Hint is not None
            else set()
        )

        if brightness is not None and (not supported_modes or supported_modes != {"onoff"}):
            service_data["brightness_pct"] = brightness

        if color_temp is not None:
            if not supported_modes or "color_temp" in supported_modes:
                min_k, max_k = _light_kelvin_bounds(st_Hint)
                service_data["color_temp_kelvin"] = _pct_to_kelvin_mired_interp(
                    color_temp, min_k, max_k
                )
            elif supported_modes & {"hs", "xy", "rgb", "rgbw", "rgbww"}:
                # For RGB/XY lights without color_temp mode, map 0% (warm red-amber) -> 100% (cool white)
                if color_temp <= 50:
                    hue = 22.0 + (color_temp / 50.0) * 14.0
                    sat = 85.0 - (color_temp / 50.0) * 70.0
                else:
                    hue = 210.0
                    sat = ((color_temp - 50.0) / 50.0) * 35.0
                service_data["hs_color"] = (round(hue, 1), round(sat, 1))

        try:
            await self.hass.services.async_call("light", "turn_on", service_data)
        except Exception:  # noqa: BLE001
            await self.hass.services.async_call(
                "light",
                "turn_on",
                {
                    ATTR_ENTITY_ID: (
                        target_entities if len(target_entities) > 1 else target_entities[0]
                    )
                },
            )

    async def _async_control_linked_light(
        self,
        idx: int,
        brightness: int | None,
        color_temp: int | None,
    ) -> None:
        """Control a linked Home Assistant light or light group safely based on member capabilities."""
        linked_light = self.entry.options.get(f"light_{idx}")
        if not linked_light:
            return

        # Suppress intermediate state_changed echoes while Zigbee bulbs/groups transition
        self._ignore_state_changes_until = time.monotonic() + 1.6
        self._schedule_debounced_sync(1.7)

        if brightness is not None and brightness == 0:
            await self.hass.services.async_call(
                "light", "turn_off", {ATTR_ENTITY_ID: linked_light}
            )
            return

        st = self.hass.states.get(linked_light)

        # If linked_light is a Light Group with member entities and we are setting color_temp,
        # group the member bulbs by their (min_k, max_k) bounds so bulbs like IKEA NYMÅNE (2200K-4000K)
        # reach their full deep warm red-ish minimum instead of being scaled to a 6500K group maximum.
        member_ids = st.attributes.get("entity_id") if st is not None else None
        if (
            color_temp is not None
            and isinstance(member_ids, (list, tuple))
            and len(member_ids) > 1
        ):
            buckets: dict[tuple[int, int], tuple[list[str], State | None]] = {}
            for m_id in member_ids:
                if not isinstance(m_id, str) or not m_id.startswith("light."):
                    continue
                # Never recurse into PineTime virtual lights if any were accidentally added to a group
                if "pinetime" in m_id:
                    continue
                m_st = self.hass.states.get(m_id)
                bounds = _light_kelvin_bounds(m_st)
                if bounds not in buckets:
                    buckets[bounds] = ([], m_st)
                buckets[bounds][0].append(m_id)

            if len(buckets) > 1:
                for entities, rep_state in buckets.values():
                    await self._async_turn_on_single_or_group_members(
                        entities, brightness, color_temp, rep_state
                    )
                return

        await self._async_turn_on_single_or_group_members(
            [linked_light], brightness, color_temp, st
        )

    async def _async_control_linked_switch(self, idx: int, state: bool) -> None:
        """Control a linked Home Assistant switch/input_boolean entity."""
        if linked_sw := self.entry.options.get(f"switch_{idx}"):
            self._ignore_state_changes_until = time.monotonic() + 1.0
            self._schedule_debounced_sync(1.1)
            service = "turn_on" if state else "turn_off"
            await self.hass.services.async_call(
                "homeassistant", service, {ATTR_ENTITY_ID: linked_sw}
            )

    async def _async_control_linked_climate(self, target_c: int) -> None:
        """Control a linked Home Assistant climate entity."""
        if linked_climate := self.entry.options.get("climate_entity"):
            self._ignore_state_changes_until = time.monotonic() + 1.0
            self._schedule_debounced_sync(1.1)
            await self.hass.services.async_call(
                "climate",
                "set_temperature",
                {
                    ATTR_ENTITY_ID: linked_climate,
                    "temperature": target_c,
                },
            )

    async def _async_drain_pending_light_cmds(self) -> None:
        """Process queued light commands in serialized order, coalescing rapid slider updates."""
        async with self._cmd_lock:
            while self._pending_light_cmds:
                idx = next(iter(self._pending_light_cmds))
                brightness, color_temp = self._pending_light_cmds.pop(idx)
                await self._async_control_linked_light(idx, brightness, color_temp)

    async def _async_handle_command_bytes(self, cmd: bytes) -> None:
        """Process a command packet from the PineTime watch."""
        cmd_type = cmd[0]
        arg1 = cmd[1]
        arg2 = cmd[2]
        arg3 = cmd[3]

        if cmd_type == CMD_LIGHT and arg1 < 6:
            idx = arg1
            brightness = min(100, arg2)
            color_temp = min(100, arg3)
            self.light_brightness[idx] = brightness
            self.light_color_temp[idx] = color_temp
            self._notify_listeners()

            self.hass.bus.async_fire(
                EVENT_PINETIME_COMMAND,
                {
                    "type": "light",
                    "index": idx,
                    "name": self.get_light_name(idx),
                    "brightness": brightness,
                    "color_temp": color_temp,
                },
            )
            self._pending_light_cmds[idx] = (brightness, color_temp)
            if self._light_worker_task is None or self._light_worker_task.done():
                self._light_worker_task = self.hass.async_create_task(
                    self._async_drain_pending_light_cmds()
                )

        elif cmd_type == CMD_SWITCH and arg1 < 6:
            idx = arg1
            state = arg2 != 0
            self.switch_states[idx] = state
            self._notify_listeners()

            self.hass.bus.async_fire(
                EVENT_PINETIME_COMMAND,
                {
                    "type": "switch",
                    "index": idx,
                    "name": self.get_switch_name(idx),
                    "state": state,
                },
            )
            async with self._cmd_lock:
                await self._async_control_linked_switch(idx, state)

        elif cmd_type == CMD_THERMOSTAT:
            target_c = max(2, min(21, arg1))
            self.thermostat_target = target_c
            self._notify_listeners()

            self.hass.bus.async_fire(
                EVENT_PINETIME_COMMAND,
                {
                    "type": "thermostat",
                    "target_c": target_c,
                },
            )
            async with self._cmd_lock:
                await self._async_control_linked_climate(target_c)

        elif cmd_type == CMD_REFRESH:
            if time.monotonic() >= self._ignore_state_changes_until:
                await self.async_sync_all_to_watch(force=False, screen_id=arg1)

    def _pull_states_from_linked_entities(self) -> None:
        """Read states from any linked Home Assistant entities into our state cache."""
        opts = self.entry.options

        for i in range(6):
            if linked_light := opts.get(f"light_{i}"):
                st = self.hass.states.get(linked_light)
                if st and st.state not in (STATE_UNAVAILABLE, STATE_UNKNOWN):
                    if st.state == STATE_ON:
                        br_255 = st.attributes.get("brightness")
                        if br_255 is not None:
                            self.light_brightness[i] = max(1, round(int(br_255) * 100 / 255))
                        elif self.light_brightness[i] == 0:
                            self.light_brightness[i] = 100

                        min_k, max_k = _light_kelvin_bounds(st)
                        if kelvin := st.attributes.get("color_temp_kelvin"):
                            self.light_color_temp[i] = _kelvin_to_pct_mired_interp(
                                int(kelvin), min_k, max_k
                            )
                        elif mireds := st.attributes.get("color_temp"):
                            kelvin_from_mireds = round(1_000_000 / max(1, int(mireds)))
                            self.light_color_temp[i] = _kelvin_to_pct_mired_interp(
                                kelvin_from_mireds, min_k, max_k
                            )
                    else:
                        self.light_brightness[i] = 0

            if linked_sw := opts.get(f"switch_{i}"):
                st = self.hass.states.get(linked_sw)
                if st and st.state not in (STATE_UNAVAILABLE, STATE_UNKNOWN):
                    self.switch_states[i] = st.state == STATE_ON

        if linked_climate := opts.get("climate_entity"):
            st = self.hass.states.get(linked_climate)
            if st and st.state not in (STATE_UNAVAILABLE, STATE_UNKNOWN):
                if (t := st.attributes.get("temperature")) is not None:
                    try:
                        self.thermostat_target = max(2, min(21, round(float(t))))
                    except (ValueError, TypeError):
                        pass
                if (cur := st.attributes.get("current_temperature")) is not None:
                    try:
                        self.thermostat_current = float(cur)
                    except (ValueError, TypeError):
                        pass

        if temp_sensor := opts.get("thermostat_temp_sensor"):
            st = self.hass.states.get(temp_sensor)
            if st and st.state not in (STATE_UNAVAILABLE, STATE_UNKNOWN):
                try:
                    self.thermostat_current = float(st.state)
                except (ValueError, TypeError):
                    pass

    async def async_write_watch_state(
        self, payload: str | bytes, slot_key: str | None = None, force: bool = True
    ) -> None:
        """Write an ASCII or binary state update to HA_STATE_CHAR_UUID on the watch."""
        if not self._client or not self._client.is_connected:
            return
        if not force and slot_key is not None:
            if self._last_sent_payloads.get(slot_key) == payload:
                return
        data = payload.encode("utf-8") if isinstance(payload, str) else payload
        async with self._write_lock:
            if not self._client or not self._client.is_connected:
                return
            try:
                await self._client.write_gatt_char(HA_STATE_CHAR_UUID, data, response=True)
                if slot_key is not None:
                    self._last_sent_payloads[slot_key] = payload
            except Exception as err:  # noqa: BLE001
                _LOGGER.warning("Failed writing '%s' to PineTime: %s", payload, err)

    async def async_set_light_from_ha(
        self,
        idx: int,
        brightness: int | None,
        color_temp: int | None,
        turn_off: bool = False,
    ) -> None:
        """Update a light slot from Home Assistant (phone/UI), push to watch and linked light."""
        if turn_off:
            self.light_brightness[idx] = 0
            await self._async_control_linked_light(idx, 0, None)
        else:
            if brightness is not None:
                self.light_brightness[idx] = max(1, min(100, brightness))
            elif self.light_brightness[idx] == 0:
                self.light_brightness[idx] = 80
            if color_temp is not None:
                self.light_color_temp[idx] = max(0, min(100, color_temp))
            # Only send explicit brightness/color_temp kwargs that the user actually changed on the phone
            await self._async_control_linked_light(idx, brightness, color_temp)

        self._notify_listeners()
        name = self.get_light_name(idx)
        await self.async_write_watch_state(
            f"L:{idx}:{self.light_brightness[idx]}:{self.light_color_temp[idx]}:{name}",
            slot_key=f"L:{idx}",
            force=True,
        )

    async def async_set_switch_from_ha(self, idx: int, state: bool) -> None:
        """Update a switch slot from Home Assistant (phone/UI), push to watch and linked switch."""
        self.switch_states[idx] = state
        self._notify_listeners()
        name = self.get_switch_name(idx)
        await self.async_write_watch_state(
            f"S:{idx}:{1 if state else 0}:{name}",
            slot_key=f"S:{idx}",
            force=True,
        )
        await self._async_control_linked_switch(idx, state)

    async def async_set_thermostat_from_ha(self, target_c: int) -> None:
        """Update thermostat target from Home Assistant (phone/UI), push to watch and linked climate."""
        self.thermostat_target = max(2, min(21, int(target_c)))
        self._notify_listeners()
        if self.thermostat_current is not None:
            await self.async_write_watch_state(
                f"T:{self.thermostat_target}:{self.thermostat_current:.1f}",
                slot_key="T",
                force=True,
            )
        else:
            await self.async_write_watch_state(
                bytes([0x04, self.thermostat_target]),
                slot_key="T",
                force=True,
            )
        await self._async_control_linked_climate(self.thermostat_target)

    async def async_sync_all_to_watch(
        self, force: bool = True, screen_id: int | None = None
    ) -> None:
        """Push Lights, Switches, Thermostat, and/or 6 Room Temperatures to the watch."""
        if not self._client or not self._client.is_connected:
            return

        self._pull_states_from_linked_entities()
        opts = self.entry.options

        # 1. Sync 6 Lights (brightness, color temp, and custom 8-char name)
        if screen_id in (None, 0, 1):
            for i in range(6):
                name = self.get_light_name(i)
                await self.async_write_watch_state(
                    f"L:{i}:{self.light_brightness[i]}:{self.light_color_temp[i]}:{name}",
                    slot_key=f"L:{i}",
                    force=force,
                )

        # 2. Sync 6 Switches (state and custom 8-char name)
        if screen_id in (None, 0, 2):
            for i in range(6):
                name = self.get_switch_name(i)
                await self.async_write_watch_state(
                    f"S:{i}:{1 if self.switch_states[i] else 0}:{name}",
                    slot_key=f"S:{i}",
                    force=force,
                )

        # 3. Sync Thermostat
        if screen_id in (None, 0, 3):
            if self.thermostat_current is not None:
                await self.async_write_watch_state(
                    f"T:{self.thermostat_target}:{self.thermostat_current:.1f}",
                    slot_key="T",
                    force=force,
                )
            else:
                await self.async_write_watch_state(
                    bytes([0x04, self.thermostat_target]),
                    slot_key="T",
                    force=force,
                )

        # 4. Sync 6 Room Temperature sensors (always syncs custom name even if no sensor is set)
        if screen_id in (None, 0, 4):
            for i in range(6):
                label = self.get_temp_name(i)
                sensor_ent = opts.get(f"temp_{i}_sensor")
                sent_val = False
                if sensor_ent:
                    st = self.hass.states.get(sensor_ent)
                    if st and st.state not in (STATE_UNAVAILABLE, STATE_UNKNOWN):
                        val_str = st.state
                        if st.domain == "weather":
                            val_str = str(st.attributes.get("temperature", ""))
                        try:
                            temp_c = float(val_str)
                            await self.async_write_watch_state(
                                f"{i}:{label}:{temp_c:.1f}",
                                slot_key=f"TEMP:{i}",
                                force=force,
                            )
                            sent_val = True
                        except (ValueError, TypeError):
                            pass
                if not sent_val:
                    await self.async_write_watch_state(
                        f"{i}:{label}:--",
                        slot_key=f"TEMP:{i}",
                        force=force,
                    )
