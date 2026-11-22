#pragma once

#include <array>
#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "components/ble/HomeAssistantService.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class HomeAssistantTemperatures : public Screen {
      public:
        explicit HomeAssistantTemperatures(Controllers::HomeAssistantService& haService);
        ~HomeAssistantTemperatures() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        void UpdateRows();

        struct TempSlotCache {
          char name[9] {};
          int16_t tempX10 = 0;
          bool valid = false;
          bool initialized = false;
        };

        Controllers::HomeAssistantService& haService;
        std::array<lv_obj_t*, Controllers::HomeAssistantService::NumTemps> nameLabels {};
        std::array<lv_obj_t*, Controllers::HomeAssistantService::NumTemps> valLabels {};
        std::array<TempSlotCache, Controllers::HomeAssistantService::NumTemps> tempCache {};
        uint32_t lastSeenVersion = 0;
        lv_task_t* taskRefresh = nullptr;
      };
    }
  }
}
