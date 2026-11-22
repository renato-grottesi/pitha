#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "components/motion/MotionController.h"
#include "displayapp/Controllers.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"

namespace Pinetime::Applications {
  namespace Screens {
    class BubbleLevel : public Screen {
    public:
      BubbleLevel(Controllers::MotionController& motionController, System::SystemTask& systemTask);
      ~BubbleLevel() override;

      void Refresh() override;
      bool OnTouchEvent(TouchEvents event) override;

    private:
      Controllers::MotionController& motionController;
      System::WakeLock wakeLock;

      int32_t filterX = 0;
      int32_t filterY = 0;
      int16_t calX = 0;
      int16_t calY = 0;
      bool isCalibrated = false;

      enum class TiltZone : uint8_t { Unknown, Level, Near, Tilted };

      TiltZone lastZone = TiltZone::Unknown;
      lv_coord_t lastBubbleX = -1000;
      lv_coord_t lastBubbleY = -1000;
      int32_t lastTenthsX = 10000;
      int32_t lastTenthsY = 10000;
      bool lastCalibrated = false;

      lv_obj_t* angleLabel = nullptr;
      lv_obj_t* outerRing = nullptr;
      lv_obj_t* midRing = nullptr;
      lv_obj_t* centerRing = nullptr;
      lv_obj_t* hLine = nullptr;
      lv_obj_t* vLine = nullptr;
      lv_obj_t* bubble = nullptr;

      lv_task_t* taskRefresh = nullptr;
    };
  }

  template <>
  struct AppTraits<Apps::BubbleLevel> {
    static constexpr Apps app = Apps::BubbleLevel;
    static constexpr const char* icon = Screens::Symbols::crosshairs;

    static Screens::Screen* Create(AppControllers& controllers) {
      return new Screens::BubbleLevel(controllers.motionController, *controllers.systemTask);
    };

    static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
      return true;
    };
  };
}
