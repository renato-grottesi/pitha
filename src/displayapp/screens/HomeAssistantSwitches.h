#pragma once

#include <array>
#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "components/ble/HomeAssistantService.h"
#include "components/motor/MotorController.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class HomeAssistantSwitches : public Screen {
      public:
        HomeAssistantSwitches(Controllers::HomeAssistantService& haService, Controllers::MotorController& motorController);
        ~HomeAssistantSwitches() override;

        void Refresh() override;
        void OnEvent(lv_obj_t* obj, lv_event_t event);

      private:
        void UpdateButtons();

        Controllers::HomeAssistantService& haService;
        Controllers::MotorController& motorController;

        struct SwitchSlotCache {
          char name[9] {};
          bool state = false;
          bool initialized = false;
        };

        std::array<lv_obj_t*, 6> switchBtns {};
        std::array<lv_obj_t*, 6> switchLabels {};
        std::array<SwitchSlotCache, 6> switchCache {};
        uint32_t lastSeenVersion = 0;
        lv_task_t* taskRefresh = nullptr;
      };
    }
  }
}
