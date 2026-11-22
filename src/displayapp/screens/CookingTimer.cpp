#include "displayapp/screens/CookingTimer.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void CookingEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<CookingTimer*>(obj->user_data);
    if (screen != nullptr) {
      screen->OnButtonEvent(obj, event);
    }
  }
}

CookingTimer::CookingTimer(Controllers::CookingTimerController& cookingTimer,
                           Controllers::MotorController& motorController,
                           System::SystemTask& systemTask)
  : cookingTimer {cookingTimer}, motorController {motorController}, wakeLock(systemTask) {

  // ---- 1. 2x2 Circular Burner Grid View ----
  gridContainer = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(gridContainer, 240, 240);
  lv_obj_set_pos(gridContainer, 0, 0);
  lv_obj_set_style_local_bg_opa(gridContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(gridContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

  for (uint8_t i = 0; i < Controllers::CookingTimerController::NumPlates; ++i) {
    const uint8_t col = i % 2;
    const uint8_t row = i / 2;

    lv_obj_t* btn = lv_btn_create(gridContainer, nullptr);
    btn->user_data = this;
    lv_obj_set_event_cb(btn, CookingEventHandler);
    lv_obj_set_size(btn, 108, 108);
    lv_obj_set_pos(btn, static_cast<lv_coord_t>(8 + col * 116), static_cast<lv_coord_t>(8 + row * 116));
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);

    lv_obj_t* lbl = lv_label_create(btn, nullptr);
    lv_obj_set_style_local_text_font(lbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
    lv_label_set_text_static(lbl, "--");

    plateBtns[i] = btn;
    plateLabels[i] = lbl;
  }

  // ---- 2. Plate Detail View ----
  detailContainer = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(detailContainer, 240, 240);
  lv_obj_set_pos(detailContainer, 0, 0);
  lv_obj_set_style_local_bg_opa(detailContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(detailContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

  plateTitleLabel = lv_label_create(detailContainer, nullptr);
  lv_obj_set_style_local_text_color(plateTitleLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(plateTitleLabel, "Plate 1");
  lv_obj_align(plateTitleLabel, detailContainer, LV_ALIGN_IN_TOP_MID, 0, 8);

  detailTimeLabel = lv_label_create(detailContainer, nullptr);
  lv_obj_set_style_local_text_font(detailTimeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
  lv_label_set_text_static(detailTimeLabel, "00:00");
  lv_obj_align(detailTimeLabel, detailContainer, LV_ALIGN_IN_TOP_MID, 0, 42);

  btnMinus = lv_btn_create(detailContainer, nullptr);
  btnMinus->user_data = this;
  lv_obj_set_event_cb(btnMinus, CookingEventHandler);
  lv_obj_set_size(btnMinus, 110, 84);
  lv_obj_set_pos(btnMinus, 8, 148);
  lv_obj_set_style_local_radius(btnMinus, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 16);
  lv_obj_set_style_local_bg_color(btnMinus, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x58, 0x18, 0x18));
  lv_obj_t* lblMinus = lv_label_create(btnMinus, nullptr);
  lv_obj_set_style_local_text_font(lblMinus, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_label_set_text_static(lblMinus, "-1m");

  btnPlus = lv_btn_create(detailContainer, nullptr);
  btnPlus->user_data = this;
  lv_obj_set_event_cb(btnPlus, CookingEventHandler);
  lv_obj_set_size(btnPlus, 110, 84);
  lv_obj_set_pos(btnPlus, 122, 148);
  lv_obj_set_style_local_radius(btnPlus, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 16);
  lv_obj_set_style_local_bg_color(btnPlus, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x18, 0x58, 0x20));
  lv_obj_t* lblPlus = lv_label_create(btnPlus, nullptr);
  lv_obj_set_style_local_text_font(lblPlus, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
  lv_label_set_text_static(lblPlus, "+1m");

  ShowGridView();

  if (cookingTimer.IsAnyAlerting()) {
    OnPlateAlert();
  }

  taskRefresh = lv_task_create(RefreshTaskCallback, 50, LV_TASK_PRIO_MID, this);
}

CookingTimer::~CookingTimer() {
  lv_task_del(taskRefresh);
  SilenceAlert();
  cookingTimer.AcknowledgeAllAlerts();
  lv_obj_clean(lv_scr_act());
}

void CookingTimer::SilenceAlert() {
  if (cookingTimer.IsAnyAlerting() || motorController.IsRinging()) {
    motorController.StopRinging();
    cookingTimer.SilenceVibration();
    wakeLock.Release();
  }
}

void CookingTimer::OnPlateAlert() {
  heldBtn = nullptr;
  ShowGridView();
  motorController.StartRinging();
  wakeLock.Lock();
  UpdateGridUI();
}

void CookingTimer::ShowGridView() {
  heldBtn = nullptr;
  inDetailView = false;
  UpdateGridUI();
  lv_obj_set_hidden(detailContainer, true);
  lv_obj_set_hidden(gridContainer, false);
}

void CookingTimer::ShowDetailView(uint8_t plate) {
  heldBtn = nullptr;
  selectedPlate = plate % Controllers::CookingTimerController::NumPlates;
  inDetailView = true;
  lv_label_set_text_fmt(plateTitleLabel, "Plate %u", static_cast<unsigned>(selectedPlate + 1));
  lv_obj_align(plateTitleLabel, detailContainer, LV_ALIGN_IN_TOP_MID, 0, 8);
  UpdateDetailUI(true);
  lv_obj_set_hidden(gridContainer, true);
  lv_obj_set_hidden(detailContainer, false);
}

void CookingTimer::UpdateGridUI(bool force) {
  for (uint8_t i = 0; i < Controllers::CookingTimerController::NumPlates; ++i) {
    PlateVisualMode mode = PlateVisualMode::Idle;
    uint32_t value = 0;

    if (cookingTimer.IsRunning(i)) {
      const uint32_t remSec = cookingTimer.GetRemainingSeconds(i);
      if (remSec >= 60) {
        mode = PlateVisualMode::Minutes;
        value = (remSec + 30) / 60;
      } else {
        mode = PlateVisualMode::Seconds;
        value = remSec;
      }
    } else if (cookingTimer.IsPlateAlerting(i)) {
      mode = PlateVisualMode::Alerting;
      value = 0;
    }

    if (!force && plateCache[i].mode == mode && plateCache[i].value == value) {
      continue;
    }

    if (force || plateCache[i].mode != mode) {
      switch (mode) {
        case PlateVisualMode::Minutes:
          lv_obj_set_style_local_text_color(plateLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x20, 0xE8, 0x40));
          lv_obj_set_style_local_bg_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x14, 0x26, 0x14));
          lv_obj_set_style_local_border_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x00, 0xC8, 0x40));
          lv_obj_set_style_local_border_width(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 3);
          break;
        case PlateVisualMode::Seconds:
          lv_obj_set_style_local_text_color(plateLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
          lv_obj_set_style_local_bg_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x2E, 0x12, 0x12));
          lv_obj_set_style_local_border_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
          lv_obj_set_style_local_border_width(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 3);
          break;
        case PlateVisualMode::Alerting:
          lv_obj_set_style_local_text_color(plateLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
          lv_obj_set_style_local_bg_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x58, 0x00, 0x00));
          lv_obj_set_style_local_border_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
          lv_obj_set_style_local_border_width(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 4);
          break;
        case PlateVisualMode::Idle:
        case PlateVisualMode::Unknown:
        default:
          lv_obj_set_style_local_text_color(plateLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
          lv_obj_set_style_local_bg_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
          lv_obj_set_style_local_border_color(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
          lv_obj_set_style_local_border_width(plateBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
          break;
      }
    }

    if (force || plateCache[i].mode != mode || plateCache[i].value != value) {
      if (mode == PlateVisualMode::Minutes || mode == PlateVisualMode::Seconds) {
        lv_label_set_text_fmt(plateLabels[i], "%lu", static_cast<unsigned long>(value));
      } else if (mode == PlateVisualMode::Alerting) {
        lv_label_set_text_static(plateLabels[i], "0");
      } else {
        lv_label_set_text_static(plateLabels[i], "--");
      }
    }

    plateCache[i] = {mode, value};
  }
}

void CookingTimer::UpdateDetailUI(bool force) {
  const uint32_t remSec = cookingTimer.GetRemainingSeconds(selectedPlate);
  PlateVisualMode mode = PlateVisualMode::Idle;
  if (remSec >= 60) {
    mode = PlateVisualMode::Minutes;
  } else if (remSec > 0 || cookingTimer.IsPlateAlerting(selectedPlate)) {
    mode = PlateVisualMode::Seconds;
  }

  if (!force && remSec == lastDetailSec && mode == lastDetailMode) {
    return;
  }

  const uint32_t mins = remSec / 60;
  const uint32_t secs = remSec % 60;
  const bool useLargeFont = (mins < 100);

  if (force || lastDetailMode == PlateVisualMode::Unknown || useLargeFont != lastDetailLargeFont) {
    lv_obj_set_style_local_text_font(detailTimeLabel,
                                     LV_LABEL_PART_MAIN,
                                     LV_STATE_DEFAULT,
                                     useLargeFont ? &jetbrains_mono_76 : &jetbrains_mono_42);
  }

  if (force || mode != lastDetailMode) {
    const lv_color_t textColor =
      (mode == PlateVisualMode::Minutes) ? LV_COLOR_MAKE(0x20, 0xE8, 0x40)
                                         : ((mode == PlateVisualMode::Seconds) ? LV_COLOR_RED : Colors::lightGray);
    lv_obj_set_style_local_text_color(detailTimeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, textColor);
  }

  if (useLargeFont) {
    lv_label_set_text_fmt(detailTimeLabel, "%02lu:%02lu", static_cast<unsigned long>(mins), static_cast<unsigned long>(secs));
    lv_obj_align(detailTimeLabel, detailContainer, LV_ALIGN_IN_TOP_MID, 0, 42);
  } else {
    lv_label_set_text_fmt(detailTimeLabel, "%lu:%02lu", static_cast<unsigned long>(mins), static_cast<unsigned long>(secs));
    lv_obj_align(detailTimeLabel, detailContainer, LV_ALIGN_IN_TOP_MID, 0, 58);
  }

  lastDetailSec = remSec;
  lastDetailMode = mode;
  lastDetailLargeFont = useLargeFont;
}

void CookingTimer::Refresh() {
  const TickType_t now = xTaskGetTickCount();

  if (cookingTimer.IsAnyAlerting() && (now - cookingTimer.GetLatestAlertTick()) > pdMS_TO_TICKS(10000)) {
    SilenceAlert();
  }

  if (inDetailView && heldBtn != nullptr) {
    const uint32_t heldMs = ((now - holdStartTick) * 1000U) / configTICK_RATE_HZ;
    const uint32_t sinceRepeatMs = ((now - lastRepeatTick) * 1000U) / configTICK_RATE_HZ;
    if (heldMs >= 400 && sinceRepeatMs >= 200) {
      lastRepeatTick = now;
      int32_t stepMin = 1;
      if (heldMs >= 3500) {
        stepMin = 5;
      } else if (heldMs >= 1800) {
        stepMin = 2;
      }
      const int32_t sign = (heldBtn == btnPlus) ? 1 : -1;
      cookingTimer.AdjustSeconds(selectedPlate, sign * stepMin * 60);
      if (heldBtn == btnMinus && !cookingTimer.IsRunning(selectedPlate)) {
        heldBtn = nullptr;
      }
    }
  }

  if (inDetailView) {
    UpdateDetailUI();
  } else {
    UpdateGridUI();
  }
}

void CookingTimer::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (!inDetailView) {
    if (event == LV_EVENT_CLICKED) {
      const bool wasRinging = cookingTimer.IsAnyAlerting() || motorController.IsRinging();
      SilenceAlert();
      for (uint8_t i = 0; i < Controllers::CookingTimerController::NumPlates; ++i) {
        if (obj == plateBtns[i]) {
          const bool wasPlateAlerting = cookingTimer.IsPlateAlerting(i);
          if (wasPlateAlerting) {
            cookingTimer.ClearPlateAlert(i);
            UpdateGridUI();
            if (wasRinging) {
              return;
            }
          }
          ShowDetailView(i);
          return;
        }
      }
    }
    return;
  }

  if (obj == btnPlus || obj == btnMinus) {
    if (event == LV_EVENT_PRESSED) {
      SilenceAlert();
      const int32_t delta = (obj == btnPlus) ? 60 : -60;
      cookingTimer.AdjustSeconds(selectedPlate, delta);
      motorController.RunForDuration(10);
      heldBtn = obj;
      holdStartTick = xTaskGetTickCount();
      lastRepeatTick = holdStartTick;
      UpdateDetailUI();
    } else if (event == LV_EVENT_RELEASED || event == LV_EVENT_PRESS_LOST) {
      heldBtn = nullptr;
    }
  }
}

bool CookingTimer::OnButtonPushed() {
  if (inDetailView) {
    ShowGridView();
    return true;
  }
  if (cookingTimer.IsAnyAlerting() || cookingTimer.IsAnyPlateAlerting()) {
    SilenceAlert();
    cookingTimer.AcknowledgeAllAlerts();
    UpdateGridUI();
    return true;
  }
  return false;
}

bool CookingTimer::OnTouchEvent(TouchEvents event) {
  if (inDetailView) {
    if ((event == TouchEvents::SwipeRight || event == TouchEvents::SwipeDown) && heldBtn == nullptr) {
      ShowGridView();
      return true;
    }
    if (event == TouchEvents::SwipeUp || event == TouchEvents::SwipeLeft) {
      return true;
    }
  }
  return false;
}
