#include "displayapp/screens/HomeAssistantBtRestart.h"
#include <FreeRTOS.h>
#include <task.h>
#include "displayapp/InfiniTimeTheme.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void BtRestartBtnCallback(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<HomeAssistantBtRestart*>(obj->user_data);
    if (screen != nullptr) {
      screen->OnButtonEvent(obj, event);
    }
  }
}

HomeAssistantBtRestart::HomeAssistantBtRestart(Controllers::HomeAssistantService& haService,
                                               Controllers::MotorController& motorController)
  : haService {haService}, motorController {motorController} {

  titleLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(titleLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_fmt(titleLabel, "%s Bluetooth", Symbols::bluetooth);
  lv_obj_align(titleLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 14);

  statusLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_align(statusLabel, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(statusLabel, nullptr, LV_ALIGN_CENTER, 0, -10);

  restartBtn = lv_btn_create(lv_scr_act(), nullptr);
  restartBtn->user_data = this;
  lv_obj_set_event_cb(restartBtn, BtRestartBtnCallback);
  lv_obj_set_size(restartBtn, 216, 56);
  lv_obj_align(restartBtn, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_local_radius(restartBtn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 10);
  lv_obj_set_style_local_bg_color(restartBtn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::blue);

  lv_obj_t* btnLbl = lv_label_create(restartBtn, nullptr);
  lv_label_set_text_fmt(btnLbl, "%s Restart BT", Symbols::bluetooth);

  TriggerRestart();

  taskRefresh = lv_task_create(RefreshTaskCallback, 250, LV_TASK_PRIO_MID, this);
}

HomeAssistantBtRestart::~HomeAssistantBtRestart() {
  if (taskRefresh != nullptr) {
    lv_task_del(taskRefresh);
  }
  lv_obj_clean(lv_scr_act());
}

void HomeAssistantBtRestart::TriggerRestart() {
  restartTick = xTaskGetTickCount();
  haService.RestartBluetooth();
  motorController.RunForDuration(30);
  UpdateStatusUI();
}

void HomeAssistantBtRestart::UpdateStatusUI() {
  const uint32_t elapsedMs = ((xTaskGetTickCount() - restartTick) * 1000) / configTICK_RATE_HZ;
  BtStatus status = BtStatus::Advertising;
  if (elapsedMs < 1200) {
    status = BtStatus::Restarting;
  } else if (haService.IsBleConnected()) {
    status = BtStatus::Connected;
  }

  if (status == lastStatus) {
    return;
  }
  lastStatus = status;

  if (status == BtStatus::Restarting) {
    lv_obj_set_style_local_text_color(statusLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_YELLOW);
    lv_label_set_text_static(statusLabel, "Restarting BT...\nDropping old link");
  } else if (status == BtStatus::Connected) {
    lv_obj_set_style_local_text_color(statusLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x00, 0xE0, 0x60));
    lv_label_set_text_static(statusLabel, "Connected!\nHome Assistant Active");
  } else {
    lv_obj_set_style_local_text_color(statusLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_CYAN);
    lv_label_set_text_static(statusLabel, "Fast Advertising...\nWaiting for HA");
  }
  lv_obj_align(statusLabel, nullptr, LV_ALIGN_CENTER, 0, -10);
}

void HomeAssistantBtRestart::Refresh() {
  UpdateStatusUI();
}

void HomeAssistantBtRestart::OnButtonEvent(lv_obj_t* obj, lv_event_t event) {
  if (obj == restartBtn && event == LV_EVENT_CLICKED) {
    TriggerRestart();
  }
}
