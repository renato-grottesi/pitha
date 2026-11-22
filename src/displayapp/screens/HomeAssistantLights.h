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
      class HomeAssistantLights : public Screen {
      public:
        HomeAssistantLights(Controllers::HomeAssistantService& haService, Controllers::MotorController& motorController);
        ~HomeAssistantLights() override;

        void Refresh() override;
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;
        bool OnTouchEvent(uint16_t x, uint16_t y) override;

        void OnEvent(lv_obj_t* obj, lv_event_t event);

      private:
        void ShowGridView();
        void ShowDetailView(uint8_t groupIdx);
        void UpdateGridLabels();
        void UpdateDetailWidgets();

        Controllers::HomeAssistantService& haService;
        Controllers::MotorController& motorController;

        bool inDetailView = false;
        bool userEditedInDetail = false;
        uint8_t selectedGroup = 0;
        uint8_t editBrightness = 80;
        uint8_t editColorTemp = 40;
        bool dirtyPendingSend = false;
        uint32_t lastSendTick = 0;
        uint32_t lastSeenVersion = 0;

        uint8_t lastDetailGroup = 0xFF;
        uint8_t lastDetailBrightness = 0xFF;
        uint8_t lastDetailColorTemp = 0xFF;
        int8_t lastDetailIsOn = -1;

        // Grid view widgets (6 buttons in 3x2 layout)
        struct GridSlotCache {
          char name[9] {};
          uint8_t brightness = 0xFF;
          uint8_t colorTemp = 0xFF;
          bool initialized = false;
        };

        lv_obj_t* gridContainer = nullptr;
        std::array<lv_obj_t*, 6> groupBtns {};
        std::array<lv_obj_t*, 6> groupLabels {};
        std::array<GridSlotCache, 6> gridCache {};

        // Detail view widgets
        lv_obj_t* detailContainer = nullptr;
        lv_obj_t* titleLabel = nullptr;
        lv_obj_t* toggleBtn = nullptr;
        lv_obj_t* toggleBtnLabel = nullptr;

        lv_obj_t* intensityLabel = nullptr;
        lv_obj_t* intensitySlider = nullptr;

        lv_obj_t* colorLabel = nullptr;
        lv_obj_t* colorSlider = nullptr;
        lv_obj_t* backBtn = nullptr;

        lv_task_t* taskRefresh = nullptr;
      };
    }
  }
}
