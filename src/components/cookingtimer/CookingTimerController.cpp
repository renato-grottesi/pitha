#include "components/cookingtimer/CookingTimerController.h"
#include <algorithm>

using namespace Pinetime::Controllers;

CookingTimerController::CookingTimerController(void* const timerData, TimerCallbackFunction_t timerCallbackFunction) {
  timer = xTimerCreate("CookTmr", 1, pdFALSE, timerData, timerCallbackFunction);
}

void CookingTimerController::AdjustSeconds(size_t plate, int32_t deltaSeconds) {
  if (plate >= NumPlates) {
    return;
  }

  const TickType_t now = xTaskGetTickCount();
  plates[plate].alerting = false;
  if (!IsAnyPlateAlerting()) {
    hasActiveAlert = false;
  }

  int32_t currentRemTicks = 0;
  if (plates[plate].running) {
    currentRemTicks = std::max<int32_t>(0, static_cast<int32_t>(plates[plate].expiryTick - now));
  }

  constexpr int32_t maxTicks = static_cast<int32_t>(MaxSeconds) * static_cast<int32_t>(configTICK_RATE_HZ);
  const int32_t deltaTicks = deltaSeconds * static_cast<int32_t>(configTICK_RATE_HZ);
  const int32_t nextTicks = std::clamp<int32_t>(currentRemTicks + deltaTicks, 0, maxTicks);

  if (nextTicks <= 0) {
    plates[plate].running = false;
    plates[plate].expiryTick = 0;
  } else {
    plates[plate].running = true;
    plates[plate].expiryTick = now + static_cast<TickType_t>(nextTicks);
  }

  RescheduleTimer();
}

void CookingTimerController::ResetPlate(size_t plate) {
  if (plate >= NumPlates) {
    return;
  }
  plates[plate].running = false;
  plates[plate].alerting = false;
  plates[plate].expiryTick = 0;
  if (!IsAnyPlateAlerting()) {
    hasActiveAlert = false;
  }
  RescheduleTimer();
}

bool CookingTimerController::OnTimerDone() {
  const TickType_t now = xTaskGetTickCount();
  bool expiredAny = false;

  for (auto& plate : plates) {
    if (plate.running && static_cast<int32_t>(plate.expiryTick - now) <= 0) {
      plate.running = false;
      plate.alerting = true;
      plate.expiryTick = 0;
      expiredAny = true;
    }
  }

  if (expiredAny) {
    hasActiveAlert = true;
    latestAlertTick = now;
  }

  RescheduleTimer();
  return expiredAny;
}

uint32_t CookingTimerController::GetRemainingSeconds(size_t plate) const {
  if (plate >= NumPlates || !plates[plate].running) {
    return 0;
  }
  const int32_t remTicks = static_cast<int32_t>(plates[plate].expiryTick - xTaskGetTickCount());
  if (remTicks <= 0) {
    return 0;
  }
  return static_cast<uint32_t>((remTicks + static_cast<int32_t>(configTICK_RATE_HZ) - 1) / static_cast<int32_t>(configTICK_RATE_HZ));
}

bool CookingTimerController::IsRunning(size_t plate) const {
  return plate < NumPlates && plates[plate].running;
}

bool CookingTimerController::IsPlateAlerting(size_t plate) const {
  return plate < NumPlates && plates[plate].alerting;
}

bool CookingTimerController::IsAnyPlateAlerting() const {
  for (const auto& plate : plates) {
    if (plate.alerting) {
      return true;
    }
  }
  return false;
}

void CookingTimerController::ClearPlateAlert(size_t plate) {
  if (plate >= NumPlates) {
    return;
  }
  plates[plate].alerting = false;
  if (!IsAnyPlateAlerting()) {
    hasActiveAlert = false;
  }
}

void CookingTimerController::SilenceVibration() {
  hasActiveAlert = false;
}

void CookingTimerController::AcknowledgeAllAlerts() {
  hasActiveAlert = false;
  for (auto& plate : plates) {
    plate.alerting = false;
  }
}

void CookingTimerController::RescheduleTimer() {
  const TickType_t now = xTaskGetTickCount();
  bool found = false;
  int32_t minRemTicks = 0;

  for (const auto& plate : plates) {
    if (plate.running) {
      const int32_t remTicks = static_cast<int32_t>(plate.expiryTick - now);
      if (!found || remTicks < minRemTicks) {
        minRemTicks = remTicks;
        found = true;
      }
    }
  }

  if (found) {
    const TickType_t period = static_cast<TickType_t>(std::max<int32_t>(1, minRemTicks));
    xTimerChangePeriod(timer, period, 0);
  } else {
    xTimerStop(timer, 0);
  }
}
