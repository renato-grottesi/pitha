#pragma once

#include "displayapp/apps/Apps.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Symbols.h"
#include "components/pomodoro/PomodoroController.h"
#include "components/motor/MotorController.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"
#include "utility/DirtyValue.h"
#include <lvgl/lvgl.h>
#include <array>
#include <chrono>

namespace Pinetime::Applications {
  namespace Screens {
    class Pomodoro : public Screen {
    public:
      struct Preset {
        uint8_t workMinutes;
        uint8_t relaxMinutes;
        const char* label;
      };

      static constexpr std::array<Preset, 4> presets {{
        {15, 3, "15 / 3"},
        {25, 5, "25 / 5"},
        {45, 15, "45 / 15"},
        {50, 10, "50 / 10"},
      }};

      Pomodoro(Controllers::PomodoroController& pomodoroController,
               Controllers::MotorController& motorController,
               System::SystemTask& systemTask);
      ~Pomodoro() override;

      void Refresh() override;
      void OnButtonEvent(lv_obj_t* obj, lv_event_t event);
      void OnPhaseAlert();

    private:
      void UpdateLayout();
      void UpdateTimeDisplay(bool force = false);
      void SilenceAlert();

      Controllers::PomodoroController& pomodoro;
      Controllers::MotorController& motorController;
      System::WakeLock wakeLock;

      lv_obj_t* lblStatus;
      lv_obj_t* lblTime;
      lv_obj_t* btnStop;
      lv_obj_t* txtStop;
      std::array<lv_obj_t*, 4> btnPresets {};

      lv_task_t* taskRefresh;
      Utility::DirtyValue<std::chrono::seconds> displayedSeconds;
      Controllers::PomodoroController::Phase displayedPhase = Controllers::PomodoroController::Phase::Idle;
    };
  }

  template <>
  struct AppTraits<Apps::Pomodoro> {
    static constexpr Apps app = Apps::Pomodoro;
    static constexpr const char* icon = Screens::Symbols::pomodoro;

    static Screens::Screen* Create(AppControllers& controllers) {
      return new Screens::Pomodoro(controllers.pomodoroController,
                                   controllers.motorController,
                                   *controllers.systemTask);
    };

    static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
      return true;
    };
  };
}
