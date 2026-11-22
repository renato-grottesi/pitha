#pragma once

#include "components/timer/Timer.h"
#include <FreeRTOS.h>
#include <timers.h>
#include <chrono>

namespace Pinetime {
  namespace Controllers {
    class PomodoroController {
    public:
      enum class Phase { Idle, Work, Relax };

      PomodoroController(void* timerData, TimerCallbackFunction_t timerCallbackFunction);

      void StartSession(std::chrono::milliseconds workDuration, std::chrono::milliseconds relaxDuration);
      void Stop();
      void OnTimerDone();

      Phase GetPhase() const {
        return phase;
      }

      bool IsRunning() const {
        return phase == Phase::Work || phase == Phase::Relax;
      }

      std::chrono::seconds GetRemainingSeconds();

      bool IsAlerting() const {
        return alerting;
      }

      TickType_t GetAlertStartTick() const {
        return alertStartTick;
      }

      void ClearAlert() {
        alerting = false;
      }

      bool IsSessionCompleted() const {
        return sessionCompleted;
      }

      void ClearSessionCompleted() {
        sessionCompleted = false;
      }

    private:
      Timer timer;
      Phase phase = Phase::Idle;
      std::chrono::milliseconds relaxDuration {0};
      bool alerting = false;
      bool sessionCompleted = false;
      TickType_t alertStartTick = 0;
    };
  }
}
