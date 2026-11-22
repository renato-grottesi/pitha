#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <lvgl/lvgl.h>
#include "displayapp/apps/Apps.h"
#include "Screen.h"
#include "ScreenList.h"
#include "displayapp/Controllers.h"
#include "Symbols.h"
#include "Tile.h"
#include "components/settings/Settings.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      template <size_t N>
      class ApplicationList : public Screen {
      public:
        explicit ApplicationList(DisplayApp* app,
                                 Pinetime::Controllers::Settings& settingsController,
                                 const Pinetime::Controllers::Battery& batteryController,
                                 const Pinetime::Controllers::Ble& bleController,
                                 const Pinetime::Controllers::AlarmController& alarmController,
                                 Controllers::DateTime& dateTimeController,
                                 Pinetime::Controllers::FS& filesystem,
                                 std::array<Tile::Applications, N>&& apps)
          : app {app},
            settingsController {settingsController},
            batteryController {batteryController},
            bleController {bleController},
            alarmController {alarmController},
            dateTimeController {dateTimeController},
            filesystem {filesystem},
            apps {std::move(apps)},
            screens {app,
                     static_cast<uint8_t>(std::min<uint8_t>(settingsController.GetAppMenu(), nScreens - 1)),
                     CreateScreenList(),
                     Screens::ScreenListModes::UpDown} {
        }

        ~ApplicationList() override {
          lv_obj_clean(lv_scr_act());
        }

        bool OnTouchEvent(TouchEvents event) override {
          return screens.OnTouchEvent(event);
        }

      private:
        DisplayApp* app;

        auto CreateScreenList() const {
          std::array<std::function<std::unique_ptr<Screen>()>, nScreens> list;
          for (size_t i = 0; i < list.size(); i++) {
            list[i] = [this, i]() -> std::unique_ptr<Screen> {
              return CreateScreen(i);
            };
          }
          return list;
        }

        std::unique_ptr<Screen> CreateScreen(unsigned int screenNum) const {
          std::array<Tile::Applications, appsPerScreen> pageApps;
          for (int i = 0; i < appsPerScreen; i++) {
            if (i + (screenNum * appsPerScreen) >= apps.size()) {
              pageApps[i] = {"", Pinetime::Applications::Apps::None, false};
            } else {
              pageApps[i] = apps[i + (screenNum * appsPerScreen)];
            }
          }

          return std::make_unique<Screens::Tile>(screenNum,
                                                 nScreens,
                                                 app,
                                                 settingsController,
                                                 batteryController,
                                                 bleController,
                                                 alarmController,
                                                 dateTimeController,
                                                 pageApps);
        }

        Controllers::Settings& settingsController;
        const Pinetime::Controllers::Battery& batteryController;
        const Pinetime::Controllers::Ble& bleController;
        const Pinetime::Controllers::AlarmController& alarmController;
        Controllers::DateTime& dateTimeController;
        Pinetime::Controllers::FS& filesystem;
        std::array<Tile::Applications, N> apps;

        static constexpr int appsPerScreen = 6;
        static constexpr int nScreens = N > 0 ? (N - 1) / appsPerScreen + 1 : 1;

        ScreenList<nScreens> screens;
      };
    }
  }
}
