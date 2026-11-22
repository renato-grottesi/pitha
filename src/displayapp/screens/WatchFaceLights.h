#pragma once

#include <FreeRTOS.h>
#include <task.h>
#include <lvgl/lvgl.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include "displayapp/apps/Apps.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/Controllers.h"
#include "components/datetime/DateTimeController.h"
#include "components/ble/SimpleWeatherService.h"
#include "utility/DirtyValue.h"

namespace Pinetime {
  namespace Controllers {
    class Settings;
    class Battery;
    class Ble;
    class NotificationManager;
    class HeartRateController;
    class MotionController;
    class PomodoroController;
  }

  namespace Applications {
    namespace Screens {

      class WatchFaceLights : public Screen {
      public:
        struct ShadowWedge {
          int32_t nxAB = 0;
          int32_t nyAB = 0;
          int32_t cAB = 0;
          int32_t nxA = 0;
          int32_t nyA = 0;
          int32_t cA = 0;
          int32_t nxB = 0;
          int32_t nyB = 0;
          int32_t cB = 0;
          bool valid = false;
        };

        struct DialTick {
          uint8_t x1;
          uint8_t y1;
          uint8_t x2;
          uint8_t y2;
        };

        WatchFaceLights(Controllers::DateTime& dateTimeController,
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
                        Controllers::SimpleWeatherService& weatherService);
        ~WatchFaceLights() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        void OnDisplaySleep() override;

        void DrawBackground(lv_obj_t* obj, const lv_area_t* clipArea);

      private:
        void UpdateClock();
        void SetInfoOverlayVisible(bool visible);
        void RefreshInfoOverlay(bool forceUpdate);

        uint8_t sHour = 99;
        uint8_t sMinute = 99;
        uint8_t sSecond = 99;

        // Unit vectors (scaled by 1024) for the hour (red), minute (green), and second hands
        int32_t hourDirX = 0;
        int32_t hourDirY = -1024;
        int32_t minDirX = 0;
        int32_t minDirY = -1024;
        int32_t secDirX = 0;
        int32_t secDirY = -1024;

        // Which side of the second hand (+1 or -1) is shadowed from the Red and Green lights
        int32_t secRedShadowSign = 1;
        int32_t secGreenShadowSign = 1;

        // Off-center 2D light source coordinates for Red (along Hour hand) and Green (along Minute hand)
        int32_t redLightX = 120;
        int32_t redLightY = 100;
        int32_t greenLightX = 120;
        int32_t greenLightY = 100;

        // 2D ray-traced shadow wedges cast by the Minute and Hour hands
        ShadowWedge wedgeMinForRed {};
        ShadowWedge wedgeHourForGreen {};

        std::array<DialTick, 60> dialTicks {};
        lv_area_t prevSecondArea {};
        bool hasPrevSecondArea = false;

        bool infoVisible = false;
        TickType_t lastTapTick = 0;

        lv_obj_t* bgObject;

        lv_point_t hourPoint[2];
        lv_point_t hourPointTrace[2];
        lv_point_t minutePoint[2];
        lv_point_t minutePointTrace[2];
        lv_point_t secondPoint[2];

        lv_obj_t* hourBody;
        lv_obj_t* hourBodyTrace;
        lv_obj_t* minuteBody;
        lv_obj_t* minuteBodyTrace;
        lv_obj_t* secondBody;
        lv_obj_t* centerDot;

        lv_style_t hourLineStyle;
        lv_style_t hourLineStyleTrace;
        lv_style_t minuteLineStyle;
        lv_style_t minuteLineStyleTrace;
        lv_style_t secondLineStyle;

        // Tap-to-reveal overlay widgets
        lv_obj_t* topPanel;
        lv_obj_t* notificationIcon;
        lv_obj_t* servicesIcons;
        lv_obj_t* bleIcon;
        lv_obj_t* batteryValue;
        lv_obj_t* labelDateWeek;

        lv_obj_t* bottomPanel;
        lv_obj_t* stepIcon;
        lv_obj_t* stepValue;
        lv_obj_t* heartbeatIcon;
        lv_obj_t* heartbeatValue;
        lv_obj_t* weatherIcon;
        lv_obj_t* weatherValue;

        Utility::DirtyValue<uint8_t> batteryPercentRemaining {};
        Utility::DirtyValue<bool> powerPresent {};
        Utility::DirtyValue<bool> bleState {};
        Utility::DirtyValue<bool> alarmEnabled {};
        Utility::DirtyValue<bool> timerRunning {};
        Utility::DirtyValue<bool> stopWatchRunning {};
        Utility::DirtyValue<bool> pomodoroRunning {};
        Utility::DirtyValue<bool> notificationState {};
        Utility::DirtyValue<uint32_t> stepCount {};
        Utility::DirtyValue<uint8_t> heartbeat {};
        Utility::DirtyValue<bool> heartbeatRunning {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>> currentDateTime {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::days>> currentDate {};
        Utility::DirtyValue<std::optional<Controllers::SimpleWeatherService::CurrentWeather>> currentWeather {};

        Controllers::DateTime& dateTimeController;
        const Controllers::Battery& batteryController;
        const Controllers::Ble& bleController;
        const Controllers::AlarmController& alarmController;
        Controllers::Timer& timerController;
        Controllers::StopWatchController& stopWatchController;
        Controllers::PomodoroController& pomodoroController;
        Controllers::NotificationManager& notificationManager;
        Controllers::Settings& settingsController;
        Controllers::HeartRateController& heartRateController;
        Controllers::MotionController& motionController;
        Controllers::SimpleWeatherService& weatherService;

        lv_task_t* taskRefresh;
      };
    }

    template <>
    struct WatchFaceTraits<WatchFace::Lights> {
      static constexpr WatchFace watchFace = WatchFace::Lights;
      static constexpr const char* name = "Lights";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFaceLights(controllers.dateTimeController,
                                            controllers.batteryController,
                                            controllers.bleController,
                                            controllers.alarmController,
                                            controllers.timer,
                                            controllers.stopWatchController,
                                            controllers.pomodoroController,
                                            controllers.notificationManager,
                                            controllers.settingsController,
                                            controllers.heartRateController,
                                            controllers.motionController,
                                            *controllers.weatherController);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
