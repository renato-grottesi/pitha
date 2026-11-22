#include "displayapp/screens/WatchFacePolygons.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <lvgl/lvgl.h>
#include "displayapp/screens/BleIcon.h"
#include "displayapp/screens/NotificationIcon.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/screens/WeatherSymbols.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/heartrate/HeartRateController.h"
#include "components/motion/MotionController.h"
#include "components/pomodoro/PomodoroController.h"
#include "components/settings/Settings.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr int16_t LV_TRIG_SCALE = std::numeric_limits<int16_t>::max();

  constexpr lv_color_t colorWhite = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
  constexpr lv_color_t colorBlack = LV_COLOR_MAKE(0x00, 0x00, 0x00);
  constexpr lv_color_t colorRed = LV_COLOR_MAKE(0xE3, 0x1B, 0x23);
  constexpr lv_color_t colorGreen = LV_COLOR_MAKE(0x00, 0xA8, 0x48);
  constexpr lv_color_t colorBrightGreen = LV_COLOR_MAKE(0x00, 0xD0, 0x54);
  constexpr lv_color_t colorYellow = LV_COLOR_MAKE(0xFF, 0xCD, 0x00);
  constexpr lv_color_t colorBlue = LV_COLOR_MAKE(0x1E, 0x64, 0xFF);
  constexpr lv_color_t colorViolet = LV_COLOR_MAKE(0xA8, 0x20, 0xF0);
  constexpr lv_color_t colorCyan = LV_COLOR_MAKE(0x00, 0xC2, 0xE0);
  constexpr lv_color_t colorMuted = LV_COLOR_MAKE(0xB8, 0xB8, 0xB2);

  // Indexed by state = (inRed ? 1 : 0) | (inGreen ? 2 : 0) | (inBlue ? 4 : 0)
  // 0 (000): Black bg         -> White digit
  // 1 (001): Red bg           -> Green digit
  // 2 (010): Green bg         -> Red digit
  // 3 (011): Yellow (R+G) bg  -> Blue digit
  // 4 (100): Blue bg          -> Yellow digit
  // 5 (101): Violet (R+B) bg  -> Green digit
  // 6 (110): Cyan (G+B) bg    -> Red digit
  // 7 (111): White (R+G+B) bg -> Black digit
  constexpr lv_color_t bgPalette[8] = {
    colorBlack,
    colorRed,
    colorGreen,
    colorYellow,
    colorBlue,
    colorViolet,
    colorCyan,
    colorWhite,
  };

  constexpr lv_color_t digitPalette[8] = {
    colorWhite,
    colorBrightGreen,
    colorRed,
    colorBlue,
    colorYellow,
    colorBrightGreen,
    colorRed,
    colorBlack,
  };

  // Irregular convex quadrilateral (~15% smaller, ~+-10% edge/angle variation)
  constexpr int16_t quadBaseAngles[4] = {42, 136, 224, 317};
  constexpr int16_t quadBaseRadii[4] = {66, 59, 64, 58};

  // Irregular convex hexagon (~15% smaller, ~+-10% edge/angle variation)
  constexpr int16_t hexBaseAngles[6] = {0, 56, 123, 182, 237, 304};
  constexpr int16_t hexBaseRadii[6] = {64, 58, 63, 57, 65, 59};

  // Irregular triangle (~15% smaller, ~+-10% edge/angle variation)
  constexpr int16_t triBaseAngles[3] = {18, 142, 255};
  constexpr int16_t triBaseRadii[3] = {65, 58, 62};

  int16_t Cosine(int16_t angle) {
    return _lv_trigo_sin(angle + 90);
  }

  int16_t Sine(int16_t angle) {
    return _lv_trigo_sin(angle);
  }

  template <std::size_t N>
  void GetConvexPolySpan(const lv_point_t (&pts)[N], int32_t y, int32_t& xMin, int32_t& xMax) {
    xMin = 1000;
    xMax = -1000;
    for (std::size_t i = 0; i < N; ++i) {
      const std::size_t nextIdx = (i + 1 == N) ? 0 : (i + 1);
      const lv_point_t p1 = pts[i];
      const lv_point_t p2 = pts[nextIdx];
      if ((p1.y <= y && p2.y >= y) || (p2.y <= y && p1.y >= y)) {
        if (p1.y == p2.y) {
          xMin = std::min<int32_t>(xMin, std::min(p1.x, p2.x));
          xMax = std::max<int32_t>(xMax, std::max(p1.x, p2.x));
        } else {
          const int32_t x = p1.x + ((p2.x - p1.x) * (y - p1.y)) / (p2.y - p1.y);
          if (x < xMin) {
            xMin = x;
          }
          if (x > xMax) {
            xMax = x;
          }
        }
      }
    }
  }

  lv_area_t ClampPaddedArea(int16_t minX, int16_t minY, int16_t maxX, int16_t maxY) {
    return lv_area_t {
      .x1 = static_cast<lv_coord_t>(std::clamp<int16_t>(minX - 1, 0, 239)),
      .y1 = static_cast<lv_coord_t>(std::clamp<int16_t>(minY - 1, 0, 239)),
      .x2 = static_cast<lv_coord_t>(std::clamp<int16_t>(maxX + 1, 0, 239)),
      .y2 = static_cast<lv_coord_t>(std::clamp<int16_t>(maxY + 1, 0, 239)),
    };
  }

  lv_design_res_t BgDesignCallback(lv_obj_t* obj, const lv_area_t* clipArea, lv_design_mode_t mode) {
    if (mode == LV_DESIGN_COVER_CHK) {
      return LV_DESIGN_RES_COVER;
    }
    auto* screen = static_cast<WatchFacePolygons*>(obj->user_data);
    if (screen == nullptr) {
      return LV_DESIGN_RES_OK;
    }
    if (mode == LV_DESIGN_DRAW_MAIN) {
      screen->ClearMaskBuffer(obj, clipArea);
    } else if (mode == LV_DESIGN_DRAW_POST) {
      screen->DrawColorSplit(obj, clipArea);
    }
    return LV_DESIGN_RES_OK;
  }
}

WatchFacePolygons::WatchFacePolygons(Controllers::DateTime& dateTimeController,
                                     const Controllers::Battery& batteryController,
                                     const Controllers::Ble& bleController,
                                     const Controllers::AlarmController& alarmController,
                                     Controllers::Timer& timerController,
                                     Controllers::StopWatchController& stopWatchController,
                                     Controllers::PomodoroController& pomodoroController,
                                     Controllers::NotificationManager& notificationManager,
                                     Controllers::Settings& settingsController,
                                     Controllers::HeartRateController& heartRateController,
                                     Controllers::MotionController& motionController,
                                     Controllers::SimpleWeatherService& weatherService)
  : dateTimeController {dateTimeController},
    batteryController {batteryController},
    bleController {bleController},
    alarmController {alarmController},
    timerController {timerController},
    stopWatchController {stopWatchController},
    pomodoroController {pomodoroController},
    notificationManager {notificationManager},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController},
    weatherService {weatherService} {

  bgObject = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(bgObject, 240, 240);
  lv_obj_set_pos(bgObject, 0, 0);
  lv_obj_set_click(bgObject, false);
  bgObject->user_data = this;
  lv_obj_set_design_cb(bgObject, BgDesignCallback);

  // Giant stacked Hours (top) and Minutes (bottom) as children of bgObject
  // so LVGL renders them between LV_DESIGN_DRAW_MAIN and LV_DESIGN_DRAW_POST.
  labelHour = lv_label_create(bgObject, nullptr);
  lv_obj_set_style_local_text_font(labelHour, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_extrabold_145);
  lv_obj_set_style_local_text_color(labelHour, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(labelHour, "00");
  lv_obj_align(labelHour, bgObject, LV_ALIGN_IN_TOP_MID, 0, 6);
  lv_obj_set_auto_realign(labelHour, true);

  labelMinute = lv_label_create(bgObject, nullptr);
  lv_obj_set_style_local_text_font(labelMinute, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_extrabold_145);
  lv_obj_set_style_local_text_color(labelMinute, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(labelMinute, "00");
  lv_obj_align(labelMinute, bgObject, LV_ALIGN_IN_BOTTOM_MID, 0, -6);
  lv_obj_set_auto_realign(labelMinute, true);

  // ---- Tap-to-Reveal Top Overlay Panel ----
  topPanel = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(topPanel, 224, 54);
  lv_obj_align(topPanel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 6);
  lv_obj_set_click(topPanel, false);
  lv_obj_set_style_local_bg_color(topPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_bg_opa(topPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_80);
  lv_obj_set_style_local_radius(topPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 8);
  lv_obj_set_style_local_border_width(topPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

  notificationIcon = lv_label_create(topPanel, nullptr);
  lv_obj_set_style_local_text_color(notificationIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorYellow);
  lv_label_set_text_static(notificationIcon, "");
  lv_obj_align(notificationIcon, topPanel, LV_ALIGN_IN_TOP_LEFT, 8, 5);

  servicesIcons = lv_label_create(topPanel, nullptr);
  lv_obj_set_style_local_text_color(servicesIcons, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorMuted);
  lv_label_set_text_static(servicesIcons, "");
  lv_obj_align(servicesIcons, topPanel, LV_ALIGN_IN_TOP_LEFT, 28, 5);

  batteryValue = lv_label_create(topPanel, nullptr);
  lv_obj_set_style_local_text_color(batteryValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorWhite);
  lv_label_set_text_static(batteryValue, "--%");
  lv_obj_align(batteryValue, topPanel, LV_ALIGN_IN_TOP_RIGHT, -8, 5);
  lv_obj_set_auto_realign(batteryValue, true);

  bleIcon = lv_label_create(topPanel, nullptr);
  lv_obj_set_style_local_text_color(bleIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorBlue);
  lv_label_set_text_static(bleIcon, "");
  lv_obj_align(bleIcon, batteryValue, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  lv_obj_set_auto_realign(bleIcon, true);

  labelDateWeek = lv_label_create(topPanel, nullptr);
  lv_obj_set_style_local_text_color(labelDateWeek, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorYellow);
  lv_label_set_text_static(labelDateWeek, "");
  lv_obj_align(labelDateWeek, topPanel, LV_ALIGN_IN_BOTTOM_MID, 0, -4);
  lv_obj_set_auto_realign(labelDateWeek, true);

  // ---- Tap-to-Reveal Bottom Overlay Panel ----
  bottomPanel = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(bottomPanel, 224, 36);
  lv_obj_align(bottomPanel, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -6);
  lv_obj_set_click(bottomPanel, false);
  lv_obj_set_style_local_bg_color(bottomPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_bg_opa(bottomPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_80);
  lv_obj_set_style_local_radius(bottomPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 8);
  lv_obj_set_style_local_border_width(bottomPanel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

  stepIcon = lv_label_create(bottomPanel, nullptr);
  lv_obj_set_style_local_text_color(stepIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorGreen);
  lv_label_set_text_static(stepIcon, Symbols::shoe);
  lv_obj_align(stepIcon, bottomPanel, LV_ALIGN_IN_LEFT_MID, 8, 0);

  stepValue = lv_label_create(bottomPanel, nullptr);
  lv_obj_set_style_local_text_color(stepValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorWhite);
  lv_label_set_text_static(stepValue, "0");
  lv_obj_align(stepValue, stepIcon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

  heartbeatIcon = lv_label_create(bottomPanel, nullptr);
  lv_obj_set_style_local_text_color(heartbeatIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorRed);
  lv_label_set_text_static(heartbeatIcon, Symbols::heartBeat);
  lv_obj_align(heartbeatIcon, bottomPanel, LV_ALIGN_CENTER, -12, 0);

  heartbeatValue = lv_label_create(bottomPanel, nullptr);
  lv_obj_set_style_local_text_color(heartbeatValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorWhite);
  lv_label_set_text_static(heartbeatValue, "--");
  lv_obj_align(heartbeatValue, heartbeatIcon, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

  weatherValue = lv_label_create(bottomPanel, nullptr);
  lv_obj_set_style_local_text_color(weatherValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorWhite);
  lv_label_set_text_static(weatherValue, "--");
  lv_obj_align(weatherValue, bottomPanel, LV_ALIGN_IN_RIGHT_MID, -8, 0);
  lv_obj_set_auto_realign(weatherValue, true);

  weatherIcon = lv_label_create(bottomPanel, nullptr);
  lv_obj_set_style_local_text_color(weatherIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorYellow);
  lv_obj_set_style_local_text_font(weatherIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &fontawesome_weathericons);
  lv_label_set_text_static(weatherIcon, "");
  lv_obj_align(weatherIcon, weatherValue, LV_ALIGN_OUT_LEFT_MID, -4, 0);
  lv_obj_set_auto_realign(weatherIcon, true);

  SetInfoOverlayVisible(false);

  StepShapes();
  Refresh();
  taskRefresh = lv_task_create(RefreshTaskCallback, 100, LV_TASK_PRIO_MID, this);
}

WatchFacePolygons::~WatchFacePolygons() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

bool WatchFacePolygons::OnTouchEvent(TouchEvents event) {
  if (event == TouchEvents::Tap) {
    const TickType_t now = xTaskGetTickCount();
    if (now - lastTapTick < pdMS_TO_TICKS(350)) {
      return true;
    }
    lastTapTick = now;
    infoVisible = !infoVisible;
    if (infoVisible) {
      RefreshInfoOverlay(true);
    }
    SetInfoOverlayVisible(infoVisible);
    return true;
  }
  return false;
}

void WatchFacePolygons::OnDisplaySleep() {
  isSleeping = true;
  if (infoVisible) {
    infoVisible = false;
    SetInfoOverlayVisible(false);
  }
}

void WatchFacePolygons::OnDisplayWake() {
  isSleeping = false;
}

void WatchFacePolygons::SetInfoOverlayVisible(bool visible) {
  lv_obj_set_hidden(topPanel, !visible);
  lv_obj_set_hidden(bottomPanel, !visible);
}

void WatchFacePolygons::StepShapes() {
  prevQuadArea = currQuadArea;
  prevHexArea = currHexArea;
  prevTriArea = currTriArea;

  // Advance and bounce Red irregular quadrilateral
  quadX += quadVx;
  quadY += quadVy;
  quadAngle = static_cast<uint16_t>((quadAngle + 1) % 360);

  const int32_t qCx = quadX >> 4;
  const int32_t qCy = quadY >> 4;
  int16_t qMinX = 1000;
  int16_t qMaxX = -1000;
  int16_t qMinY = 1000;
  int16_t qMaxY = -1000;

  for (int i = 0; i < 4; ++i) {
    const int16_t ang = static_cast<int16_t>((quadBaseAngles[i] + quadAngle) % 360);
    const int32_t r = quadBaseRadii[i];
    const int16_t px = static_cast<int16_t>(qCx + (r * static_cast<int32_t>(Cosine(ang))) / LV_TRIG_SCALE);
    const int16_t py = static_cast<int16_t>(qCy + (r * static_cast<int32_t>(Sine(ang))) / LV_TRIG_SCALE);
    quadPts[i] = {px, py};
    qMinX = std::min(qMinX, px);
    qMaxX = std::max(qMaxX, px);
    qMinY = std::min(qMinY, py);
    qMaxY = std::max(qMaxY, py);
  }
  quadMinY = qMinY;
  quadMaxY = qMaxY;
  currQuadArea = ClampPaddedArea(qMinX, qMinY, qMaxX, qMaxY);

  if ((qMinX <= 0 && quadVx < 0) || (qMaxX >= 239 && quadVx > 0)) {
    quadVx = -quadVx;
  }
  if ((qMinY <= 0 && quadVy < 0) || (qMaxY >= 239 && quadVy > 0)) {
    quadVy = -quadVy;
  }

  // Advance and bounce Green irregular hexagon
  hexX += hexVx;
  hexY += hexVy;
  hexAngle = static_cast<uint16_t>((hexAngle + 359) % 360);

  const int32_t hCx = hexX >> 4;
  const int32_t hCy = hexY >> 4;
  int16_t hMinX = 1000;
  int16_t hMaxX = -1000;
  int16_t hMinY = 1000;
  int16_t hMaxY = -1000;

  for (int i = 0; i < 6; ++i) {
    const int16_t ang = static_cast<int16_t>((hexBaseAngles[i] + hexAngle) % 360);
    const int32_t r = hexBaseRadii[i];
    const int16_t px = static_cast<int16_t>(hCx + (r * static_cast<int32_t>(Cosine(ang))) / LV_TRIG_SCALE);
    const int16_t py = static_cast<int16_t>(hCy + (r * static_cast<int32_t>(Sine(ang))) / LV_TRIG_SCALE);
    hexPts[i] = {px, py};
    hMinX = std::min(hMinX, px);
    hMaxX = std::max(hMaxX, px);
    hMinY = std::min(hMinY, py);
    hMaxY = std::max(hMaxY, py);
  }
  hexMinY = hMinY;
  hexMaxY = hMaxY;
  currHexArea = ClampPaddedArea(hMinX, hMinY, hMaxX, hMaxY);

  if ((hMinX <= 0 && hexVx < 0) || (hMaxX >= 239 && hexVx > 0)) {
    hexVx = -hexVx;
  }
  if ((hMinY <= 0 && hexVy < 0) || (hMaxY >= 239 && hexVy > 0)) {
    hexVy = -hexVy;
  }

  // Advance and bounce Blue irregular triangle
  triX += triVx;
  triY += triVy;
  triAngle = static_cast<uint16_t>((triAngle + 2) % 360);

  const int32_t tCx = triX >> 4;
  const int32_t tCy = triY >> 4;
  int16_t tMinX = 1000;
  int16_t tMaxX = -1000;
  int16_t tMinY = 1000;
  int16_t tMaxY = -1000;

  for (int i = 0; i < 3; ++i) {
    const int16_t ang = static_cast<int16_t>((triBaseAngles[i] + triAngle) % 360);
    const int32_t r = triBaseRadii[i];
    const int16_t px = static_cast<int16_t>(tCx + (r * static_cast<int32_t>(Cosine(ang))) / LV_TRIG_SCALE);
    const int16_t py = static_cast<int16_t>(tCy + (r * static_cast<int32_t>(Sine(ang))) / LV_TRIG_SCALE);
    triPts[i] = {px, py};
    tMinX = std::min(tMinX, px);
    tMaxX = std::max(tMaxX, px);
    tMinY = std::min(tMinY, py);
    tMaxY = std::max(tMaxY, py);
  }
  triMinY = tMinY;
  triMaxY = tMaxY;
  currTriArea = ClampPaddedArea(tMinX, tMinY, tMaxX, tMaxY);

  if ((tMinX <= 0 && triVx < 0) || (tMaxX >= 239 && triVx > 0)) {
    triVx = -triVx;
  }
  if ((tMinY <= 0 && triVy < 0) || (tMaxY >= 239 && triVy > 0)) {
    triVy = -triVy;
  }
}

void WatchFacePolygons::InvalidateMovingPolygons() {
  if (!hasPrevBounds) {
    lv_obj_invalidate(bgObject);
    hasPrevBounds = true;
    return;
  }

  auto invalidateUnion = [this](const lv_area_t& prevA, const lv_area_t& currA) {
    lv_area_t dirty;
    _lv_area_join(&dirty, &prevA, &currA);
    lv_obj_invalidate_area(bgObject, &dirty);
  };

  invalidateUnion(prevQuadArea, currQuadArea);
  invalidateUnion(prevHexArea, currHexArea);
  invalidateUnion(prevTriArea, currTriArea);
}

void WatchFacePolygons::ClearMaskBuffer(lv_obj_t* obj, const lv_area_t* clipArea) {
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_buf_t* vdb = lv_disp_get_buf(disp);
  const lv_area_t* dispArea = &vdb->area;
  auto* dispBuf = static_cast<lv_color_t*>(vdb->buf_act);

  lv_area_t drawArea;
  if (!_lv_area_intersect(&drawArea, clipArea, &obj->coords)) {
    return;
  }

  const int32_t dispW = lv_area_get_width(dispArea);
  const int32_t drawW = drawArea.x2 - drawArea.x1 + 1;
  if (drawW == dispW) {
    lv_color_t* start = dispBuf + (drawArea.y1 - dispArea->y1) * dispW;
    const int32_t drawH = drawArea.y2 - drawArea.y1 + 1;
    std::memset(start, 0, static_cast<std::size_t>(drawW * drawH) * sizeof(lv_color_t));
  } else {
    for (int32_t y = drawArea.y1; y <= drawArea.y2; ++y) {
      lv_color_t* row = dispBuf + (y - dispArea->y1) * dispW + (drawArea.x1 - dispArea->x1);
      std::memset(row, 0, static_cast<std::size_t>(drawW) * sizeof(lv_color_t));
    }
  }
}

void WatchFacePolygons::DrawColorSplit(lv_obj_t* obj, const lv_area_t* clipArea) {
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_buf_t* vdb = lv_disp_get_buf(disp);
  const lv_area_t* dispArea = &vdb->area;
  auto* dispBuf = static_cast<lv_color_t*>(vdb->buf_act);

  lv_area_t drawArea;
  if (!_lv_area_intersect(&drawArea, clipArea, &obj->coords)) {
    return;
  }

  const int32_t dispW = lv_area_get_width(dispArea);
  const int32_t drawW = drawArea.x2 - drawArea.x1 + 1;

  for (int32_t y = drawArea.y1; y <= drawArea.y2; ++y) {
    lv_color_t* row = dispBuf + (y - dispArea->y1) * dispW + (drawArea.x1 - dispArea->x1);

    int32_t rMin = 1000;
    int32_t rMax = -1000;
    if (y >= quadMinY && y <= quadMaxY) {
      GetConvexPolySpan(quadPts, y, rMin, rMax);
    }

    int32_t gMin = 1000;
    int32_t gMax = -1000;
    if (y >= hexMinY && y <= hexMaxY) {
      GetConvexPolySpan(hexPts, y, gMin, gMax);
    }

    int32_t bMin = 1000;
    int32_t bMax = -1000;
    if (y >= triMinY && y <= triMaxY) {
      GetConvexPolySpan(triPts, y, bMin, bMax);
    }

    const bool hasRed = (rMax >= drawArea.x1 && rMin <= drawArea.x2);
    const bool hasGreen = (gMax >= drawArea.x1 && gMin <= drawArea.x2);
    const bool hasBlue = (bMax >= drawArea.x1 && bMin <= drawArea.x2);

    if (!hasRed && !hasGreen && !hasBlue) {
      lv_color_t* const rowEnd = row + drawW;
      for (; row < rowEnd; ++row) {
        *row = (row->full != 0) ? colorWhite : colorBlack;
      }
      continue;
    }

    for (int32_t x = drawArea.x1; x <= drawArea.x2; ++x) {
      const bool isDigit = (row->full != 0);
      const uint32_t inRed = (x >= rMin && x <= rMax) ? 1U : 0U;
      const uint32_t inGreen = (x >= gMin && x <= gMax) ? 2U : 0U;
      const uint32_t inBlue = (x >= bMin && x <= bMax) ? 4U : 0U;

      const uint32_t state = inRed | inGreen | inBlue;
      *row = isDigit ? digitPalette[state] : bgPalette[state];
      ++row;
    }
  }
}

void WatchFacePolygons::RefreshInfoOverlay(bool forceUpdate) {
  notificationState = notificationManager.AreNewNotificationsAvailable();
  if (notificationState.IsUpdated() || forceUpdate) {
    lv_label_set_text_static(notificationIcon, NotificationIcon::GetIcon(notificationState.Get()));
  }

  alarmEnabled = alarmController.IsEnabled();
  timerRunning = timerController.IsRunning();
  stopWatchRunning = stopWatchController.IsRunning();
  pomodoroRunning = pomodoroController.IsRunning();
  if (alarmEnabled.IsUpdated() || timerRunning.IsUpdated() || stopWatchRunning.IsUpdated() || pomodoroRunning.IsUpdated() || forceUpdate) {
    lv_label_set_text_fmt(servicesIcons,
                          "%s%s%s%s%s%s%s",
                          pomodoroRunning.Get() ? Symbols::pomodoro : "",
                          pomodoroRunning.Get() ? " " : "",
                          stopWatchRunning.Get() ? Symbols::stopWatch : "",
                          stopWatchRunning.Get() ? " " : "",
                          alarmEnabled.Get() ? Symbols::bell : "",
                          alarmEnabled.Get() ? " " : "",
                          timerRunning.Get() ? Symbols::hourGlass : "");
  }

  bleState = bleController.IsConnected();
  if (bleState.IsUpdated() || forceUpdate) {
    lv_label_set_text_static(bleIcon, BleIcon::GetIcon(bleState.Get()));
  }

  batteryPercentRemaining = batteryController.PercentRemaining();
  powerPresent = batteryController.IsPowerPresent();
  if (batteryPercentRemaining.IsUpdated() || powerPresent.IsUpdated() || forceUpdate) {
    const uint8_t remaining = batteryPercentRemaining.Get();
    lv_label_set_text_fmt(batteryValue, "%d%%", remaining);
    lv_obj_set_style_local_text_color(batteryValue,
                                      LV_LABEL_PART_MAIN,
                                      LV_STATE_DEFAULT,
                                      powerPresent.Get() ? colorGreen : (remaining <= 20 ? colorRed : colorWhite));
    lv_obj_align(bleIcon, batteryValue, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  }

  currentDate = std::chrono::time_point_cast<std::chrono::days>(currentDateTime.Get());
  if (currentDate.IsUpdated() || forceUpdate) {
    time_t ttTime =
      std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(currentDateTime.Get()));
    tm* tmTime = std::localtime(&ttTime);
    char wkBuf[8] = "01";
    if (tmTime != nullptr) {
      const char* fmt = (settingsController.GetClockType() == Controllers::Settings::ClockType::H24) ? "%V" : "%U";
      strftime(wkBuf, sizeof(wkBuf), fmt, tmTime);
    }
    const int weekNumber = std::atoi(wkBuf);
    lv_label_set_text_fmt(labelDateWeek,
                          "%s %02d %s WK%02d",
                          dateTimeController.DayOfWeekShortToString(),
                          dateTimeController.Day(),
                          dateTimeController.MonthShortToString(),
                          weekNumber);
  }

  stepCount = motionController.NbSteps();
  if (stepCount.IsUpdated() || forceUpdate) {
    lv_label_set_text_fmt(stepValue, "%lu", stepCount.Get());
  }

  heartbeat = heartRateController.HeartRate();
  heartbeatRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
  if (heartbeat.IsUpdated() || heartbeatRunning.IsUpdated() || forceUpdate) {
    if (heartbeatRunning.Get()) {
      lv_label_set_text_fmt(heartbeatValue, "%d", heartbeat.Get());
    } else {
      lv_label_set_text_static(heartbeatValue, "--");
    }
  }

  currentWeather = weatherService.Current();
  if (currentWeather.IsUpdated() || forceUpdate) {
    auto optCurrentWeather = currentWeather.Get();
    if (optCurrentWeather) {
      const bool isImperial = (settingsController.GetWeatherFormat() == Controllers::Settings::WeatherFormat::Imperial);
      const int16_t temp = isImperial ? optCurrentWeather->temperature.Fahrenheit() : optCurrentWeather->temperature.Celsius();
      if (temp > -100) {
        lv_label_set_text_fmt(weatherValue, "%d\xC2\xB0", temp);
      } else {
        lv_label_set_text_static(weatherValue, "--");
      }
      lv_label_set_text_static(weatherIcon, Symbols::GetSymbol(optCurrentWeather->iconId, weatherService.IsNight()));
      lv_obj_align(weatherIcon, weatherValue, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    } else {
      lv_label_set_text_static(weatherValue, "--");
      lv_label_set_text_static(weatherIcon, "");
    }
  }
}

void WatchFacePolygons::Refresh() {
  if (!isSleeping) {
    StepShapes();
    InvalidateMovingPolygons();
  }

  currentDateTime = std::chrono::time_point_cast<std::chrono::minutes>(dateTimeController.CurrentDateTime());
  if (currentDateTime.IsUpdated()) {
    uint8_t hour = dateTimeController.Hours();
    const uint8_t minute = dateTimeController.Minutes();

    if (settingsController.GetClockType() == Controllers::Settings::ClockType::H12) {
      if (hour == 0) {
        hour = 12;
      } else if (hour > 12) {
        hour = static_cast<uint8_t>(hour - 12);
      }
    }

    lv_label_set_text_fmt(labelHour, "%02d", hour);
    lv_label_set_text_fmt(labelMinute, "%02d", minute);
  }

  if (infoVisible) {
    RefreshInfoOverlay(false);
  }
}
