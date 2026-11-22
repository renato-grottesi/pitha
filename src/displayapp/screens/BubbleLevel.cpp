#include "displayapp/screens/BubbleLevel.h"
#include <algorithm>
#include <cstdlib>
#include "displayapp/InfiniTimeTheme.h"
#include "utility/Math.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr lv_coord_t CenterX = 120;
  constexpr lv_coord_t CenterY = 128;
  constexpr int32_t MaxRadiusPx = 78;
  constexpr int16_t BubbleSize = 34;

  static constexpr lv_point_t hCrossPoints[] = {{24, CenterY}, {216, CenterY}};
  static constexpr lv_point_t vCrossPoints[] = {{CenterX, 32}, {CenterX, 224}};

  uint32_t IntegerSqrt(uint32_t n) {
    if (n == 0) {
      return 0;
    }
    uint32_t x = n;
    uint32_t y = (x + 1U) / 2U;
    while (y < x) {
      x = y;
      y = (x + n / x) / 2U;
    }
    return x;
  }

  int32_t MgToTenthsDeg(int32_t mg) {
    if (std::abs(mg) <= 250) {
      return (mg * 573) / 1024;
    }
    return static_cast<int32_t>(Pinetime::Utility::Asin(static_cast<int16_t>(std::clamp<int32_t>(mg * 32, -32767, 32767)))) * 10;
  }
}

BubbleLevel::BubbleLevel(Controllers::MotionController& motionController, System::SystemTask& systemTask)
  : motionController {motionController}, wakeLock(systemTask) {

  wakeLock.Lock();

  filterX = static_cast<int32_t>(motionController.X()) * 16;
  filterY = static_cast<int32_t>(motionController.Y()) * 16;

  angleLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_align(angleLabel, LV_LABEL_ALIGN_CENTER);
  lv_obj_set_style_local_text_color(angleLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(angleLabel, "X:+0.0\xc2\xb0 Y:+0.0\xc2\xb0");
  lv_obj_align(angleLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 4);

  outerRing = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(outerRing, false);
  lv_obj_set_size(outerRing, 196, 196);
  lv_obj_set_pos(outerRing, CenterX - 98, CenterY - 98);
  lv_obj_set_style_local_radius(outerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_color(outerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_MAKE(0x0C, 0x1E, 0x14));
  lv_obj_set_style_local_border_color(outerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_obj_set_style_local_border_width(outerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);

  hLine = lv_line_create(lv_scr_act(), nullptr);
  lv_obj_set_click(hLine, false);
  lv_line_set_points(hLine, hCrossPoints, 2);
  lv_obj_set_style_local_line_color(hLine, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
  lv_obj_set_style_local_line_width(hLine, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, 1);

  vLine = lv_line_create(lv_scr_act(), nullptr);
  lv_obj_set_click(vLine, false);
  lv_line_set_points(vLine, vCrossPoints, 2);
  lv_obj_set_style_local_line_color(vLine, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
  lv_obj_set_style_local_line_width(vLine, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, 1);

  midRing = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(midRing, false);
  lv_obj_set_size(midRing, 104, 104);
  lv_obj_set_pos(midRing, CenterX - 52, CenterY - 52);
  lv_obj_set_style_local_radius(midRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_opa(midRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_color(midRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
  lv_obj_set_style_local_border_width(midRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);

  centerRing = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(centerRing, false);
  lv_obj_set_size(centerRing, 44, 44);
  lv_obj_set_pos(centerRing, CenterX - 22, CenterY - 22);
  lv_obj_set_style_local_radius(centerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_opa(centerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_color(centerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_border_width(centerRing, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);

  bubble = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(bubble, false);
  lv_obj_set_size(bubble, BubbleSize, BubbleSize);
  lv_obj_set_pos(bubble, CenterX - BubbleSize / 2, CenterY - BubbleSize / 2);
  lv_obj_set_style_local_radius(bubble, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_color(bubble, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
  lv_obj_set_style_local_border_color(bubble, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_border_width(bubble, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);

  Refresh();
  taskRefresh = lv_task_create(RefreshTaskCallback, 40, LV_TASK_PRIO_MID, this);
}

BubbleLevel::~BubbleLevel() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void BubbleLevel::Refresh() {
  filterX = (filterX + static_cast<int32_t>(motionController.X()) * 16) / 2;
  filterY = (filterY + static_cast<int32_t>(motionController.Y()) * 16) / 2;

  const int32_t adjX = (filterX / 16) - calX;
  const int32_t adjY = (filterY / 16) - calY;

  int32_t dx = (-adjX * MaxRadiusPx) / 512;
  int32_t dy = (adjY * MaxRadiusPx) / 512;

  const uint32_t distSq = static_cast<uint32_t>(dx * dx + dy * dy);
  if (distSq > static_cast<uint32_t>(MaxRadiusPx * MaxRadiusPx)) {
    const int32_t dist = static_cast<int32_t>(IntegerSqrt(distSq));
    if (dist > 0) {
      dx = (dx * MaxRadiusPx) / dist;
      dy = (dy * MaxRadiusPx) / dist;
    }
  }

  const auto newBubbleX = static_cast<lv_coord_t>(CenterX + dx - BubbleSize / 2);
  const auto newBubbleY = static_cast<lv_coord_t>(CenterY + dy - BubbleSize / 2);
  if (newBubbleX != lastBubbleX || newBubbleY != lastBubbleY) {
    lv_obj_set_pos(bubble, newBubbleX, newBubbleY);
    lastBubbleX = newBubbleX;
    lastBubbleY = newBubbleY;
  }

  const int32_t tenthsX = MgToTenthsDeg(-adjX);
  const int32_t tenthsY = MgToTenthsDeg(adjY);
  const int32_t absX = std::abs(tenthsX);
  const int32_t absY = std::abs(tenthsY);
  const char signX = (tenthsX < 0) ? '-' : '+';
  const char signY = (tenthsY < 0) ? '-' : '+';
  const char* calTag = isCalibrated ? "*" : "";

  TiltZone zone = TiltZone::Tilted;
  if (lastZone == TiltZone::Level) {
    if (absX <= 16 && absY <= 16) {
      zone = TiltZone::Level;
    } else if (absX <= 50 && absY <= 50) {
      zone = TiltZone::Near;
    }
  } else {
    if (absX <= 10 && absY <= 10) {
      zone = TiltZone::Level;
    } else if (absX <= 50 && absY <= 50) {
      zone = TiltZone::Near;
    }
  }

  if (zone != lastZone) {
    const lv_color_t bubbleCol = (zone == TiltZone::Level) ? LV_COLOR_MAKE(0x20, 0xE8, 0x40)
                                                           : ((zone == TiltZone::Near) ? LV_COLOR_YELLOW : Colors::orange);
    lv_obj_set_style_local_bg_color(bubble, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, bubbleCol);

    const bool wasLevel = (lastZone == TiltZone::Level);
    const bool isLevel = (zone == TiltZone::Level);
    if (lastZone == TiltZone::Unknown || wasLevel != isLevel) {
      lv_obj_set_style_local_border_color(centerRing,
                                          LV_OBJ_PART_MAIN,
                                          LV_STATE_DEFAULT,
                                          isLevel ? LV_COLOR_MAKE(0x20, 0xE8, 0x40) : LV_COLOR_WHITE);
      lv_obj_set_style_local_text_color(angleLabel,
                                        LV_LABEL_PART_MAIN,
                                        LV_STATE_DEFAULT,
                                        isLevel ? LV_COLOR_MAKE(0x20, 0xE8, 0x40) : LV_COLOR_WHITE);
    }
  }

  if (tenthsX != lastTenthsX || tenthsY != lastTenthsY || zone != lastZone || isCalibrated != lastCalibrated) {
    if (zone == TiltZone::Level) {
      lv_label_set_text_fmt(angleLabel,
                            "LEVEL %c%ld.%ld %c%ld.%ld%s",
                            signX,
                            static_cast<long>(absX / 10),
                            static_cast<long>(absX % 10),
                            signY,
                            static_cast<long>(absY / 10),
                            static_cast<long>(absY % 10),
                            calTag);
    } else {
      lv_label_set_text_fmt(angleLabel,
                            "X:%c%ld.%ld\xc2\xb0 Y:%c%ld.%ld\xc2\xb0%s",
                            signX,
                            static_cast<long>(absX / 10),
                            static_cast<long>(absX % 10),
                            signY,
                            static_cast<long>(absY / 10),
                            static_cast<long>(absY % 10),
                            calTag);
    }
    lv_obj_align(angleLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 4);
    lastTenthsX = tenthsX;
    lastTenthsY = tenthsY;
    lastCalibrated = isCalibrated;
  }

  lastZone = zone;
}

bool BubbleLevel::OnTouchEvent(TouchEvents event) {
  if (event == TouchEvents::Tap) {
    if (!isCalibrated) {
      const int16_t curX = static_cast<int16_t>(filterX / 16);
      const int16_t curY = static_cast<int16_t>(filterY / 16);
      if (std::abs(curX) < 300 && std::abs(curY) < 300) {
        calX = curX;
        calY = curY;
        isCalibrated = true;
      }
    } else {
      calX = 0;
      calY = 0;
      isCalibrated = false;
    }
    return true;
  }
  return false;
}
