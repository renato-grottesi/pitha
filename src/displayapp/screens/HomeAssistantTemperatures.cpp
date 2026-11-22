#include "displayapp/screens/HomeAssistantTemperatures.h"
#include <cstdlib>
#include <cstring>
#include "displayapp/InfiniTimeTheme.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

HomeAssistantTemperatures::HomeAssistantTemperatures(Controllers::HomeAssistantService& haService) : haService {haService} {
  haService.NotifyScreenOpen(4);
  lastSeenVersion = haService.GetTempsVersion();

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0xFF, 0xCD, 0x00));
  lv_label_set_text_fmt(title, "%s TEMPERATURES", Symbols::tempHigh);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 0, 4);

  for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumTemps; ++i) {
    const lv_coord_t yPos = static_cast<lv_coord_t>(34 + i * 34);

    lv_obj_t* rowBg = lv_obj_create(lv_scr_act(), nullptr);
    lv_obj_set_size(rowBg, 228, 30);
    lv_obj_set_pos(rowBg, 6, yPos);
    lv_obj_set_click(rowBg, false);
    lv_obj_set_style_local_bg_color(rowBg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, Colors::bgAlt);
    lv_obj_set_style_local_radius(rowBg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 6);
    lv_obj_set_style_local_border_width(rowBg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

    lv_obj_t* nLbl = lv_label_create(rowBg, nullptr);
    lv_obj_set_style_local_text_color(nLbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_label_set_text_static(nLbl, "");
    lv_obj_align(nLbl, rowBg, LV_ALIGN_IN_LEFT_MID, 8, 0);

    lv_obj_t* vLbl = lv_label_create(rowBg, nullptr);
    lv_obj_set_style_local_text_color(vLbl, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
    lv_label_set_text_static(vLbl, "--.-\xC2\xB0" "C");
    lv_obj_align(vLbl, rowBg, LV_ALIGN_IN_RIGHT_MID, -8, 0);
    lv_obj_set_auto_realign(vLbl, true);

    nameLabels[i] = nLbl;
    valLabels[i] = vLbl;
  }

  UpdateRows();
  taskRefresh = lv_task_create(RefreshTaskCallback, 50, LV_TASK_PRIO_LOW, this);
}

HomeAssistantTemperatures::~HomeAssistantTemperatures() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void HomeAssistantTemperatures::UpdateRows() {
  for (uint8_t i = 0; i < Controllers::HomeAssistantService::NumTemps; ++i) {
    const auto& sensor = haService.GetTempSensor(i);
    auto& cache = tempCache[i];
    if (cache.initialized && cache.valid == sensor.valid && (!sensor.valid || cache.tempX10 == sensor.tempX10) &&
        std::strcmp(cache.name, sensor.name) == 0) {
      continue;
    }

    if (!cache.initialized || std::strcmp(cache.name, sensor.name) != 0) {
      lv_label_set_text_fmt(nameLabels[i], "%s", sensor.name);
      std::strncpy(cache.name, sensor.name, sizeof(cache.name) - 1);
      cache.name[sizeof(cache.name) - 1] = '\0';
    }

    if (!cache.initialized || cache.valid != sensor.valid || (sensor.valid && cache.tempX10 != sensor.tempX10)) {
      if (!cache.initialized || cache.valid != sensor.valid) {
        lv_obj_set_style_local_text_color(valLabels[i],
                                          LV_LABEL_PART_MAIN,
                                          LV_STATE_DEFAULT,
                                          sensor.valid ? LV_COLOR_MAKE(0x00, 0xE0, 0x70) : Colors::lightGray);
      }

      if (sensor.valid) {
        const int16_t absX10 = std::abs(sensor.tempX10);
        lv_label_set_text_fmt(valLabels[i],
                              "%s%d.%d\xC2\xB0" "C",
                              (sensor.tempX10 < 0) ? "-" : "",
                              absX10 / 10,
                              absX10 % 10);
      } else {
        lv_label_set_text_static(valLabels[i], "--.-\xC2\xB0" "C");
      }
      cache.valid = sensor.valid;
      cache.tempX10 = sensor.tempX10;
    }

    cache.initialized = true;
  }
}

bool HomeAssistantTemperatures::OnTouchEvent(TouchEvents event) {
  if (event == TouchEvents::Tap) {
    haService.NotifyScreenOpen(4);
    return true;
  }
  return false;
}

void HomeAssistantTemperatures::Refresh() {
  const uint32_t ver = haService.GetTempsVersion();
  if (ver != lastSeenVersion) {
    lastSeenVersion = ver;
    UpdateRows();
  }
}
