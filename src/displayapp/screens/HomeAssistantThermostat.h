#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "components/ble/HomeAssistantService.h"
#include "components/motor/MotorController.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class HomeAssistantThermostat : public Screen {
      public:
        HomeAssistantThermostat(Controllers::HomeAssistantService& haService, Controllers::MotorController& motorController);
        ~HomeAssistantThermostat() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        bool OnTouchEvent(uint16_t x, uint16_t y) override;
        void OnEvent(lv_obj_t* obj, lv_event_t event);

      private:
        void UpdateWidgets();

        Controllers::HomeAssistantService& haService;
        Controllers::MotorController& motorController;

        uint8_t targetC = 19;
        bool dirtyPendingSend = false;
        uint32_t lastChangeTick = 0;
        uint32_t lastSeenVersion = 0;

        uint8_t lastArcColorBand = 0xFF;
        uint8_t lastDrawnTargetC = 0xFF;
        bool lastHasCurrent = false;
        int16_t lastDrawnCurrentX10 = 0x7FFF;

        lv_obj_t* tempArc = nullptr;
        lv_obj_t* targetCaption = nullptr;
        lv_obj_t* targetValueLabel = nullptr;
        lv_obj_t* currentValueLabel = nullptr;

        lv_task_t* taskRefresh = nullptr;
      };
    }
  }
}
