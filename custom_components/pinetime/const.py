"""Constants for the PineTime Smartwatch Home Assistant integration."""

DOMAIN = "pinetime"

HA_SERVICE_UUID = "00000000-78fc-48fe-8e23-433b3a1942e0"
HA_CMD_CHAR_UUID = "00000001-78fc-48fe-8e23-433b3a1942e0"
HA_STATE_CHAR_UUID = "00000002-78fc-48fe-8e23-433b3a1942e0"

CTS_CURRENT_TIME_UUID = "00002a2b-0000-1000-8000-00805f9b34fb"
BATTERY_LEVEL_UUID = "00002a19-0000-1000-8000-00805f9b34fb"

CMD_LIGHT = 0x01
CMD_SWITCH = 0x02
CMD_THERMOSTAT = 0x03
CMD_REFRESH = 0x04

LIGHT_NAMES = ["Kitchen", "Living", "Outside", "Bedroom", "Office", "All"]
SWITCH_NAMES = ["Heater", "Office", "Fridge", "Alarm", "AtHome", "Extra"]
TEMP_DEFAULT_NAMES = ["Kitchen", "Living", "Bedroom", "Office", "Outside", "Extra"]

EVENT_PINETIME_COMMAND = "pinetime_command"
