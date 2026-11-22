#include "components/pomodoro/PomodoroController.h"

using namespace Pinetime::Controllers;

PomodoroController::PomodoroController(void* const timerData, TimerCallbackFunction_t timerCallbackFunction)
  : timer(timerData, timerCallbackFunction) {
}

void PomodoroController::StartSession(std::chrono::milliseconds workDuration, std::chrono::milliseconds relaxDuration) {
  this->relaxDuration = relaxDuration;
  phase = Phase::Work;
  alerting = false;
  sessionCompleted = false;
  timer.StartTimer(workDuration);
}

void PomodoroController::Stop() {
  timer.StopTimer();
  phase = Phase::Idle;
  alerting = false;
  sessionCompleted = false;
}

void PomodoroController::OnTimerDone() {
  if (phase == Phase::Work) {
    phase = Phase::Relax;
    timer.StartTimer(relaxDuration);
    alerting = true;
    alertStartTick = xTaskGetTickCount();
  } else if (phase == Phase::Relax) {
    timer.ResetExpiredTime();
    phase = Phase::Idle;
    sessionCompleted = true;
    alerting = true;
    alertStartTick = xTaskGetTickCount();
  }
}

std::chrono::seconds PomodoroController::GetRemainingSeconds() {
  auto state = timer.GetTimerState();
  if (state && !state->expired) {
    return std::chrono::duration_cast<std::chrono::seconds>(state->distanceToExpiry + std::chrono::milliseconds(999));
  }
  return std::chrono::seconds(0);
}
