#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    class CookingTimerController {
    public:
      static constexpr size_t NumPlates = 4;
      static constexpr uint32_t MaxSeconds = 240 * 60;

      struct PlateState {
        bool running = false;
        bool alerting = false;
        TickType_t expiryTick = 0;
      };

      CookingTimerController(void* timerData, TimerCallbackFunction_t timerCallbackFunction);

      void AdjustSeconds(size_t plate, int32_t deltaSeconds);
      void ResetPlate(size_t plate);
      bool OnTimerDone();

      uint32_t GetRemainingSeconds(size_t plate) const;
      bool IsRunning(size_t plate) const;
      bool IsPlateAlerting(size_t plate) const;
      bool IsAnyPlateAlerting() const;

      bool IsAnyAlerting() const {
        return hasActiveAlert;
      }

      TickType_t GetLatestAlertTick() const {
        return latestAlertTick;
      }

      void ClearPlateAlert(size_t plate);
      void SilenceVibration();
      void AcknowledgeAllAlerts();

    private:
      void RescheduleTimer();

      TimerHandle_t timer;
      std::array<PlateState, NumPlates> plates {};
      bool hasActiveAlert = false;
      TickType_t latestAlertTick = 0;
    };
  }
}
