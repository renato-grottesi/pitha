#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "components/ble/HomeAssistantService.h"
#include "components/motor/MotorController.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class HomeAssistantBtRestart : public Screen {
      public:
        HomeAssistantBtRestart(Controllers::HomeAssistantService& haService, Controllers::MotorController& motorController);
        ~HomeAssistantBtRestart() override;

        void Refresh() override;
        void OnButtonEvent(lv_obj_t* obj, lv_event_t event);

      private:
        void TriggerRestart();
        void UpdateStatusUI();

        Controllers::HomeAssistantService& haService;
        Controllers::MotorController& motorController;

        uint32_t restartTick = 0;

        enum class BtStatus : uint8_t { Unknown, Restarting, Connected, Advertising };
        BtStatus lastStatus = BtStatus::Unknown;

        lv_obj_t* titleLabel = nullptr;
        lv_obj_t* statusLabel = nullptr;
        lv_obj_t* restartBtn = nullptr;
        lv_task_t* taskRefresh = nullptr;
      };
    }
  }
}
