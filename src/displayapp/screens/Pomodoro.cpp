#include "displayapp/screens/Pomodoro.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void BtnEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<Pomodoro*>(obj->user_data);
    screen->OnButtonEvent(obj, event);
  }
}

Pomodoro::Pomodoro(Controllers::PomodoroController& pomodoroController,
                   Controllers::MotorController& motorController,
                   System::SystemTask& systemTask)
  : pomodoro {pomodoroController}, motorController {motorController}, wakeLock(systemTask) {

  lblStatus = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_align(lblStatus, LV_LABEL_ALIGN_CENTER);
  lv_label_set_text_static(lblStatus, "Pomodoro");
  lv_obj_align(lblStatus, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 6);

  // 2x2 grid for the 4 common Pomodoro session lengths
  for (size_t i = 0; i < presets.size(); i++) {
    btnPresets[i] = lv_btn_create(lv_scr_act(), nullptr);
    btnPresets[i]->user_data = this;
    lv_obj_set_event_cb(btnPresets[i], BtnEventHandler);
    lv_obj_set_size(btnPresets[i], 108, 88);
    lv_obj_set_style_local_radius(btnPresets[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 16);
    lv_obj_set_style_local_bg_color(btnPresets[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);

    lv_coord_t x = (i % 2 == 0) ? 8 : 124;
    lv_coord_t y = (i < 2) ? 42 : 142;
    lv_obj_set_pos(btnPresets[i], x, y);

    lv_obj_t* lbl = lv_label_create(btnPresets[i], nullptr);
    lv_label_set_text_static(lbl, presets[i].label);
  }

  // Countdown time label (MM:SS)
  lblTime = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(lblTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
  lv_obj_set_style_local_text_color(lblTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(lblTime, "00:00");
  lv_obj_align(lblTime, lv_scr_act(), LV_ALIGN_CENTER, 0, -10);

  // Stop button shown during an active session
  btnStop = lv_btn_create(lv_scr_act(), nullptr);
  btnStop->user_data = this;
  lv_obj_set_event_cb(btnStop, BtnEventHandler);
  lv_obj_set_size(btnStop, LV_HOR_RES, 50);
  lv_obj_align(btnStop, lv_scr_act(), LV_ALIGN_IN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_local_radius(btnStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_color(btnStop, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);

  txtStop = lv_label_create(btnStop, nullptr);
  lv_label_set_text_static(txtStop, "Stop");

  displayedPhase = pomodoro.GetPhase();
  UpdateLayout();
  UpdateTimeDisplay(true);

  if (pomodoro.IsAlerting()) {
    OnPhaseAlert();
  }

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

Pomodoro::~Pomodoro() {
  lv_task_del(taskRefresh);
  SilenceAlert();
  pomodoro.ClearSessionCompleted();
  lv_obj_clean(lv_scr_act());
}

void Pomodoro::OnPhaseAlert() {
  motorController.StartRinging();
  wakeLock.Lock();
  displayedPhase = pomodoro.GetPhase();
  UpdateLayout();
  UpdateTimeDisplay(true);
}

void Pomodoro::SilenceAlert() {
  if (pomodoro.IsAlerting() || motorController.IsRinging()) {
    motorController.StopRinging();
    pomodoro.ClearAlert();
    wakeLock.Release();
  }
}

void Pomodoro::UpdateLayout() {
  bool running = pomodoro.IsRunning();

  for (auto* btn : btnPresets) {
    lv_obj_set_hidden(btn, running);
  }
  lv_obj_set_hidden(lblTime, !running);
  lv_obj_set_hidden(btnStop, !running);

  if (pomodoro.GetPhase() == Controllers::PomodoroController::Phase::Work) {
    lv_label_set_text_static(lblStatus, "WORK");
    lv_obj_set_style_local_text_color(lblStatus, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::orange);
  } else if (pomodoro.GetPhase() == Controllers::PomodoroController::Phase::Relax) {
    lv_label_set_text_static(lblStatus, "RELAX");
    lv_obj_set_style_local_text_color(lblStatus, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
  } else {
    if (pomodoro.IsSessionCompleted()) {
      lv_label_set_text_static(lblStatus, "Relax over!");
      lv_obj_set_style_local_text_color(lblStatus, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
    } else {
      lv_label_set_text_static(lblStatus, "Pomodoro");
      lv_obj_set_style_local_text_color(lblStatus, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    }
  }
  lv_obj_align(lblStatus, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 6);
}

void Pomodoro::UpdateTimeDisplay(bool force) {
  if (!pomodoro.IsRunning()) {
    return;
  }
  displayedSeconds = pomodoro.GetRemainingSeconds();
  if (force || displayedSeconds.IsUpdated()) {
    int totalSec = static_cast<int>(displayedSeconds.Get().count());
    int mins = totalSec / 60;
    int secs = totalSec % 60;
    lv_label_set_text_fmt(lblTime, "%02d:%02d", mins, secs);
    lv_obj_align(lblTime, lv_scr_act(), LV_ALIGN_CENTER, 0, -10);
  }
}

void Pomodoro::Refresh() {
  if (pomodoro.IsAlerting()) {
    if (xTaskGetTickCount() - pomodoro.GetAlertStartTick() > pdMS_TO_TICKS(5000)) {
      SilenceAlert();
    }
  }

  if (displayedPhase != pomodoro.GetPhase()) {
    displayedPhase = pomodoro.GetPhase();
    UpdateLayout();
    UpdateTimeDisplay(true);
  } else if (pomodoro.IsRunning()) {
    UpdateTimeDisplay(false);
  }
}

void Pomodoro::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (event != LV_EVENT_CLICKED) {
    return;
  }

  SilenceAlert();

  if (obj == btnStop) {
    pomodoro.Stop();
    displayedPhase = pomodoro.GetPhase();
    UpdateLayout();
    return;
  }

  for (size_t i = 0; i < presets.size(); i++) {
    if (obj == btnPresets[i]) {
      pomodoro.StartSession(std::chrono::minutes(presets[i].workMinutes),
                            std::chrono::minutes(presets[i].relaxMinutes));
      displayedPhase = pomodoro.GetPhase();
      UpdateLayout();
      UpdateTimeDisplay(true);
      return;
    }
  }
}
