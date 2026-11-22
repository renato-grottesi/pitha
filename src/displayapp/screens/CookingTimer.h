#pragma once

#include <FreeRTOS.h>
#include <task.h>
#include <lvgl/lvgl.h>
#include <array>
#include <cstdint>
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "components/cookingtimer/CookingTimerController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"

namespace Pinetime::Applications {
  namespace Screens {
    class CookingTimer : public Screen {
    public:
      CookingTimer(Controllers::CookingTimerController& cookingTimer,
                   Controllers::MotorController& motorController,
                   System::SystemTask& systemTask);
      ~CookingTimer() override;

      void Refresh() override;
      bool OnButtonPushed() override;
      bool OnTouchEvent(TouchEvents event) override;

      void OnButtonEvent(lv_obj_t* obj, lv_event_t event);
      void OnPlateAlert();

    private:
      void ShowGridView();
      void ShowDetailView(uint8_t plate);
      void UpdateGridUI(bool force = false);
      void UpdateDetailUI(bool force = false);
      void SilenceAlert();

      enum class PlateVisualMode : uint8_t { Unknown, Idle, Minutes, Seconds, Alerting };

      struct PlateVisualCache {
        PlateVisualMode mode = PlateVisualMode::Unknown;
        uint32_t value = 0xFFFFFFFFU;
      };

      std::array<PlateVisualCache, Controllers::CookingTimerController::NumPlates> plateCache {};
      PlateVisualMode lastDetailMode = PlateVisualMode::Unknown;
      uint32_t lastDetailSec = 0xFFFFFFFFU;
      bool lastDetailLargeFont = true;

      Controllers::CookingTimerController& cookingTimer;
      Controllers::MotorController& motorController;
      System::WakeLock wakeLock;

      bool inDetailView = false;
      uint8_t selectedPlate = 0;

      lv_obj_t* gridContainer = nullptr;
      std::array<lv_obj_t*, Controllers::CookingTimerController::NumPlates> plateBtns {};
      std::array<lv_obj_t*, Controllers::CookingTimerController::NumPlates> plateLabels {};

      lv_obj_t* detailContainer = nullptr;
      lv_obj_t* plateTitleLabel = nullptr;
      lv_obj_t* detailTimeLabel = nullptr;
      lv_obj_t* btnMinus = nullptr;
      lv_obj_t* btnPlus = nullptr;

      lv_obj_t* heldBtn = nullptr;
      TickType_t holdStartTick = 0;
      TickType_t lastRepeatTick = 0;

      lv_task_t* taskRefresh = nullptr;
    };
  }

  template <>
  struct AppTraits<Apps::CookingTimer> {
    static constexpr Apps app = Apps::CookingTimer;
    static constexpr const char* icon = Screens::Symbols::fire;

    static Screens::Screen* Create(AppControllers& controllers) {
      return new Screens::CookingTimer(controllers.cookingTimerController,
                                       controllers.motorController,
                                       *controllers.systemTask);
    };

    static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
      return true;
    };
  };
}
