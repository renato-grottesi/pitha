#pragma once

#include <FreeRTOS.h>
#include <task.h>
#include <lvgl/lvgl.h>
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

      class WatchFacePolygons : public Screen {
      public:
        WatchFacePolygons(Controllers::DateTime& dateTimeController,
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
        ~WatchFacePolygons() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        void OnDisplaySleep() override;
        void OnDisplayWake() override;

        void ClearMaskBuffer(lv_obj_t* obj, const lv_area_t* clipArea);
        void DrawColorSplit(lv_obj_t* obj, const lv_area_t* clipArea);

      private:
        void StepShapes();
        void InvalidateMovingPolygons();
        void SetInfoOverlayVisible(bool visible);
        void RefreshInfoOverlay(bool forceUpdate);

        // Center positions & velocities in Q4 fixed-point (16 = 1px)
        // Start in 3 distinct regions so shapes don't spend too much time overlapping
        int32_t quadX = 58 * 16;
        int32_t quadY = 60 * 16;
        int32_t quadVx = 46;
        int32_t quadVy = 32;
        uint16_t quadAngle = 15;

        int32_t hexX = 182 * 16;
        int32_t hexY = 178 * 16;
        int32_t hexVx = -38;
        int32_t hexVy = -49;
        uint16_t hexAngle = 40;

        int32_t triX = 178 * 16;
        int32_t triY = 64 * 16;
        int32_t triVx = -44;
        int32_t triVy = 31;
        uint16_t triAngle = 85;

        lv_point_t quadPts[4] {};
        lv_point_t hexPts[6] {};
        lv_point_t triPts[3] {};
        int16_t quadMinY = 0;
        int16_t quadMaxY = 239;
        int16_t hexMinY = 0;
        int16_t hexMaxY = 239;
        int16_t triMinY = 0;
        int16_t triMaxY = 239;

        lv_area_t prevQuadArea {};
        lv_area_t prevHexArea {};
        lv_area_t prevTriArea {};
        lv_area_t currQuadArea {};
        lv_area_t currHexArea {};
        lv_area_t currTriArea {};
        bool hasPrevBounds = false;

        bool infoVisible = false;
        bool isSleeping = false;
        TickType_t lastTapTick = 0;

        lv_obj_t* bgObject;
        lv_obj_t* labelHour;
        lv_obj_t* labelMinute;

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
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::minutes>> currentDateTime {};
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
    struct WatchFaceTraits<WatchFace::Polygons> {
      static constexpr WatchFace watchFace = WatchFace::Polygons;
      static constexpr const char* name = "Polygons";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFacePolygons(controllers.dateTimeController,
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
