#include "displayapp/screens/HomeAssistantLights.h"
#include <algorithm>
#include <cstring>
#include <FreeRTOS.h>
#include <task.h>
#include "displayapp/InfiniTimeTheme.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void LightEventCallback(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<HomeAssistantLights*>(obj->user_data);
    if (screen != nullptr) {
      screen->OnEvent(obj, event);
    }
  }

  // Smoothly interpolate color temperature from Warm Amber (0%) -> Neutral White (50%) -> Cool Blue (100%)
  lv_color_t ColorTempToLvColor(uint8_t pct) {
    pct = std::min<uint8_t>(100, pct);
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    if (pct <= 50) {
      // 0% -> (255, 155, 40), 50% -> (255, 245, 230)
      r = 255;
      g = static_cast<uint8_t>(155 + (pct * 90) / 50);
      b = static_cast<uint8_t>(40 + (pct * 190) / 50);
    } else {
      // 50% -> (255, 245, 230), 100% -> (110, 180, 255)
      const uint8_t t = pct - 50;
      r = static_cast<uint8_t>(255 - (t * 145) / 50);
      g = static_cast<uint8_t>(245 - (t * 65) / 50);
      b = static_cast<uint8_t>(230 + (t * 25) / 50);
    }
    return LV_COLOR_MAKE(r, g, b);
  }

  const char* ColorTempDescription(uint8_t pct) {
    if (pct < 33) {
      return "Warm";
    }
    if (pct < 67) {
      return "Neutral";
    }
    return "Cool";
  }
}

HomeAssistantLights::HomeAssistantLights(Controllers::HomeAssistantService& haService, Controllers::MotorController& motorController)
  : haService {haService}, motorController {motorController} {

  haService.NotifyScreenOpen(1);
  lastSeenVersion = haService.GetLightsVersion();

  // ---- 1. Build 6-button 2x3 Room Grid View ----
  gridContainer = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(gridContainer, 240, 240);
  lv_obj_set_pos(gridContainer, 0, 0);
  lv_obj_set_style_local_bg_opa(gridContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(gridContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

  for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumLights; ++i) {
    const uint8_t col = i % 2;
    const uint8_t row = i / 2;

    lv_obj_t* btn = lv_btn_create(gridContainer, nullptr);
    btn->user_data = this;
    lv_obj_set_event_cb(btn, LightEventCallback);
    lv_obj_set_size(btn, 112, 72);
    lv_obj_set_pos(btn, static_cast<lv_coord_t>(4 + col * 120), static_cast<lv_coord_t>(4 + row * 78));
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 10);
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);

    lv_obj_t* lbl = lv_label_create(btn, nullptr);
    lv_label_set_align(lbl, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(lbl, btn, LV_ALIGN_CENTER, 0, 0);

    groupBtns[i] = btn;
    groupLabels[i] = lbl;
  }

  // ---- 2. Build Room Dimmer Detail View (Intensity + Color Temp Sliders) ----
  detailContainer = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(detailContainer, 240, 240);
  lv_obj_set_pos(detailContainer, 0, 0);
  lv_obj_set_style_local_bg_opa(detailContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(detailContainer, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

  titleLabel = lv_label_create(detailContainer, nullptr);
  lv_obj_set_style_local_text_color(titleLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(titleLabel, "Kitchen");
  lv_obj_align(titleLabel, detailContainer, LV_ALIGN_IN_TOP_LEFT, 10, 12);

  toggleBtn = lv_btn_create(detailContainer, nullptr);
  toggleBtn->user_data = this;
  lv_obj_set_event_cb(toggleBtn, LightEventCallback);
  lv_obj_set_size(toggleBtn, 76, 38);
  lv_obj_align(toggleBtn, detailContainer, LV_ALIGN_IN_TOP_RIGHT, -10, 6);
  lv_obj_set_style_local_radius(toggleBtn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 8);

  toggleBtnLabel = lv_label_create(toggleBtn, nullptr);
  lv_label_set_text_static(toggleBtnLabel, "ON");

  // Intensity Slider (0..100%)
  intensityLabel = lv_label_create(detailContainer, nullptr);
  lv_obj_set_style_local_text_color(intensityLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(intensityLabel, "Dimmer: 80%");
  lv_obj_align(intensityLabel, detailContainer, LV_ALIGN_IN_TOP_LEFT, 10, 56);

  intensitySlider = lv_slider_create(detailContainer, nullptr);
  lv_obj_set_click(intensitySlider, false);
  lv_obj_set_size(intensitySlider, 212, 26);
  lv_obj_align(intensitySlider, detailContainer, LV_ALIGN_IN_TOP_MID, 0, 86);
  lv_slider_set_range(intensitySlider, 0, 100);
  lv_obj_set_style_local_bg_color(intensitySlider, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_style_local_bg_opa(intensitySlider, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_bg_color(intensitySlider, LV_SLIDER_PART_INDIC, LV_STATE_DEFAULT, LV_COLOR_MAKE(0xFF, 0xCD, 0x00));
  lv_obj_set_style_local_bg_opa(intensitySlider, LV_SLIDER_PART_INDIC, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_bg_color(intensitySlider, LV_SLIDER_PART_KNOB, LV_STATE_DEFAULT, LV_COLOR_WHITE);

  // Color Temperature Slider (0..100% Warm -> Cool)
  colorLabel = lv_label_create(detailContainer, nullptr);
  lv_obj_set_style_local_text_color(colorLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(colorLabel, "Color: Warm");
  lv_obj_align(colorLabel, detailContainer, LV_ALIGN_IN_TOP_LEFT, 10, 138);

  colorSlider = lv_slider_create(detailContainer, nullptr);
  lv_obj_set_click(colorSlider, false);
  lv_obj_set_size(colorSlider, 212, 26);
  lv_obj_align(colorSlider, detailContainer, LV_ALIGN_IN_TOP_MID, 0, 164);
  lv_slider_set_range(colorSlider, 0, 100);
  lv_obj_set_style_local_bg_color(colorSlider, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_style_local_bg_opa(colorSlider, LV_SLIDER_PART_BG, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_bg_opa(colorSlider, LV_SLIDER_PART_INDIC, LV_STATE_DEFAULT, LV_OPA_COVER);

  backBtn = lv_btn_create(detailContainer, nullptr);
  backBtn->user_data = this;
  lv_obj_set_event_cb(backBtn, LightEventCallback);
  lv_obj_set_size(backBtn, 220, 32);
  lv_obj_align(backBtn, detailContainer, LV_ALIGN_IN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_local_radius(backBtn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 8);
  lv_obj_set_style_local_bg_color(backBtn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_t* backLbl = lv_label_create(backBtn, nullptr);
  lv_label_set_text_static(backLbl, "Rooms");

  UpdateGridLabels();
  ShowGridView();

  taskRefresh = lv_task_create(RefreshTaskCallback, 50, LV_TASK_PRIO_MID, this);
}

HomeAssistantLights::~HomeAssistantLights() {
  if (dirtyPendingSend) {
    haService.SetLight(selectedGroup, editBrightness, editColorTemp, true);
  }
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void HomeAssistantLights::ShowGridView() {
  if (dirtyPendingSend) {
    haService.SetLight(selectedGroup, editBrightness, editColorTemp, true);
    dirtyPendingSend = false;
  }
  inDetailView = false;
  userEditedInDetail = false;
  UpdateGridLabels();
  lv_obj_set_hidden(detailContainer, true);
  lv_obj_set_hidden(gridContainer, false);
}

void HomeAssistantLights::ShowDetailView(uint8_t groupIdx) {
  selectedGroup = groupIdx % Controllers::HomeAssistantService::NumLights;
  const auto& lt = haService.GetLight(selectedGroup);
  editBrightness = lt.brightness;
  editColorTemp = lt.colorTemp;
  dirtyPendingSend = false;
  userEditedInDetail = false;
  lastDetailGroup = 0xFF;
  lastDetailBrightness = 0xFF;
  lastDetailColorTemp = 0xFF;
  lastDetailIsOn = -1;

  inDetailView = true;
  UpdateDetailWidgets();
  lv_obj_set_hidden(gridContainer, true);
  lv_obj_set_hidden(detailContainer, false);
}

void HomeAssistantLights::UpdateGridLabels() {
  for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumLights; ++i) {
    const auto& lt = haService.GetLight(i);
    auto& cache = gridCache[i];
    if (cache.initialized && cache.brightness == lt.brightness && cache.colorTemp == lt.colorTemp &&
        std::strcmp(cache.name, lt.name) == 0) {
      continue;
    }

    const bool isOn = (lt.brightness > 0);
    const bool wasOn = (cache.brightness > 0);
    if (!cache.initialized || isOn != wasOn) {
      const lv_color_t bgCol = isOn ? LV_COLOR_MAKE(0x28, 0x32, 0x3C) : Colors::bgAlt;
      lv_obj_set_style_local_bg_color(groupBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, bgCol);
    }

    if (!cache.initialized || isOn != wasOn || (isOn && cache.colorTemp != lt.colorTemp)) {
      const lv_color_t txtCol = isOn ? ColorTempToLvColor(lt.colorTemp) : Colors::lightGray;
      lv_obj_set_style_local_text_color(groupLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, txtCol);
    }

    if (!cache.initialized || isOn != wasOn || cache.brightness != lt.brightness || std::strcmp(cache.name, lt.name) != 0) {
      if (isOn) {
        lv_label_set_text_fmt(groupLabels[i], "%s\n%s %d%%", lt.name, Symbols::lightbulb, lt.brightness);
      } else {
        lv_label_set_text_fmt(groupLabels[i], "%s\n%s OFF", lt.name, Symbols::lightbulb);
      }
      lv_obj_align(groupLabels[i], groupBtns[i], LV_ALIGN_CENTER, 0, 0);
    }

    std::strncpy(cache.name, lt.name, sizeof(cache.name) - 1);
    cache.name[sizeof(cache.name) - 1] = '\0';
    cache.brightness = lt.brightness;
    cache.colorTemp = lt.colorTemp;
    cache.initialized = true;
  }
}

void HomeAssistantLights::UpdateDetailWidgets() {
  if (selectedGroup != lastDetailGroup) {
    lastDetailGroup = selectedGroup;
    const auto& lt = haService.GetLight(selectedGroup);
    lv_label_set_text(titleLabel, lt.name);
  }

  const int8_t isOn = (editBrightness > 0) ? 1 : 0;
  if (isOn != lastDetailIsOn) {
    lastDetailIsOn = isOn;
    lv_label_set_text_static(toggleBtnLabel, isOn ? "ON" : "OFF");
    lv_obj_set_style_local_bg_color(toggleBtn,
                                    LV_BTN_PART_MAIN,
                                    LV_STATE_DEFAULT,
                                    isOn ? LV_COLOR_MAKE(0x00, 0xA8, 0x48) : Colors::bgAlt);
  }

  if (editBrightness != lastDetailBrightness) {
    lastDetailBrightness = editBrightness;
    lv_label_set_text_fmt(intensityLabel, "Dimmer: %d%%", editBrightness);
    lv_slider_set_value(intensitySlider, editBrightness, LV_ANIM_OFF);
  }

  if (editColorTemp != lastDetailColorTemp) {
    lastDetailColorTemp = editColorTemp;
    const lv_color_t ctColor = ColorTempToLvColor(editColorTemp);
    lv_label_set_text_fmt(colorLabel, "Color: %s (%d%%)", ColorTempDescription(editColorTemp), editColorTemp);
    lv_slider_set_value(colorSlider, editColorTemp, LV_ANIM_OFF);
    lv_obj_set_style_local_bg_color(colorSlider, LV_SLIDER_PART_INDIC, LV_STATE_DEFAULT, ctColor);
    lv_obj_set_style_local_bg_color(colorSlider, LV_SLIDER_PART_KNOB, LV_STATE_DEFAULT, ctColor);
  }
}

void HomeAssistantLights::OnEvent(lv_obj_t* obj, lv_event_t event) {
  if (!inDetailView) {
    if (event == LV_EVENT_CLICKED) {
      for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumLights; ++i) {
        if (obj == groupBtns[i]) {
          ShowDetailView(i);
          return;
        }
      }
    }
    return;
  }

  if (obj == backBtn && event == LV_EVENT_CLICKED) {
    ShowGridView();
    return;
  }

  if (obj == toggleBtn && event == LV_EVENT_CLICKED) {
    const auto& lt = haService.GetLight(selectedGroup);
    if (editBrightness > 0) {
      editBrightness = 0;
    } else {
      editBrightness = (lt.savedBrightness > 0) ? lt.savedBrightness : 80;
    }
    userEditedInDetail = true;
    UpdateDetailWidgets();
    haService.SetLight(selectedGroup, editBrightness, editColorTemp, true);
    motorController.RunForDuration(15);
    dirtyPendingSend = false;
    return;
  }
}

bool HomeAssistantLights::OnTouchEvent(uint16_t x, uint16_t y) {
  if (!inDetailView) {
    return false;
  }
  // Direct slider coordinate tracking with 28px edge zones so 0% (warmest) and 100% are effortless to hit
  const int32_t clampedX = std::clamp<int32_t>(static_cast<int32_t>(x) - 28, 0, 184);
  const auto pct = static_cast<uint8_t>((clampedX * 100) / 184);

  if (y >= 65 && y <= 130) {
    userEditedInDetail = true;
    if (editBrightness != pct) {
      editBrightness = pct;
      UpdateDetailWidgets();
      dirtyPendingSend = true;
      lastSendTick = xTaskGetTickCount();
    }
    return true;
  }
  if (y >= 145 && y <= 198) {
    userEditedInDetail = true;
    if (editColorTemp != pct) {
      editColorTemp = pct;
      UpdateDetailWidgets();
      dirtyPendingSend = true;
      lastSendTick = xTaskGetTickCount();
    }
    return true;
  }
  return false;
}

bool HomeAssistantLights::OnTouchEvent(TouchEvents event) {
  if (inDetailView) {
    // Prevent swipe gestures from closing the app while adjusting sliders
    if (event == TouchEvents::SwipeDown || event == TouchEvents::SwipeUp || event == TouchEvents::SwipeLeft ||
        event == TouchEvents::SwipeRight) {
      return true;
    }
  }
  return false;
}

bool HomeAssistantLights::OnButtonPushed() {
  if (inDetailView) {
    ShowGridView();
    return true;
  }
  return false;
}

void HomeAssistantLights::Refresh() {
  if (dirtyPendingSend && (xTaskGetTickCount() - lastSendTick >= pdMS_TO_TICKS(250))) {
    haService.SetLight(selectedGroup, editBrightness, editColorTemp, true);
    dirtyPendingSend = false;
  }

  const uint32_t ver = haService.GetLightsVersion();
  if (ver != lastSeenVersion) {
    lastSeenVersion = ver;
    if (inDetailView && !dirtyPendingSend && !userEditedInDetail) {
      const auto& lt = haService.GetLight(selectedGroup);
      editBrightness = lt.brightness;
      editColorTemp = lt.colorTemp;
      UpdateDetailWidgets();
    } else if (!inDetailView) {
      UpdateGridLabels();
    }
  }
}
