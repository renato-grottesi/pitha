#include "displayapp/screens/HomeAssistantSwitches.h"
#include <cstring>
#include "displayapp/InfiniTimeTheme.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void SwitchEventCallback(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<HomeAssistantSwitches*>(obj->user_data);
    if (screen != nullptr) {
      screen->OnEvent(obj, event);
    }
  }
}

HomeAssistantSwitches::HomeAssistantSwitches(Controllers::HomeAssistantService& haService,
                                             Controllers::MotorController& motorController)
  : haService {haService}, motorController {motorController} {

  haService.NotifyScreenOpen(2);
  lastSeenVersion = haService.GetSwitchesVersion();

  for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumSwitches; ++i) {
    const uint8_t col = i % 2;
    const uint8_t row = i / 2;

    lv_obj_t* btn = lv_btn_create(lv_scr_act(), nullptr);
    btn->user_data = this;
    lv_obj_set_event_cb(btn, SwitchEventCallback);
    lv_obj_set_size(btn, 112, 72);
    lv_obj_set_pos(btn, static_cast<lv_coord_t>(4 + col * 120), static_cast<lv_coord_t>(4 + row * 78));
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 10);
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 0);

    lv_obj_t* lbl = lv_label_create(btn, nullptr);
    lv_label_set_align(lbl, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(lbl, btn, LV_ALIGN_CENTER, 0, 0);

    switchBtns[i] = btn;
    switchLabels[i] = lbl;
  }

  UpdateButtons();
  taskRefresh = lv_task_create(RefreshTaskCallback, 50, LV_TASK_PRIO_MID, this);
}

HomeAssistantSwitches::~HomeAssistantSwitches() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void HomeAssistantSwitches::UpdateButtons() {
  for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumSwitches; ++i) {
    const auto& sw = haService.GetSwitch(i);
    auto& cache = switchCache[i];
    if (cache.initialized && cache.state == sw.state && std::strcmp(cache.name, sw.name) == 0) {
      continue;
    }

    if (!cache.initialized || cache.state != sw.state) {
      const lv_color_t bgCol = sw.state ? LV_COLOR_MAKE(0x00, 0x8A, 0x3E) : Colors::bgAlt;
      const lv_color_t txtCol = sw.state ? LV_COLOR_WHITE : Colors::lightGray;

      lv_obj_set_style_local_bg_color(switchBtns[i], LV_BTN_PART_MAIN, LV_STATE_DEFAULT, bgCol);
      lv_obj_set_style_local_bg_color(switchBtns[i], LV_BTN_PART_MAIN, LV_STATE_PRESSED, bgCol);
      lv_obj_set_style_local_text_color(switchLabels[i], LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, txtCol);
    }

    lv_label_set_text_fmt(switchLabels[i], "%s\n%s %s", sw.name, Symbols::toggleOn, sw.state ? "ON" : "OFF");
    lv_obj_align(switchLabels[i], switchBtns[i], LV_ALIGN_CENTER, 0, 0);

    std::strncpy(cache.name, sw.name, sizeof(cache.name) - 1);
    cache.name[sizeof(cache.name) - 1] = '\0';
    cache.state = sw.state;
    cache.initialized = true;
  }
}

void HomeAssistantSwitches::OnEvent(lv_obj_t* obj, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumSwitches; ++i) {
      if (obj == switchBtns[i]) {
        haService.ToggleSwitch(i);
        motorController.RunForDuration(15);
        lastSeenVersion = haService.GetSwitchesVersion();
        UpdateButtons();
        return;
      }
    }
  }
}

void HomeAssistantSwitches::Refresh() {
  const uint32_t ver = haService.GetSwitchesVersion();
  if (ver != lastSeenVersion) {
    lastSeenVersion = ver;
    UpdateButtons();
  }
}
