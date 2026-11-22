# PITHA — Home Assistant Custom Integration (`pinetime`)

This component (`custom_components/pinetime/`) connects your **Home Assistant** host directly to a **PineTime smartwatch running PITHA firmware** over Bluetooth Low Energy (BLE) — with no external ESP32 proxy, phone, or intermediary server needed during normal operation.

> [!IMPORTANT]
> **Disclaimer — Personal Project:**
> This project is an independent personal project created and maintained by Renato Grottesi in his personal capacity and spare time. It is not an official product of, affiliated with, endorsed by, sponsored by, or otherwise associated with his employer or any other organization. All views, code, documentation, and opinions expressed in this repository are solely those of the author and do not represent the views or positions of his employer.

---

## Features

- **6 Light Dimmers**: On/Off toggle, `0–100%` Brightness, Warm–Cool Color Temperature, and custom 8-character names.
- **6 Switches**: On/Off toggles with custom 8-character names (`switch.*` or `input_boolean.*`).
- **Circular Thermostat**: `2–21 °C` target setpoint in `1 °C` steps plus live current temperature display.
- **6-Row Temperature Monitor**: Custom 8-character room names + live sensor readings in °C.
- **Automatic Clock Sync**: Syncs Home Assistant's local time to the watch via the BLE Current Time Service (`0x2A2B`) upon connection.
- **Home Assistant Event Bus**: Every watch action also emits a `pinetime_command` event in Home Assistant.

---

## 1. Installing in Home Assistant

### Option A: 1-Click Install via HACS (Recommended)

Requires Home Assistant with [HACS](https://hacs.xyz/) and the **Bluetooth** integration enabled:

1. Add `renato-grottesi/pitha` directly in HACS by clicking:

   [![Open your Home Assistant instance and open a repository inside the Home Assistant Community Store.](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=renato-grottesi&repository=pitha&category=integration)

2. Click **Download**, then restart Home Assistant (**Settings → System → Restart**).
3. Start setup by clicking below (or accepting the auto-discovered **InfiniTime** / **PineTime** device under **Settings → Devices & Services**):

   [![Open your Home Assistant instance and start setting up a new integration.](https://my.home-assistant.io/badges/config_flow_start.svg)](https://my.home-assistant.io/redirect/config_flow_start/?domain=pinetime)

### Option B: Manual Install from GitHub Releases

1. Download **`pitha-homeassistant-<version>.zip`** from [GitHub Releases](https://github.com/renato-grottesi/pitha/releases).
2. Extract it into your Home Assistant `/config/` directory so files live at `/config/custom_components/pinetime/`:
   ```bash
   unzip pitha-homeassistant-<version>.zip -d /config/
   ```
3. Restart Home Assistant (**Settings → System → Restart**).

---

## 2. Setting Up & Configuring

1. Ensure Home Assistant's built-in **Bluetooth** integration is enabled under **Settings → Devices & Services → Bluetooth**.
2. Go to **Settings → Devices & Services**:
   - Click **Configure** on the auto-discovered **InfiniTime** / **PineTime** Bluetooth device, or
   - Click **+ Add Integration → PITHA (PineTime Smartwatch)** and select or enter your watch's Bluetooth MAC address (e.g. `AA:BB:CC:DD:EE:FF`).
3. Click **Configure** on the integration card to:
   - Set custom names (up to **8 characters**) for each of the **6 Lights**, **6 Switches**, and **6 Temperature rows**.
   - Link each slot to any Home Assistant entity (`light.*`, `switch.*` / `input_boolean.*`, `climate.*`, `sensor.*` / `weather.*`).
   - Leave any unused entity picker blank.

---

## 3. Building & Flashing Watch Firmware

See the root [README.md](../README.md#installing-the-firmware-from-github-releases) for pre-built DFU installation via Gadgetbridge, InfiniLink, WatchMate, Siglo, or InfiniTimeExplorer.

If you build from source and flash via `itd` / `itctl`:

```bash
cmake --build build --target pinetime-mcuboot-app -j4
# Produces build/src/pinetime-mcuboot-app-dfu-1.16.0.zip
```

1. *(If Home Assistant is actively connected over BLE, temporarily disable the `pinetime` integration or step out of range so your PC can connect.)*
2. Connect and flash:
   ```bash
   bluetoothctl connect <WATCH_MAC>
   itd &
   itctl fw upgrade -a build/src/pinetime-mcuboot-app-dfu-1.16.0.zip
   ```
3. After reboot, confirm the firmware on the watch under **Settings → Firmware → Validate**.
4. Disconnect from your PC so Home Assistant can reconnect automatically:
   ```bash
   pkill itd
   bluetoothctl disconnect <WATCH_MAC>
   ```

---

## 4. Local Development & Packaging (SSH / SCP)

When testing local changes directly against a Home Assistant host:

```bash
tar -czf pinetime_custom_component.tar.gz custom_components/pinetime
scp pinetime_custom_component.tar.gz <USER>@<HA_HOST>:/config/
ssh <USER>@<HA_HOST> "cd /config && tar -xzf pinetime_custom_component.tar.gz && rm pinetime_custom_component.tar.gz && ha core restart"
```

---

## 5. BLE GATT Protocol Reference

- **Service UUID**: `00000000-78fc-48fe-8e23-433b3a1942e0`
- **Command Characteristic (`READ | NOTIFY`)**: `00000001-78fc-48fe-8e23-433b3a1942e0`
  - 5-byte notification `[cmd_type, arg1, arg2, arg3, seq]`:
    - `0x01` (Light): `[0x01, index (0..5), brightness (0..100), color_temp (0..100), seq]`
    - `0x02` (Switch): `[0x02, index (0..5), state (0|1), 0, seq]`
    - `0x03` (Thermostat): `[0x03, target_c (2..21), 0, 0, seq]`
    - `0x04` (Refresh Request): `[0x04, screen_id, 0, 0, seq]`
  - Fires a `pinetime_command` event on the Home Assistant event bus.
- **State Characteristic (`READ | WRITE | WRITE_NO_RSP`)**: `00000002-78fc-48fe-8e23-433b3a1942e0`
  - Accepts UTF-8 strings from Home Assistant:
    - Light update: `L:<idx>:<brightness>:<colorTemp>:<name>` (e.g. `L:0:80:40:Kitchen`)
    - Switch update: `S:<idx>:<0|1>:<name>` (e.g. `S:0:1:Heater`)
    - Thermostat update: `T:<targetC>:<currentC>` (e.g. `T:19:20.5`)
    - Temperature slot update: `<slot>:<name>:<tempC>` or `<slot>:<name>:--` (e.g. `0:Kitchen:21.4`)

---

## License

Licensed under the **GNU General Public License v3.0 or later** ([LICENSE](../LICENSE)). Forked from [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime); see [NOTICE](../NOTICE) for attribution details.
