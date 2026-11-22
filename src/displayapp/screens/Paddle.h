#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Symbols.h"
#include "components/motion/MotionController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      class Paddle : public Screen {
      public:
        Paddle(Controllers::MotionController& motionController,
               Controllers::MotorController& motorController,
               System::SystemTask& systemTask);
        ~Paddle() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        void DrawPlayfield(lv_obj_t* obj, const lv_area_t* clipArea);

      private:
        uint32_t NextRand();
        void CalibrateNeutralY();
        void RollAiAimOffset();
        void ResetServe(bool towardAi);
        void ResetMatch();
        void UpdatePlayerFromAccel();
        void UpdateAiPaddle();
        void StepBall();
        void InvalidateRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
        void UpdateScores();
        void UpdatePauseOverlay();

        Controllers::MotionController& motionController;
        Controllers::MotorController& motorController;
        System::WakeLock wakeLock;

        uint32_t rngState = 1;
        int32_t neutralY = 0;

        int32_t playerYQ4 = 0;
        int32_t aiYQ4 = 0;
        int32_t aiAimOffsetQ4 = 0;

        int32_t ballXQ4 = 0;
        int32_t ballYQ4 = 0;
        int32_t ballVxQ4 = 26;
        int32_t ballVyQ4 = 10;

        int16_t prevPlayerY = 0;
        int16_t prevAiY = 0;
        int16_t prevBallX = 0;
        int16_t prevBallY = 0;

        uint8_t playerScore = 0;
        uint8_t aiScore = 0;
        uint8_t rallyHits = 0;
        uint8_t serveDelayTicks = 0;
        bool paused = false;
        bool scoreDirty = true;

        lv_obj_t* playfieldObj = nullptr;
        lv_obj_t* playerScoreLabel = nullptr;
        lv_obj_t* aiScoreLabel = nullptr;
        lv_obj_t* pauseBox = nullptr;
        lv_obj_t* pauseLabel = nullptr;
        lv_task_t* taskRefresh = nullptr;
      };
    }

    template <>
    struct AppTraits<Apps::Paddle> {
      static constexpr Apps app = Apps::Paddle;
      static constexpr const char* icon = Screens::Symbols::paddle;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Paddle(controllers.motionController, controllers.motorController, *controllers.systemTask);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
