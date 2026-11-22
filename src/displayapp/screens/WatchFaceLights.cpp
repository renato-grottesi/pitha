#include "displayapp/screens/WatchFaceLights.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
  constexpr int16_t HourLength = 68;
  constexpr int16_t MinuteLength = 94;
  constexpr int16_t SecondLength = 108;
  constexpr int16_t SecondTailLength = -22;
  constexpr int16_t LV_TRIG_SCALE = std::numeric_limits<int16_t>::max();

  constexpr lv_color_t colorWhite = LV_COLOR_MAKE(0xF8, 0xF8, 0xF5);
  constexpr lv_color_t colorGold = LV_COLOR_MAKE(0xFF, 0xCD, 0x00);
  constexpr lv_color_t colorRed = LV_COLOR_MAKE(0xE3, 0x1B, 0x23);
  constexpr lv_color_t colorGreen = LV_COLOR_MAKE(0x00, 0xB8, 0x50);
  constexpr lv_color_t colorBlue = LV_COLOR_MAKE(0x37, 0x8A, 0xDD);
  constexpr lv_color_t colorMuted = LV_COLOR_MAKE(0xB8, 0xB8, 0xB2);
  constexpr lv_color_t colorTickMajor = LV_COLOR_MAKE(0x95, 0x95, 0x92);
  constexpr lv_color_t colorTickMinor = LV_COLOR_MAKE(0x6E, 0x6E, 0x6A);

  int16_t Cosine(int16_t angle) {
    return _lv_trigo_sin(angle + 90);
  }

  int16_t Sine(int16_t angle) {
    return _lv_trigo_sin(angle);
  }

  int16_t CoordinateXRelocate(int16_t x) {
    return static_cast<int16_t>(x + LV_HOR_RES / 2);
  }

  int16_t CoordinateYRelocate(int16_t y) {
    return static_cast<int16_t>(std::abs(y - LV_HOR_RES / 2));
  }

  lv_point_t CoordinateRelocate(int16_t radius, int16_t angle) {
    return lv_point_t {.x = CoordinateXRelocate(static_cast<int16_t>(radius * static_cast<int32_t>(Sine(angle)) / LV_TRIG_SCALE)),
                       .y = CoordinateYRelocate(static_cast<int16_t>(radius * static_cast<int32_t>(Cosine(angle)) / LV_TRIG_SCALE))};
  }

  int32_t IntSqrt(int32_t val) {
    if (val <= 0) {
      return 1;
    }
    int32_t res = 0;
    int32_t bit = 1 << 14;
    while (bit > val) {
      bit >>= 2;
    }
    while (bit != 0) {
      if (val >= res + bit) {
        val -= res + bit;
        res = (res >> 1) + bit;
      } else {
        res >>= 1;
      }
      bit >>= 2;
    }
    return res > 0 ? res : 1;
  }

  // Precomputes a 2D ray-traced shadow wedge cast by segment AB from point light L(lx, ly).
  // All line normals are scaled by 256 (Q8 fixed-point pixel distance).
  WatchFaceLights::ShadowWedge BuildShadowWedge(int32_t lx, int32_t ly, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    WatchFaceLights::ShadowWedge w {};
    int32_t uAx = ax - lx;
    int32_t uAy = ay - ly;
    int32_t uBx = bx - lx;
    int32_t uBy = by - ly;
    int32_t cross = uAx * uBy - uAy * uBx;

    if (std::abs(cross) < 12) {
      w.valid = false;
      return w;
    }

    if (cross < 0) {
      std::swap(ax, bx);
      std::swap(ay, by);
      std::swap(uAx, uBx);
      std::swap(uAy, uBy);
      cross = -cross;
    }

    const int32_t vx = bx - ax;
    const int32_t vy = by - ay;
    const int32_t lenAB = IntSqrt(vx * vx + vy * vy);
    w.nxAB = (vy * 256) / lenAB;
    w.nyAB = (-vx * 256) / lenAB;
    w.cAB = -(w.nxAB * ax + w.nyAB * ay);

    const int32_t lenA = IntSqrt(uAx * uAx + uAy * uAy);
    w.nxA = (-uAy * 256) / lenA;
    w.nyA = (uAx * 256) / lenA;
    w.cA = -(w.nxA * lx + w.nyA * ly);

    const int32_t lenB = IntSqrt(uBx * uBx + uBy * uBy);
    w.nxB = (uBy * 256) / lenB;
    w.nyB = (-uBx * 256) / lenB;
    w.cB = -(w.nxB * lx + w.nyB * ly);

    w.valid = true;
    return w;
  }

  // Returns light transmission factor in [0..256] (256 = unoccluded, 0 = pitch-black umbra shadow)
  inline int32_t EvalShadowWedge(bool valid, int32_t sAB, int32_t sA, int32_t sB) {
    if (!valid || sAB <= 0) {
      return 256;
    }
    const int32_t edgeDist = std::min(sA, sB);
    if (edgeDist >= 0) {
      return 0;
    }
    const int32_t penumbra = 140 + (sAB >> 4);
    if (edgeDist <= -penumbra) {
      return 256;
    }
    return ((-edgeDist) * 256) / penumbra;
  }

  // Tight needle-shadow transmission for the thin Seconds hand (including its -22px back tail)
  // shadowDistQ2 is in quarter-pixels (4 = 1px) behind the needle on its shadowed side.
  inline int32_t EvalSecondNeedleShadow(int32_t dotS, int32_t shadowDistQ2) {
    if (dotS < SecondTailLength || dotS > SecondLength) {
      return 256;
    }
    // Shadow starts right under the needle (-4 = -1px) and extends 28 quarter-pixels (7px) behind it
    if (shadowDistQ2 < -4 || shadowDistQ2 >= 28) {
      return 256;
    }
    if (shadowDistQ2 <= 12) {
      // Crisp umbra core (0..3px behind the needle)
      return 16;
    }
    // Soft penumbra fade from 3px to 7px behind the needle (240 / 16 == 15)
    return 16 + (shadowDistQ2 - 12) * 15;
  }

  lv_design_res_t BgDesignCallback(lv_obj_t* obj, const lv_area_t* clipArea, lv_design_mode_t mode) {
    if (mode == LV_DESIGN_COVER_CHK) {
      return LV_DESIGN_RES_COVER;
    }
    if (mode == LV_DESIGN_DRAW_MAIN) {
      auto* screen = static_cast<WatchFaceLights*>(obj->user_data);
      if (screen != nullptr) {
        screen->DrawBackground(obj, clipArea);
      }
    }
    return LV_DESIGN_RES_OK;
  }
}

WatchFaceLights::WatchFaceLights(Controllers::DateTime& dateTimeController,
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

  for (int i = 0; i < 60; ++i) {
    const int16_t angle = static_cast<int16_t>(i * 6);
    const int32_t sx = Sine(angle);
    const int32_t cy = -Cosine(angle);
    const int32_t maxComp = std::max(std::abs(sx), std::abs(cy));
    if (maxComp == 0) {
      continue;
    }
    const int32_t xOut = std::clamp<int32_t>(120 + (sx * 117) / maxComp, 0, 239);
    const int32_t yOut = std::clamp<int32_t>(120 + (cy * 117) / maxComp, 0, 239);
    const bool isMajor = (i % 5) == 0;
    const int32_t tickLen = isMajor ? 13 : 6;
    const int32_t xIn = std::clamp<int32_t>(xOut - (sx * tickLen) / LV_TRIG_SCALE, 0, 239);
    const int32_t yIn = std::clamp<int32_t>(yOut - (cy * tickLen) / LV_TRIG_SCALE, 0, 239);
    dialTicks[i] = {static_cast<uint8_t>(xIn), static_cast<uint8_t>(yIn), static_cast<uint8_t>(xOut), static_cast<uint8_t>(yOut)};
  }

  bgObject = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(bgObject, 240, 240);
  lv_obj_set_pos(bgObject, 0, 0);
  lv_obj_set_click(bgObject, false);
  bgObject->user_data = this;
  lv_obj_set_design_cb(bgObject, BgDesignCallback);

  // Analog hands: black paddle (*Body) and black inner stem (*BodyTrace), with black center circle
  minuteBody = lv_line_create(lv_scr_act(), nullptr);
  minuteBodyTrace = lv_line_create(lv_scr_act(), nullptr);
  hourBody = lv_line_create(lv_scr_act(), nullptr);
  hourBodyTrace = lv_line_create(lv_scr_act(), nullptr);
  secondBody = lv_line_create(lv_scr_act(), nullptr);

  lv_style_init(&minuteLineStyle);
  lv_style_set_line_width(&minuteLineStyle, LV_STATE_DEFAULT, 7);
  lv_style_set_line_color(&minuteLineStyle, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_style_set_line_rounded(&minuteLineStyle, LV_STATE_DEFAULT, true);
  lv_obj_add_style(minuteBody, LV_LINE_PART_MAIN, &minuteLineStyle);

  lv_style_init(&minuteLineStyleTrace);
  lv_style_set_line_width(&minuteLineStyleTrace, LV_STATE_DEFAULT, 3);
  lv_style_set_line_color(&minuteLineStyleTrace, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_style_set_line_rounded(&minuteLineStyleTrace, LV_STATE_DEFAULT, false);
  lv_obj_add_style(minuteBodyTrace, LV_LINE_PART_MAIN, &minuteLineStyleTrace);

  lv_style_init(&hourLineStyle);
  lv_style_set_line_width(&hourLineStyle, LV_STATE_DEFAULT, 7);
  lv_style_set_line_color(&hourLineStyle, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_style_set_line_rounded(&hourLineStyle, LV_STATE_DEFAULT, true);
  lv_obj_add_style(hourBody, LV_LINE_PART_MAIN, &hourLineStyle);

  lv_style_init(&hourLineStyleTrace);
  lv_style_set_line_width(&hourLineStyleTrace, LV_STATE_DEFAULT, 3);
  lv_style_set_line_color(&hourLineStyleTrace, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_style_set_line_rounded(&hourLineStyleTrace, LV_STATE_DEFAULT, false);
  lv_obj_add_style(hourBodyTrace, LV_LINE_PART_MAIN, &hourLineStyleTrace);

  lv_style_init(&secondLineStyle);
  lv_style_set_line_width(&secondLineStyle, LV_STATE_DEFAULT, 3);
  lv_style_set_line_color(&secondLineStyle, LV_STATE_DEFAULT, colorGold);
  lv_style_set_line_rounded(&secondLineStyle, LV_STATE_DEFAULT, true);
  lv_obj_add_style(secondBody, LV_LINE_PART_MAIN, &secondLineStyle);

  centerDot = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(centerDot, 10, 10);
  lv_obj_align(centerDot, nullptr, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_local_radius(centerDot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_bg_color(centerDot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_border_color(centerDot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, colorGold);
  lv_obj_set_style_local_border_width(centerDot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);

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
  lv_obj_set_style_local_text_color(notificationIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorGold);
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
  lv_obj_set_style_local_text_color(labelDateWeek, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorGold);
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
  lv_obj_set_style_local_text_color(weatherIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, colorGold);
  lv_obj_set_style_local_text_font(weatherIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &fontawesome_weathericons);
  lv_label_set_text_static(weatherIcon, "");
  lv_obj_align(weatherIcon, weatherValue, LV_ALIGN_OUT_LEFT_MID, -4, 0);
  lv_obj_set_auto_realign(weatherIcon, true);

  SetInfoOverlayVisible(false);

  Refresh();
  taskRefresh = lv_task_create(RefreshTaskCallback, 50, LV_TASK_PRIO_LOW, this);
}

WatchFaceLights::~WatchFaceLights() {
  lv_task_del(taskRefresh);

  lv_style_reset(&hourLineStyle);
  lv_style_reset(&hourLineStyleTrace);
  lv_style_reset(&minuteLineStyle);
  lv_style_reset(&minuteLineStyleTrace);
  lv_style_reset(&secondLineStyle);

  lv_obj_clean(lv_scr_act());
}

bool WatchFaceLights::OnTouchEvent(TouchEvents event) {
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

void WatchFaceLights::OnDisplaySleep() {
  if (infoVisible) {
    infoVisible = false;
    SetInfoOverlayVisible(false);
  }
}

void WatchFaceLights::SetInfoOverlayVisible(bool visible) {
  lv_obj_set_hidden(topPanel, !visible);
  lv_obj_set_hidden(bottomPanel, !visible);
}

void WatchFaceLights::DrawBackground(lv_obj_t* obj, const lv_area_t* clipArea) {
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_buf_t* vdb = lv_disp_get_buf(disp);
  const lv_area_t* dispArea = &vdb->area;
  auto* dispBuf = static_cast<lv_color_t*>(vdb->buf_act);

  lv_area_t drawArea;
  if (_lv_area_intersect(&drawArea, clipArea, &obj->coords)) {
    const int32_t dispW = lv_area_get_width(dispArea);
    const int32_t hx = hourDirX;
    const int32_t hy = hourDirY;
    const int32_t mx = minDirX;
    const int32_t my = minDirY;
    const int32_t sx = secDirX;
    const int32_t sy = secDirY;

    const ShadowWedge wMinR = wedgeMinForRed;
    const ShadowWedge wHrG = wedgeHourForGreen;
    const int32_t xStart = drawArea.x1;
    const int32_t dxStart = xStart - 120;

    for (int32_t y = drawArea.y1; y <= drawArea.y2; ++y) {
      lv_color_t* row = dispBuf + (y - dispArea->y1) * dispW + (xStart - dispArea->x1);
      const int32_t dy = y - 120;
      const int32_t dy2 = dy * dy;

      // Incremental scanline state for the Hour/Minute cross-arm shadow wedges
      int32_t sMinR_AB = wMinR.nxAB * xStart + wMinR.nyAB * y + wMinR.cAB;
      int32_t sMinR_A = wMinR.nxA * xStart + wMinR.nyA * y + wMinR.cA;
      int32_t sMinR_B = wMinR.nxB * xStart + wMinR.nyB * y + wMinR.cB;

      int32_t sHrG_AB = wHrG.nxAB * xStart + wHrG.nyAB * y + wHrG.cAB;
      int32_t sHrG_A = wHrG.nxA * xStart + wHrG.nyA * y + wHrG.cA;
      int32_t sHrG_B = wHrG.nxB * xStart + wHrG.nyB * y + wHrG.cB;

      // Incremental rotated coordinates along the scanline
      int32_t dotS_acc = dxStart * sx + dy * sy;
      int32_t perpS_acc = dxStart * (-sy) + dy * sx;
      int32_t perpH_acc = dxStart * (-hy) + dy * hx;
      int32_t dotH_acc = dxStart * hx + dy * hy;
      int32_t perpM_acc = dxStart * my - dy * mx;
      int32_t dotM_acc = dxStart * mx + dy * my;

      for (int32_t x = xStart; x <= drawArea.x2; ++x) {
        const int32_t dx = x - 120;
        const int32_t r2 = dx * dx + dy2;

        // Radial distance attenuation (25% steeper falloff): 256 at center, fading strongly toward borders and corners
        const int32_t radialAtten = 256 - ((std::min<int32_t>(19800, r2) * 729) >> 16);

        // Seconds hand local coordinates (dotS in pixels along arm, perpSQ2 in quarter-pixels perpendicular)
        const int32_t dotS = dotS_acc >> 10;
        const int32_t perpSQ2 = perpS_acc >> 8;

        // Local coordinates for Hour hand (perpH > 0 is clockwise emitting side)
        const int32_t perpH = perpH_acc >> 10;
        const int32_t dotH = dotH_acc >> 10;

        // Local coordinates for Minute hand (perpM > 0 is counter-clockwise emitting side)
        const int32_t perpM = perpM_acc >> 10;
        const int32_t dotM = dotM_acc >> 10;

        // ---- 1. Forward-only Red light emitted from the clockwise side of the Hour arm (dotH > 0, perpH > 0) ----
        int32_t r = 0;
        int32_t g = 0;
        int32_t b = 0;

        if (perpH > 0 && dotH > 0) {
          const int32_t armHalfW = (dotH < 28) ? 1 : 3;
          const int32_t perpEdgeH = std::max<int32_t>(0, perpH - armHalfW);
          int32_t dParH = 0;
          if (dotH < 5) {
            dParH = 5 - dotH;
          } else if (dotH > HourLength) {
            dParH = dotH - HourLength;
          }

          // Smooth quadratic distance from arm segment + radial distance from center (no octagon kinks)
          const int32_t dArm2H = perpEdgeH * perpEdgeH + dParH * dParH;
          const int32_t effD2H = ((dArm2H * 2 + r2) * 10923) >> 15;

          if (effD2H < 11700) {
            int32_t intensity = ((11700 - effD2H) * 1429) >> 16;
            intensity = (intensity * radialAtten) >> 8;

            // Smoothly taper to 0 as dotH -> 0 so no light is emitted toward the back of the arm (dotH <= 0)
            intensity = (intensity * dotH) / (dotH + (perpH >> 2) + 2);

            // Occlusion from Minute hand wedge + non-emitting back of Minute hand + Seconds needle
            int32_t attenMin = EvalShadowWedge(wMinR.valid, sMinR_AB, sMinR_A, sMinR_B);
            if (perpM <= 0 && perpM >= -8 && dotM >= 0 && dotM <= MinuteLength) {
              attenMin = 0;
            }
            const int32_t attenSec = EvalSecondNeedleShadow(dotS, perpSQ2 * secRedShadowSign);
            const int32_t atten = std::min(attenMin, attenSec);
            intensity = (intensity * atten) >> 8;
            r += intensity;

            // Exponentially decaying white-hot core immediately adjacent to the emitting Hour arm edge
            if (dArm2H < 80 && atten > 0) {
              int32_t whiteH = (255 * 4 * (80 - dArm2H)) / ((4 + dArm2H) * 80);
              whiteH = (whiteH * dotH) / (dotH + 2);
              whiteH = (whiteH * atten) >> 8;
              r += whiteH;
              g += whiteH;
              b += whiteH;
            }
          }
        }

        // ---- 2. Forward-only Green light emitted from the counter-clockwise side of the Minute arm (dotM > 0, perpM > 0) ----
        if (perpM > 0 && dotM > 0) {
          const int32_t armHalfW = (dotM < 28) ? 1 : 3;
          const int32_t perpEdgeM = std::max<int32_t>(0, perpM - armHalfW);
          int32_t dParM = 0;
          if (dotM < 5) {
            dParM = 5 - dotM;
          } else if (dotM > MinuteLength) {
            dParM = dotM - MinuteLength;
          }

          // Smooth quadratic distance from arm segment + radial distance from center (no octagon kinks)
          const int32_t dArm2M = perpEdgeM * perpEdgeM + dParM * dParM;
          const int32_t effD2M = ((dArm2M * 2 + r2) * 10923) >> 15;

          if (effD2M < 11700) {
            int32_t intensity = ((11700 - effD2M) * 1317) >> 16;
            intensity = (intensity * radialAtten) >> 8;

            // Smoothly taper to 0 as dotM -> 0 so no light is emitted toward the back of the arm (dotM <= 0)
            intensity = (intensity * dotM) / (dotM + (perpM >> 2) + 2);

            // Occlusion from Hour hand wedge + non-emitting back of Hour hand + Seconds needle
            int32_t attenHr = EvalShadowWedge(wHrG.valid, sHrG_AB, sHrG_A, sHrG_B);
            if (perpH <= 0 && perpH >= -8 && dotH >= 0 && dotH <= HourLength) {
              attenHr = 0;
            }
            const int32_t attenSec = EvalSecondNeedleShadow(dotS, perpSQ2 * secGreenShadowSign);
            const int32_t atten = std::min(attenHr, attenSec);
            intensity = (intensity * atten) >> 8;
            g += intensity;

            // Exponentially decaying white-hot core immediately adjacent to the emitting Minute arm edge
            if (dArm2M < 80 && atten > 0) {
              int32_t whiteM = (255 * 4 * (80 - dArm2M)) / ((4 + dArm2M) * 80);
              whiteM = (whiteM * dotM) / (dotM + 2);
              whiteM = (whiteM * atten) >> 8;
              r += whiteM;
              g += whiteM;
              b += whiteM;
            }
          }
        }

        if (r > 255) {
          r = 255;
        }
        if (g > 255) {
          g = 255;
        }
        if (b > 255) {
          b = 255;
        }
        *row++ = LV_COLOR_MAKE(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));

        // Step scanline shadow wedge distances and rotated hand coordinates
        sMinR_AB += wMinR.nxAB;
        sMinR_A += wMinR.nxA;
        sMinR_B += wMinR.nxB;

        sHrG_AB += wHrG.nxAB;
        sHrG_A += wHrG.nxA;
        sHrG_B += wHrG.nxB;

        dotS_acc += sx;
        perpS_acc -= sy;
        perpH_acc -= hy;
        dotH_acc += hx;
        perpM_acc += my;
        dotM_acc += mx;
      }
    }
  }

  // Draw rectangular edge dial tick marks (only those intersecting clipArea)
  lv_draw_line_dsc_t majorTickDsc;
  lv_draw_line_dsc_init(&majorTickDsc);
  majorTickDsc.width = 3;
  majorTickDsc.color = colorTickMajor;
  majorTickDsc.opa = LV_OPA_70;
  majorTickDsc.round_start = 1;
  majorTickDsc.round_end = 1;

  lv_draw_line_dsc_t minorTickDsc;
  lv_draw_line_dsc_init(&minorTickDsc);
  minorTickDsc.width = 1;
  minorTickDsc.color = colorTickMinor;
  minorTickDsc.opa = LV_OPA_50;

  const int16_t cx1 = clipArea->x1 - 2;
  const int16_t cy1 = clipArea->y1 - 2;
  const int16_t cx2 = clipArea->x2 + 2;
  const int16_t cy2 = clipArea->y2 + 2;

  for (int i = 0; i < 60; ++i) {
    const auto& t = dialTicks[i];
    const int16_t minY = std::min(t.y1, t.y2);
    const int16_t maxY = std::max(t.y1, t.y2);
    if (maxY < cy1 || minY > cy2) {
      continue;
    }
    const int16_t minX = std::min(t.x1, t.x2);
    const int16_t maxX = std::max(t.x1, t.x2);
    if (maxX < cx1 || minX > cx2) {
      continue;
    }
    const bool isMajor = (i % 5) == 0;
    lv_point_t p1 {static_cast<lv_coord_t>(t.x1), static_cast<lv_coord_t>(t.y1)};
    lv_point_t p2 {static_cast<lv_coord_t>(t.x2), static_cast<lv_coord_t>(t.y2)};
    lv_draw_line(&p1, &p2, clipArea, isMajor ? &majorTickDsc : &minorTickDsc);
  }
}

void WatchFaceLights::UpdateClock() {
  const uint8_t hour = dateTimeController.Hours();
  const uint8_t minute = dateTimeController.Minutes();
  const uint8_t second = dateTimeController.Seconds();

  bool handsChanged = false;
  bool secondChanged = false;

  if (sMinute != minute) {
    const int16_t minAngle = static_cast<int16_t>(minute * 6);
    minutePoint[0] = CoordinateRelocate(28, minAngle);
    minutePoint[1] = CoordinateRelocate(MinuteLength, minAngle);

    minutePointTrace[0] = CoordinateRelocate(5, minAngle);
    minutePointTrace[1] = CoordinateRelocate(29, minAngle);

    lv_line_set_points(minuteBody, minutePoint, 2);
    lv_line_set_points(minuteBodyTrace, minutePointTrace, 2);

    minDirX = (static_cast<int32_t>(Sine(minAngle)) * 1024) / LV_TRIG_SCALE;
    minDirY = (-static_cast<int32_t>(Cosine(minAngle)) * 1024) / LV_TRIG_SCALE;

    // Green light virtual origin near center pivot, offset to counter-clockwise emitting side
    greenLightX = 120 + ((minDirX * 14 + minDirY * 8) >> 10);
    greenLightY = 120 + ((minDirY * 14 - minDirX * 8) >> 10);
    handsChanged = true;
  }

  if (sHour != hour || sMinute != minute) {
    sHour = hour;
    sMinute = minute;
    const int16_t hourAngle = static_cast<int16_t>((hour % 12) * 30 + minute / 2);

    hourPoint[0] = CoordinateRelocate(28, hourAngle);
    hourPoint[1] = CoordinateRelocate(HourLength, hourAngle);

    hourPointTrace[0] = CoordinateRelocate(5, hourAngle);
    hourPointTrace[1] = CoordinateRelocate(29, hourAngle);

    lv_line_set_points(hourBody, hourPoint, 2);
    lv_line_set_points(hourBodyTrace, hourPointTrace, 2);

    hourDirX = (static_cast<int32_t>(Sine(hourAngle)) * 1024) / LV_TRIG_SCALE;
    hourDirY = (-static_cast<int32_t>(Cosine(hourAngle)) * 1024) / LV_TRIG_SCALE;

    // Red light virtual origin near center pivot, offset to clockwise emitting side
    redLightX = 120 + ((hourDirX * 14 - hourDirY * 8) >> 10);
    redLightY = 120 + ((hourDirY * 14 + hourDirX * 8) >> 10);
    handsChanged = true;
  }

  if (sSecond != second) {
    sSecond = second;
    const int16_t secAngle = static_cast<int16_t>(second * 6);

    secondPoint[0] = CoordinateRelocate(SecondTailLength, secAngle);
    secondPoint[1] = CoordinateRelocate(SecondLength, secAngle);
    lv_line_set_points(secondBody, secondPoint, 2);

    secDirX = (static_cast<int32_t>(Sine(secAngle)) * 1024) / LV_TRIG_SCALE;
    secDirY = (-static_cast<int32_t>(Cosine(secAngle)) * 1024) / LV_TRIG_SCALE;
    secondChanged = true;
  }

  if (handsChanged || secondChanged) {
    if (handsChanged) {
      // Recompute 2D ray-traced shadow wedges for Hour and Minute cross-occlusion
      // Extend radial barrier endpoints so light never wraps around the hand tips onto the dark side
      const int32_t hrFarX = 120 + ((hourDirX * 160) >> 10);
      const int32_t hrFarY = 120 + ((hourDirY * 160) >> 10);
      const int32_t minFarX = 120 + ((minDirX * 160) >> 10);
      const int32_t minFarY = 120 + ((minDirY * 160) >> 10);

      wedgeMinForRed = BuildShadowWedge(redLightX, redLightY, 120, 120, minFarX, minFarY);
      wedgeHourForGreen = BuildShadowWedge(greenLightX, greenLightY, 120, 120, hrFarX, hrFarY);
    }

    // Determine which side of the Seconds arm faces away from the Red and Green light emitters
    const int32_t redPerpS = (redLightX - 120) * (-secDirY) + (redLightY - 120) * secDirX;
    secRedShadowSign = (redPerpS <= 0) ? 1 : -1;

    const int32_t greenPerpS = (greenLightX - 120) * (-secDirY) + (greenLightY - 120) * secDirX;
    secGreenShadowSign = (greenPerpS <= 0) ? 1 : -1;

    const lv_area_t currSecondArea {
      .x1 = static_cast<lv_coord_t>(std::clamp<int16_t>(std::min(secondPoint[0].x, secondPoint[1].x) - 10, 0, 239)),
      .y1 = static_cast<lv_coord_t>(std::clamp<int16_t>(std::min(secondPoint[0].y, secondPoint[1].y) - 10, 0, 239)),
      .x2 = static_cast<lv_coord_t>(std::clamp<int16_t>(std::max(secondPoint[0].x, secondPoint[1].x) + 10, 0, 239)),
      .y2 = static_cast<lv_coord_t>(std::clamp<int16_t>(std::max(secondPoint[0].y, secondPoint[1].y) + 10, 0, 239)),
    };

    if (handsChanged || !hasPrevSecondArea) {
      lv_obj_invalidate(bgObject);
    } else {
      lv_obj_invalidate_area(bgObject, &prevSecondArea);
      lv_obj_invalidate_area(bgObject, &currSecondArea);
    }

    prevSecondArea = currSecondArea;
    hasPrevSecondArea = true;
  }
}

void WatchFaceLights::RefreshInfoOverlay(bool forceUpdate) {
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

void WatchFaceLights::Refresh() {
  currentDateTime = std::chrono::time_point_cast<std::chrono::seconds>(dateTimeController.CurrentDateTime());
  if (currentDateTime.IsUpdated()) {
    UpdateClock();
  }

  if (infoVisible) {
    RefreshInfoOverlay(false);
  }
}
