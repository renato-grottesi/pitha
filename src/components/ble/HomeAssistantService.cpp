#include "components/ble/HomeAssistantService.h"
#include "components/ble/NimbleController.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
  // Service UUID: 00000000-78fc-48fe-8e23-433b3a1942e0
  // Cmd Char:     00000001-78fc-48fe-8e23-433b3a1942e0 (READ | NOTIFY)
  // State Char:   00000002-78fc-48fe-8e23-433b3a1942e0 (READ | WRITE | WRITE_NO_RSP)
  constexpr ble_uuid128_t HaUuid(uint8_t charId) {
    return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                          .value = {0xe0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, charId, 0x00, 0x00, 0x00}};
  }

  constexpr ble_uuid128_t haServiceUuid {HaUuid(0x00)};
  constexpr ble_uuid128_t haCmdCharUuid {HaUuid(0x01)};
  constexpr ble_uuid128_t haStateCharUuid {HaUuid(0x02)};

  int HaGattCallback(uint16_t /*conn_handle*/, uint16_t /*attr_handle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    return static_cast<Pinetime::Controllers::HomeAssistantService*>(arg)->OnAccess(ctxt);
  }

  // Parses an integer or decimal with 1 fractional digit (e.g. "21.4" -> 214, "-3.5" -> -35, "20" -> 200)
  int16_t ParseFixedTempX10(const char* str) {
    if (str == nullptr || *str == '\0') {
      return 0;
    }
    bool neg = false;
    if (*str == '-') {
      neg = true;
      ++str;
    } else if (*str == '+') {
      ++str;
    }
    int32_t whole = 0;
    while (*str >= '0' && *str <= '9') {
      whole = whole * 10 + (*str - '0');
      ++str;
    }
    int32_t frac = 0;
    if (*str == '.' || *str == ',') {
      ++str;
      if (*str >= '0' && *str <= '9') {
        frac = *str - '0';
      }
    }
    int32_t val = whole * 10 + frac;
    return static_cast<int16_t>(neg ? -val : val);
  }
}

using namespace Pinetime::Controllers;

HomeAssistantService::HomeAssistantService(NimbleController& nimble) : nimble {nimble} {
  characteristicDefinition[0] = {.uuid = &haCmdCharUuid.u,
                                 .access_cb = HaGattCallback,
                                 .arg = this,
                                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                 .val_handle = &cmdCharHandle};
  characteristicDefinition[1] = {.uuid = &haStateCharUuid.u,
                                 .access_cb = HaGattCallback,
                                 .arg = this,
                                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                                 .val_handle = &stateCharHandle};
  characteristicDefinition[2] = {0};

  serviceDefinition[0] = {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &haServiceUuid.u, .characteristics = characteristicDefinition};
  serviceDefinition[1] = {0};
}

void HomeAssistantService::Init() {
  uint8_t res = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(res == 0);
  res = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(res == 0);
}

int HomeAssistantService::OnAccess(struct ble_gatt_access_ctxt* ctxt) {
  if (ble_uuid_cmp(ctxt->chr->uuid, &haCmdCharUuid.u) == 0) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      int res = os_mbuf_append(ctxt->om, lastCommand.data(), lastCommand.size());
      return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
  }

  if (ble_uuid_cmp(ctxt->chr->uuid, &haStateCharUuid.u) == 0) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      uint8_t buf[64];
      const uint16_t len = std::min<uint16_t>(OS_MBUF_PKTLEN(ctxt->om), sizeof(buf) - 1);
      os_mbuf_copydata(ctxt->om, 0, len, buf);
      buf[len] = '\0';
      ParseWritePayload(buf, len);
      return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      uint8_t stateBuf[16];
      uint8_t swMask = 0;
      for (uint8_t i = 0; i < NumSwitches; ++i) {
        if (switches[i].state) {
          swMask |= static_cast<uint8_t>(1U << i);
        }
      }
      stateBuf[0] = swMask;
      stateBuf[1] = thermostatTargetC;
      stateBuf[2] = static_cast<uint8_t>(thermostatCurrentX10 & 0xFF);
      stateBuf[3] = static_cast<uint8_t>((thermostatCurrentX10 >> 8) & 0xFF);
      for (uint8_t i = 0; i < NumLights; ++i) {
        stateBuf[4 + i] = lights[i].brightness;
        stateBuf[10 + i] = lights[i].colorTemp;
      }
      int res = os_mbuf_append(ctxt->om, stateBuf, sizeof(stateBuf));
      return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
  }
  return 0;
}

void HomeAssistantService::ParseWritePayload(const uint8_t* data, uint16_t len) {
  if (len < 2) {
    return;
  }

  // 1. Support human-readable ASCII strings from Home Assistant:
  //    - Temp slot:  "0:Kitchen:21.4" or "0:Kitchen:--" (up to 8-char name)
  //    - Thermostat: "T:19:20.5" (target=19C, current=20.5C)
  //    - Switch:     "S:0:1" or "S:0:1:Heater" (up to 8-char name)
  //    - Light:      "L:0:80:40" or "L:0:80:40:Kitchen" (up to 8-char name)
  if (len >= 4 && data[1] == ':') {
    const char prefix = static_cast<char>(data[0]);
    const char* rest = reinterpret_cast<const char*>(data + 2);

    if (prefix >= '0' && prefix < static_cast<char>('0' + NumTemps)) {
      const uint8_t slot = static_cast<uint8_t>(prefix - '0');
      const char* colon2 = std::strchr(rest, ':');
      if (colon2 != nullptr) {
        bool changed = false;
        const size_t nameLen = std::min<size_t>(static_cast<size_t>(colon2 - rest), sizeof(tempSensors[slot].name) - 1);
        if (nameLen > 0) {
          if (std::strlen(tempSensors[slot].name) != nameLen || std::strncmp(tempSensors[slot].name, rest, nameLen) != 0) {
            std::memcpy(tempSensors[slot].name, rest, nameLen);
            tempSensors[slot].name[nameLen] = '\0';
            changed = true;
          }
        }
        const char* valStr = colon2 + 1;
        if (valStr[0] == '-' && valStr[1] == '-') {
          if (tempSensors[slot].valid) {
            tempSensors[slot].valid = false;
            changed = true;
          }
        } else if (valStr[0] != '\0') {
          const int16_t newTempX10 = ParseFixedTempX10(valStr);
          if (!tempSensors[slot].valid || tempSensors[slot].tempX10 != newTempX10) {
            tempSensors[slot].tempX10 = newTempX10;
            tempSensors[slot].valid = true;
            changed = true;
          }
        }
        if (changed) {
          ++tempsVersion;
          ++stateVersion;
        }
      }
      return;
    }

    if (prefix == 'T' || prefix == 't') {
      bool changed = false;
      const int target = std::atoi(rest);
      if (target >= 2 && target <= 21) {
        const auto newTarget = static_cast<uint8_t>(target);
        if (thermostatTargetC != newTarget) {
          thermostatTargetC = newTarget;
          changed = true;
        }
      }
      const char* colon2 = std::strchr(rest, ':');
      if (colon2 != nullptr) {
        const int16_t newCurX10 = ParseFixedTempX10(colon2 + 1);
        if (!thermostatHasCurrent || thermostatCurrentX10 != newCurX10) {
          thermostatCurrentX10 = newCurX10;
          thermostatHasCurrent = true;
          changed = true;
        }
      }
      if (changed) {
        ++thermostatVersion;
        ++stateVersion;
      }
      return;
    }

    if (prefix == 'S' || prefix == 's') {
      const int idx = std::atoi(rest);
      const char* colon2 = std::strchr(rest, ':');
      if (idx >= 0 && idx < NumSwitches && colon2 != nullptr) {
        bool changed = false;
        const bool newState = (std::atoi(colon2 + 1) != 0);
        if (switches[idx].state != newState) {
          switches[idx].state = newState;
          changed = true;
        }
        const char* colon3 = std::strchr(colon2 + 1, ':');
        if (colon3 != nullptr && *(colon3 + 1) != '\0') {
          const char* nameStart = colon3 + 1;
          const size_t nameLen = std::min<size_t>(std::strlen(nameStart), sizeof(switches[idx].name) - 1);
          if (std::strlen(switches[idx].name) != nameLen || std::strncmp(switches[idx].name, nameStart, nameLen) != 0) {
            std::memcpy(switches[idx].name, nameStart, nameLen);
            switches[idx].name[nameLen] = '\0';
            changed = true;
          }
        }
        if (changed) {
          ++switchesVersion;
          ++stateVersion;
        }
      }
      return;
    }

    if (prefix == 'L' || prefix == 'l') {
      const int idx = std::atoi(rest);
      const char* colon2 = std::strchr(rest, ':');
      if (idx >= 0 && idx < NumLights && colon2 != nullptr) {
        bool changed = false;
        const auto newBr = static_cast<uint8_t>(std::clamp(std::atoi(colon2 + 1), 0, 100));
        if (lights[idx].brightness != newBr) {
          lights[idx].brightness = newBr;
          changed = true;
        }
        if (newBr > 0) {
          lights[idx].savedBrightness = newBr;
        }
        const char* colon3 = std::strchr(colon2 + 1, ':');
        if (colon3 != nullptr) {
          const auto newCt = static_cast<uint8_t>(std::clamp(std::atoi(colon3 + 1), 0, 100));
          if (lights[idx].colorTemp != newCt) {
            lights[idx].colorTemp = newCt;
            changed = true;
          }
          const char* colon4 = std::strchr(colon3 + 1, ':');
          if (colon4 != nullptr && *(colon4 + 1) != '\0') {
            const char* nameStart = colon4 + 1;
            const size_t nameLen = std::min<size_t>(std::strlen(nameStart), sizeof(lights[idx].name) - 1);
            if (std::strlen(lights[idx].name) != nameLen || std::strncmp(lights[idx].name, nameStart, nameLen) != 0) {
              std::memcpy(lights[idx].name, nameStart, nameLen);
              lights[idx].name[nameLen] = '\0';
              changed = true;
            }
          }
        }
        if (changed) {
          ++lightsVersion;
          ++stateVersion;
        }
      }
      return;
    }
  }

  // 2. Also support compact binary packets:
  //    [0x01, slot, temp_lo, temp_hi, name...]
  //    [0x02, switch_idx, state]
  //    [0x03, light_idx, brightness, color_temp]
  //    [0x04, target_c, cur_temp_lo, cur_temp_hi]
  const uint8_t pktType = data[0];
  if (pktType == 0x01 && len >= 4) {
    bool changed = false;
    const uint8_t slot = data[1] % NumTemps;
    const auto rawTemp = static_cast<int16_t>(static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8));
    if (!tempSensors[slot].valid || tempSensors[slot].tempX10 != rawTemp) {
      tempSensors[slot].tempX10 = rawTemp;
      tempSensors[slot].valid = true;
      changed = true;
    }
    if (len > 4) {
      const char* nameData = reinterpret_cast<const char*>(data + 4);
      const size_t nameLen = std::min<size_t>(len - 4, sizeof(tempSensors[slot].name) - 1);
      if (std::strlen(tempSensors[slot].name) != nameLen || std::strncmp(tempSensors[slot].name, nameData, nameLen) != 0) {
        std::memcpy(tempSensors[slot].name, nameData, nameLen);
        tempSensors[slot].name[nameLen] = '\0';
        changed = true;
      }
    }
    if (changed) {
      ++tempsVersion;
      ++stateVersion;
    }
  } else if (pktType == 0x02 && len >= 3) {
    const uint8_t idx = data[1] % NumSwitches;
    const bool newState = (data[2] != 0);
    if (switches[idx].state != newState) {
      switches[idx].state = newState;
      ++switchesVersion;
      ++stateVersion;
    }
  } else if (pktType == 0x03 && len >= 4) {
    bool changed = false;
    const uint8_t idx = data[1] % NumLights;
    const uint8_t newBr = std::min<uint8_t>(100, data[2]);
    const uint8_t newCt = std::min<uint8_t>(100, data[3]);
    if (lights[idx].brightness != newBr) {
      lights[idx].brightness = newBr;
      changed = true;
    }
    if (newBr > 0) {
      lights[idx].savedBrightness = newBr;
    }
    if (lights[idx].colorTemp != newCt) {
      lights[idx].colorTemp = newCt;
      changed = true;
    }
    if (changed) {
      ++lightsVersion;
      ++stateVersion;
    }
  } else if (pktType == 0x04 && len >= 2) {
    bool changed = false;
    const uint8_t newTarget = std::clamp<uint8_t>(data[1], 2, 21);
    if (thermostatTargetC != newTarget) {
      thermostatTargetC = newTarget;
      changed = true;
    }
    if (len >= 4) {
      const auto newCur = static_cast<int16_t>(static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8));
      if (!thermostatHasCurrent || thermostatCurrentX10 != newCur) {
        thermostatCurrentX10 = newCur;
        thermostatHasCurrent = true;
        changed = true;
      }
    }
    if (changed) {
      ++thermostatVersion;
      ++stateVersion;
    }
  }
}

void HomeAssistantService::SetLight(uint8_t index, uint8_t brightness, uint8_t colorTemp, bool notifyBle) {
  if (index >= NumLights) {
    return;
  }
  brightness = std::min<uint8_t>(100, brightness);
  colorTemp = std::min<uint8_t>(100, colorTemp);
  if (lights[index].brightness != brightness || lights[index].colorTemp != colorTemp) {
    lights[index].brightness = brightness;
    lights[index].colorTemp = colorTemp;
    ++lightsVersion;
    ++stateVersion;
  }
  if (brightness > 0) {
    lights[index].savedBrightness = brightness;
  }

  if (notifyBle) {
    SendCommand(CommandType::Light, index, brightness, colorTemp);
  }
}

void HomeAssistantService::SetSwitch(uint8_t index, bool state, bool notifyBle) {
  if (index >= NumSwitches) {
    return;
  }
  if (switches[index].state != state) {
    switches[index].state = state;
    ++switchesVersion;
    ++stateVersion;
  }

  if (notifyBle) {
    SendCommand(CommandType::Switch, index, state ? 1 : 0, 0);
  }
}

bool HomeAssistantService::ToggleSwitch(uint8_t index) {
  if (index >= NumSwitches) {
    return false;
  }
  const bool nextState = !switches[index].state;
  SetSwitch(index, nextState, true);
  return nextState;
}

void HomeAssistantService::SetThermostatTarget(uint8_t targetC, bool notifyBle) {
  targetC = std::clamp<uint8_t>(targetC, 2, 21);
  if (thermostatTargetC != targetC) {
    thermostatTargetC = targetC;
    ++thermostatVersion;
    ++stateVersion;
  }

  if (notifyBle) {
    SendCommand(CommandType::Thermostat, targetC, 0, 0);
  }
}

void HomeAssistantService::NotifyScreenOpen(uint8_t screenId) {
  lastScreenId = screenId;
  SendCommand(CommandType::RefreshRequest, screenId, 0, 0);
}

void HomeAssistantService::RestartBluetooth() {
  nimble.RestartBluetooth();
}

bool HomeAssistantService::IsBleConnected() const {
  return nimble.connHandle() != BLE_HS_CONN_HANDLE_NONE;
}

void HomeAssistantService::OnSubscribe(uint16_t attrHandle, bool subscribed) {
  if (attrHandle != cmdCharHandle || !subscribed) {
    return;
  }
  // Request a state sync from Home Assistant without replaying stale commands
  SendCommand(CommandType::RefreshRequest, lastScreenId, 0, 0);
}

void HomeAssistantService::SendCommand(CommandType type, uint8_t arg1, uint8_t arg2, uint8_t arg3) {
  ++cmdSeq;
  if (cmdSeq == 0) {
    cmdSeq = 1;
  }
  const auto seqLo = static_cast<uint8_t>(cmdSeq & 0xFF);
  const auto seqHi = static_cast<uint8_t>((cmdSeq >> 8) & 0xFF);
  lastCommand = {static_cast<uint8_t>(type), arg1, arg2, arg3, seqLo, seqHi};

  const uint16_t conn = nimble.connHandle();
  if (conn != BLE_HS_CONN_HANDLE_NONE && cmdCharHandle != 0) {
    auto* om = ble_hs_mbuf_from_flat(lastCommand.data(), lastCommand.size());
    if (om != nullptr) {
      ble_gattc_notify_custom(conn, cmdCharHandle, om);
    }
  } else {
    nimble.RestartFastAdv();
  }
}
