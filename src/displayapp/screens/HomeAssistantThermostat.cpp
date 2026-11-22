#include "displayapp/screens/HomeAssistantThermostat.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <FreeRTOS.h>
#include <task.h>
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  uint8_t TempToColorBand(uint8_t tempC) {
    if (tempC <= 12) {
      return 0;
    }
    if (tempC <= 17) {
      return 1;
    }
    if (tempC <= 19) {
      return 2;
    }
    return 3;
  }

  lv_color_t TempToArcColor(uint8_t band) {
    if (band == 0) {
      return LV_COLOR_MAKE(0x37, 0x8A, 0xDD); // Cool blue
    }
    if (band == 1) {
      return LV_COLOR_MAKE(0x00, 0xB8, 0x50); // Eco green
    }
    if (band == 2) {
      return LV_COLOR_MAKE(0xFF, 0xA5, 0x00); // Warm amber
    }
    return LV_COLOR_MAKE(0xE3, 0x1B, 0x23); // Heating red
  }
}

HomeAssistantThermostat::HomeAssistantThermostat(Controllers::HomeAssistantService& haService,
                                                 Controllers::MotorController& motorController)
  : haService {haService}, motorController {motorController} {

  haService.NotifyScreenOpen(3);
  targetC = std::clamp<uint8_t>(haService.GetThermostatTarget(), 2, 21);
  lastSeenVersion = haService.GetThermostatVersion();

  tempArc = lv_arc_create(lv_scr_act(), nullptr);
  lv_arc_set_bg_angles(tempArc, 0, 270);
  lv_arc_set_rotation(tempArc, 135);
  lv_arc_set_range(tempArc, 2, 21);
  lv_arc_set_value(tempArc, targetC);
  lv_obj_set_size(tempArc, 228, 228);
  lv_arc_set_adjustable(tempArc, true);
  lv_obj_set_click(tempArc, false);
  lv_obj_align(tempArc, lv_scr_act(), LV_ALIGN_CENTER, 0, 4);

  lv_obj_set_style_local_bg_opa(tempArc, LV_ARC_PART_BG, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(tempArc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_line_width(tempArc, LV_ARC_PART_BG, LV_STATE_DEFAULT, 18);
  lv_obj_set_style_local_line_color(tempArc, LV_ARC_PART_BG, LV_STATE_DEFAULT, Colors::bgAlt);
  lv_obj_set_style_local_line_rounded(tempArc, LV_ARC_PART_BG, LV_STATE_DEFAULT, true);

  lv_obj_set_style_local_line_width(tempArc, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, 18);
  lv_obj_set_style_local_line_rounded(tempArc, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, true);

  lv_obj_set_style_local_bg_color(tempArc, LV_ARC_PART_KNOB, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_bg_opa(tempArc, LV_ARC_PART_KNOB, LV_STATE_DEFAULT, LV_OPA_COVER);

  targetCaption = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(targetCaption, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(targetCaption, "TARGET");
  lv_obj_align(targetCaption, lv_scr_act(), LV_ALIGN_CENTER, 0, -56);

  targetValueLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(targetValueLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);
  lv_obj_set_style_local_text_color(targetValueLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(targetValueLabel, "19\xC2\xB0");
  lv_obj_align(targetValueLabel, lv_scr_act(), LV_ALIGN_CENTER, 6, -4);
  lv_obj_set_auto_realign(targetValueLabel, true);

  currentValueLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(currentValueLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(currentValueLabel, "--.-\xC2\xB0" "C");
  lv_obj_align(currentValueLabel, lv_scr_act(), LV_ALIGN_CENTER, 0, 56);
  lv_obj_set_auto_realign(currentValueLabel, true);

  UpdateWidgets();
  taskRefresh = lv_task_create(RefreshTaskCallback, 50, LV_TASK_PRIO_MID, this);
}

HomeAssistantThermostat::~HomeAssistantThermostat() {
  if (dirtyPendingSend) {
    haService.SetThermostatTarget(targetC, true);
  }
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void HomeAssistantThermostat::UpdateWidgets() {
  if (targetC != lastDrawnTargetC) {
    lastDrawnTargetC = targetC;
    lv_arc_set_value(tempArc, targetC);
    lv_label_set_text_fmt(targetValueLabel, "%d\xC2\xB0", targetC);

    const uint8_t colorBand = TempToColorBand(targetC);
    if (colorBand != lastArcColorBand) {
      lastArcColorBand = colorBand;
      lv_obj_set_style_local_line_color(tempArc, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, TempToArcColor(colorBand));
    }
  }

  const bool hasCurrent = haService.HasThermostatCurrent();
  const int16_t curX10 = hasCurrent ? haService.GetThermostatCurrentX10() : 0;
  if (lastDrawnCurrentX10 == 0x7FFF || hasCurrent != lastHasCurrent || (hasCurrent && curX10 != lastDrawnCurrentX10)) {
    lastHasCurrent = hasCurrent;
    lastDrawnCurrentX10 = curX10;
    if (hasCurrent) {
      const int16_t absX10 = std::abs(curX10);
      lv_label_set_text_fmt(currentValueLabel,
                            "%s%d.%d\xC2\xB0" "C",
                            (curX10 < 0) ? "-" : "",
                            absX10 / 10,
                            absX10 % 10);
    } else {
      lv_label_set_text_static(currentValueLabel, "--.-\xC2\xB0" "C");
    }
  }
}

void HomeAssistantThermostat::OnEvent(lv_obj_t* /*obj*/, lv_event_t /*event*/) {
}

bool HomeAssistantThermostat::OnTouchEvent(uint16_t x, uint16_t y) {
  const int32_t dx = static_cast<int32_t>(x) - 120;
  const int32_t dy = static_cast<int32_t>(y) - 124;
  const int32_t r2 = dx * dx + dy * dy;

  // Track touches anywhere along the circular arc ring (radius ~55..120px)
  if (r2 >= 55 * 55) {
    // Angle in degrees [0..360) where 0 is right (+X), 90 is down (+Y)
    float deg = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) * (180.0f / 3.14159265f);
    if (deg < 0.0f) {
      deg += 360.0f;
    }
    // Arc starts at 135 deg and sweeps 270 deg clockwise
    float rel = deg - 135.0f;
    if (rel < 0.0f) {
      rel += 360.0f;
    }
    if (rel <= 285.0f) {
      rel = std::clamp(rel, 0.0f, 270.0f);
      const auto val = static_cast<uint8_t>(std::lround(2.0f + (rel * 19.0f) / 270.0f));
      const uint8_t clampedVal = std::clamp<uint8_t>(val, 2, 21);
      if (clampedVal != targetC) {
        targetC = clampedVal;
        UpdateWidgets();
        dirtyPendingSend = true;
        lastChangeTick = xTaskGetTickCount();
      }
    }
    return true;
  }
  return false;
}

bool HomeAssistantThermostat::OnTouchEvent(TouchEvents event) {
  // Block swipe gestures from closing the thermostat while dragging the circular slider
  if (event == TouchEvents::SwipeDown || event == TouchEvents::SwipeUp || event == TouchEvents::SwipeLeft ||
      event == TouchEvents::SwipeRight) {
    return true;
  }
  return false;
}

void HomeAssistantThermostat::Refresh() {
  if (dirtyPendingSend && (xTaskGetTickCount() - lastChangeTick >= pdMS_TO_TICKS(250))) {
    haService.SetThermostatTarget(targetC, true);
    motorController.RunForDuration(15);
    dirtyPendingSend = false;
  }

  const uint32_t ver = haService.GetThermostatVersion();
  if (ver != lastSeenVersion) {
    lastSeenVersion = ver;
    if (!dirtyPendingSend) {
      targetC = std::clamp<uint8_t>(haService.GetThermostatTarget(), 2, 21);
    }
    UpdateWidgets();
  }
}
