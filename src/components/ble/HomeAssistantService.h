#pragma once

#include <array>
#include <cstdint>
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

namespace Pinetime {
  namespace Controllers {
    class NimbleController;

    class HomeAssistantService {
    public:
      static constexpr uint8_t NumLights = 6;
      static constexpr uint8_t NumSwitches = 6;
      static constexpr uint8_t NumTemps = 6;

      enum class CommandType : uint8_t {
        None = 0x00,
        Light = 0x01,
        Switch = 0x02,
        Thermostat = 0x03,
        RefreshRequest = 0x04,
      };

      struct LightState {
        char name[9];
        uint8_t brightness;
        uint8_t savedBrightness;
        uint8_t colorTemp; // 0 = Warmest, 100 = Coolest
      };

      struct SwitchItem {
        char name[9];
        bool state;
      };

      struct TempSensor {
        char name[9];
        int16_t tempX10; // in 0.1 C units (e.g. 215 = 21.5 C)
        bool valid;
      };

      explicit HomeAssistantService(NimbleController& nimble);

      void Init();
      int OnAccess(struct ble_gatt_access_ctxt* ctxt);
      void OnSubscribe(uint16_t attrHandle, bool subscribed);

      void SetLight(uint8_t index, uint8_t brightness, uint8_t colorTemp, bool notifyBle = true);
      const LightState& GetLight(uint8_t index) const {
        return lights[index % NumLights];
      }

      void SetSwitch(uint8_t index, bool state, bool notifyBle = true);
      bool ToggleSwitch(uint8_t index);
      const SwitchItem& GetSwitch(uint8_t index) const {
        return switches[index % NumSwitches];
      }

      void SetThermostatTarget(uint8_t targetC, bool notifyBle = true);
      uint8_t GetThermostatTarget() const {
        return thermostatTargetC;
      }
      int16_t GetThermostatCurrentX10() const {
        return thermostatCurrentX10;
      }
      bool HasThermostatCurrent() const {
        return thermostatHasCurrent;
      }

      const TempSensor& GetTempSensor(uint8_t index) const {
        return tempSensors[index % NumTemps];
      }

      void NotifyScreenOpen(uint8_t screenId);
      void RestartBluetooth();
      bool IsBleConnected() const;

      uint32_t GetStateVersion() const {
        return stateVersion;
      }
      uint32_t GetLightsVersion() const {
        return lightsVersion;
      }
      uint32_t GetSwitchesVersion() const {
        return switchesVersion;
      }
      uint32_t GetThermostatVersion() const {
        return thermostatVersion;
      }
      uint32_t GetTempsVersion() const {
        return tempsVersion;
      }

      const uint8_t* GetMfgAdvData() const {
        return mfgAdvData.data();
      }
      uint8_t GetMfgAdvDataLen() const {
        return static_cast<uint8_t>(mfgAdvData.size());
      }

    private:
      void SendCommand(CommandType type, uint8_t arg1, uint8_t arg2, uint8_t arg3);
      void ParseWritePayload(const uint8_t* data, uint16_t len);

      NimbleController& nimble;

      struct ble_gatt_chr_def characteristicDefinition[3];
      struct ble_gatt_svc_def serviceDefinition[2];
      uint16_t cmdCharHandle {};
      uint16_t stateCharHandle {};

      std::array<LightState, NumLights> lights {{
        {"Kitchen", 80, 80, 40},
        {"Living", 80, 80, 30},
        {"Outside", 100, 100, 60},
        {"Bedroom", 50, 50, 20},
        {"Office", 90, 90, 65},
        {"All", 80, 80, 40},
      }};

      std::array<SwitchItem, NumSwitches> switches {{
        {"Heater", false},
        {"Office", false},
        {"Fridge", true},
        {"Alarm", false},
        {"AtHome", true},
        {"Extra", false},
      }};

      uint8_t thermostatTargetC = 19;
      int16_t thermostatCurrentX10 = 200;
      bool thermostatHasCurrent = false;

      std::array<TempSensor, NumTemps> tempSensors {{
        {"Kitchen", 0, false},
        {"Living", 0, false},
        {"Bedroom", 0, false},
        {"Office", 0, false},
        {"Outside", 0, false},
        {"Extra", 0, false},
      }};

      uint16_t cmdSeq = 0;
      std::array<uint8_t, 6> lastCommand {0, 0, 0, 0, 0, 0};
      std::array<uint8_t, 7> mfgAdvData {0xFF, 0xFF, 0, 0, 0, 0, 0};
      uint8_t lastScreenId = 0;
      uint32_t stateVersion = 1;
      uint32_t lightsVersion = 1;
      uint32_t switchesVersion = 1;
      uint32_t thermostatVersion = 1;
      uint32_t tempsVersion = 1;
    };
  }
}
