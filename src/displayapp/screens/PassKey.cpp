#include "PassKey.h"
#include "displayapp/DisplayApp.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void ButtonEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<PassKey*>(obj->user_data);
    screen->OnButtonEvent(obj, event);
  }
}

PassKey::PassKey(const Pinetime::Controllers::Ble& bleController, Pinetime::Controllers::NimbleController& nimble)
  : bleController {bleController}, nimble {nimble} {
  isApprovalMode = bleController.IsPendingConnectionApproval();
  startTick = xTaskGetTickCount();

  if (isApprovalMode) {
    passkeyLabel = lv_label_create(lv_scr_act(), nullptr);
    lv_label_set_align(passkeyLabel, LV_LABEL_ALIGN_CENTER);
    lv_obj_set_style_local_text_color(passkeyLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_label_set_text_static(passkeyLabel, "Accept incoming\nBT Connection?");
    lv_obj_align(passkeyLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 8);

    const auto& addr = bleController.PeerAddress();
    macLabel = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_color(macLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
    lv_label_set_text_fmt(
      macLabel, "%02X:%02X:%02X:%02X:%02X:%02X", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    lv_obj_align(macLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 60);

    btnYes = lv_btn_create(lv_scr_act(), nullptr);
    btnYes->user_data = this;
    lv_obj_set_event_cb(btnYes, ButtonEventHandler);
    lv_obj_set_size(btnYes, 108, 60);
    lv_obj_set_pos(btnYes, 8, 96);
    lv_obj_set_style_local_bg_color(btnYes, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x00, 0x80, 0x00));
    lv_obj_t* lblYes = lv_label_create(btnYes, nullptr);
    lv_label_set_text_static(lblYes, "Yes");

    btnNo = lv_btn_create(lv_scr_act(), nullptr);
    btnNo->user_data = this;
    lv_obj_set_event_cb(btnNo, ButtonEventHandler);
    lv_obj_set_size(btnNo, 108, 60);
    lv_obj_set_pos(btnNo, 124, 96);
    lv_obj_set_style_local_bg_color(btnNo, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0xb0, 0x00, 0x00));
    lv_obj_t* lblNo = lv_label_create(btnNo, nullptr);
    lv_label_set_text_static(lblNo, "No");

    btnAlways = lv_btn_create(lv_scr_act(), nullptr);
    btnAlways->user_data = this;
    lv_obj_set_event_cb(btnAlways, ButtonEventHandler);
    lv_obj_set_size(btnAlways, 224, 60);
    lv_obj_set_pos(btnAlways, 8, 168);
    lv_obj_set_style_local_bg_color(btnAlways, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, Colors::blue);
    lv_obj_t* lblAlways = lv_label_create(btnAlways, nullptr);
    lv_label_set_text_static(lblAlways, "Always");

    taskRefresh = lv_task_create(RefreshTaskCallback, 200, LV_TASK_PRIO_MID, this);
  } else {
    passkeyLabel = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_color(passkeyLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_YELLOW);
    lv_obj_set_style_local_text_font(passkeyLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
    lv_label_set_text_fmt(passkeyLabel, "%06u", bleController.GetPairingKey());
    lv_obj_align(passkeyLabel, nullptr, LV_ALIGN_CENTER, 0, -20);
  }
}

PassKey::~PassKey() {
  if (taskRefresh != nullptr) {
    lv_task_del(taskRefresh);
  }
  lv_obj_clean(lv_scr_act());
}

void PassKey::Refresh() {
  if (!isApprovalMode) {
    return;
  }
  if (!bleController.IsConnected() || !bleController.IsPendingConnectionApproval()) {
    running = false;
    return;
  }
  if ((xTaskGetTickCount() - startTick) >= pdMS_TO_TICKS(20000)) {
    nimble.RejectConnection();
    running = false;
  }
}

void PassKey::OnButtonEvent(lv_obj_t* object, lv_event_t event) {
  if (event != LV_EVENT_CLICKED) {
    return;
  }
  if (object == btnYes) {
    nimble.AcceptConnection(false);
    running = false;
  } else if (object == btnAlways) {
    nimble.AcceptConnection(true);
    running = false;
  } else if (object == btnNo) {
    nimble.RejectConnection();
    running = false;
  }
}
