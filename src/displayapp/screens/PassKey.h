#pragma once

#include "Screen.h"
#include "components/ble/BleController.h"
#include "components/ble/NimbleController.h"
#include <lvgl/lvgl.h>
#include <FreeRTOS.h>
#include <task.h>

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      class PassKey : public Screen {
      public:
        PassKey(const Pinetime::Controllers::Ble& bleController, Pinetime::Controllers::NimbleController& nimble);
        ~PassKey() override;

        void Refresh() override;
        void OnButtonEvent(lv_obj_t* object, lv_event_t event);

      private:
        const Pinetime::Controllers::Ble& bleController;
        Pinetime::Controllers::NimbleController& nimble;
        bool isApprovalMode = false;
        TickType_t startTick = 0;

        lv_obj_t* passkeyLabel = nullptr;
        lv_obj_t* macLabel = nullptr;
        lv_obj_t* btnYes = nullptr;
        lv_obj_t* btnNo = nullptr;
        lv_obj_t* btnAlways = nullptr;
        lv_task_t* taskRefresh = nullptr;
      };
    }
  }
}
