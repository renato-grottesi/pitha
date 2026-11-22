#pragma once

#include <array>
#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "components/motion/MotionController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class Breakout : public Screen {
      public:
        Breakout(Controllers::MotionController& motionController,
                 Controllers::MotorController& motorController,
                 System::SystemTask& systemTask);
        ~Breakout() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        void DrawPlayfield(lv_obj_t* obj, const lv_area_t* clipArea);

        static constexpr uint8_t GridCols = 12;
        static constexpr uint8_t GridRows = 8;
        static constexpr uint8_t NumLevels = 8;

      private:
        enum class GameState : uint8_t {
          Ready,
          Playing,
          Paused,
          LevelClear,
          GameOver,
          Victory,
        };

        uint8_t GetBrick(uint8_t col, uint8_t row) const;
        void SetBrick(uint8_t col, uint8_t row, uint8_t val);
        uint32_t NextRand();

        void LoadLevel(uint8_t idx);
        void ResetBallOnPaddle();
        void UpdatePaddleFromAccel();
        void StepBall();
        void InvalidateRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
        void UpdateHud();
        void UpdateBanner();

        Controllers::MotionController& motionController;
        Controllers::MotorController& motorController;
        System::WakeLock wakeLock;

        // 2 bits per brick: 96 bricks packed into 24 bytes of RAM
        std::array<uint8_t, (GridCols * GridRows + 3) / 4> bricks {};

        GameState state = GameState::Ready;
        uint8_t level = 0;
        uint8_t lives = 3;
        uint8_t bricksRemaining = 0;
        uint32_t score = 0;
        uint32_t rngState = 1;

        int32_t paddleXQ4 = 0;
        int32_t ballXQ4 = 0;
        int32_t ballYQ4 = 0;
        int32_t ballVxQ4 = 16;
        int32_t ballVyQ4 = -24;

        int16_t prevPaddleX = 0;
        int16_t prevBallX = 0;
        int16_t prevBallY = 0;
        bool hudDirty = true;

        lv_obj_t* playfieldObj = nullptr;
        lv_obj_t* hudLabel = nullptr;
        lv_obj_t* statusBox = nullptr;
        lv_obj_t* statusLabel = nullptr;
        lv_task_t* taskRefresh = nullptr;
      };
    }

    template <>
    struct AppTraits<Apps::Breakout> {
      static constexpr Apps app = Apps::Breakout;
      static constexpr const char* icon = Screens::Symbols::bricks;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Breakout(controllers.motionController, controllers.motorController, *controllers.systemTask);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
