#pragma once
#include "displayapp/apps/Apps.h"
#include "Controllers.h"

#include <algorithm>
#include "displayapp/screens/Alarm.h"
#include "displayapp/screens/Breakout.h"
#include "displayapp/screens/BubbleLevel.h"
#include "displayapp/screens/Calculator.h"
#include "displayapp/screens/CookingTimer.h"
#include "displayapp/screens/Dice.h"
#include "displayapp/screens/HeartRate.h"
#include "displayapp/screens/HomeAssistantLights.h"
#include "displayapp/screens/HomeAssistantSwitches.h"
#include "displayapp/screens/HomeAssistantTemperatures.h"
#include "displayapp/screens/HomeAssistantThermostat.h"
#include "displayapp/screens/HomeAssistantBtRestart.h"
#include "displayapp/screens/Metronome.h"
#include "displayapp/screens/Paddle.h"
#include "displayapp/screens/Pomodoro.h"
#include "displayapp/screens/Steps.h"
#include "displayapp/screens/StopWatch.h"
#include "displayapp/screens/Timer.h"
#include "displayapp/screens/Twos.h"
#include "displayapp/screens/Tile.h"
#include "displayapp/screens/ApplicationList.h"
#include "displayapp/screens/WatchFaceDigital.h"
#include "displayapp/screens/WatchFaceAnalog.h"
#include "displayapp/screens/WatchFaceCasioStyleG7710.h"
#include "displayapp/screens/WatchFaceInfineat.h"
#include "displayapp/screens/WatchFacePineTimeStyle.h"
#include "displayapp/screens/WatchFaceTerminal.h"
#include "displayapp/screens/WatchFaceNumerals.h"
#include "displayapp/screens/WatchFacePrideFlag.h"
#include "displayapp/screens/WatchFaceLegend.h"
#include "displayapp/screens/WatchFaceLights.h"
#include "displayapp/screens/WatchFacePolygons.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class Screen;
    }

    struct AppDescription {
      Apps app;
      const char* icon;
      Screens::Screen* (*create)(AppControllers& controllers);
      bool (*isAvailable)(Controllers::FS& fileSystem);
    };

    struct WatchFaceDescription {
      WatchFace watchFace;
      const char* name;
      Screens::Screen* (*create)(AppControllers& controllers);
      bool (*isAvailable)(Controllers::FS& fileSystem);
    };

    template <Apps t>
    consteval AppDescription CreateAppDescription() {
      return {AppTraits<t>::app, AppTraits<t>::icon, &AppTraits<t>::Create, &AppTraits<t>::IsAvailable};
    }

    template <WatchFace t>
    consteval WatchFaceDescription CreateWatchFaceDescription() {
      return {WatchFaceTraits<t>::watchFace, WatchFaceTraits<t>::name, &WatchFaceTraits<t>::Create, &WatchFaceTraits<t>::IsAvailable};
    }

    template <template <Apps...> typename T, Apps... ts>
    consteval std::array<AppDescription, sizeof...(ts)> CreateAppDescriptions(T<ts...>) {
      return {CreateAppDescription<ts>()...};
    }

    template <template <WatchFace...> typename T, WatchFace... ts>
    consteval std::array<WatchFaceDescription, sizeof...(ts)> CreateWatchFaceDescriptions(T<ts...>) {
      return {CreateWatchFaceDescription<ts>()...};
    }

    template <>
    struct AppTraits<Apps::HaLights> {
      static constexpr Apps app = Apps::HaLights;
      static constexpr const char* icon = Screens::Symbols::lightbulb;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HomeAssistantLights(*controllers.haService, controllers.motorController);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::HaSwitches> {
      static constexpr Apps app = Apps::HaSwitches;
      static constexpr const char* icon = Screens::Symbols::toggleOn;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HomeAssistantSwitches(*controllers.haService, controllers.motorController);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::HaThermostat> {
      static constexpr Apps app = Apps::HaThermostat;
      static constexpr const char* icon = Screens::Symbols::thermometer;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HomeAssistantThermostat(*controllers.haService, controllers.motorController);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::HaTemperatures> {
      static constexpr Apps app = Apps::HaTemperatures;
      static constexpr const char* icon = Screens::Symbols::tempHigh;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HomeAssistantTemperatures(*controllers.haService);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::HaBtRestart> {
      static constexpr Apps app = Apps::HaBtRestart;
      static constexpr const char* icon = Screens::Symbols::bluetooth;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HomeAssistantBtRestart(*controllers.haService, controllers.motorController);
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    constexpr auto timeApps = CreateAppDescriptions(TimeAppTypes {});
    constexpr auto gameApps = CreateAppDescriptions(GameAppTypes {});
    constexpr auto exerciseApps = CreateAppDescriptions(ExerciseAppTypes {});
    constexpr auto utilitiesApps = CreateAppDescriptions(UtilitiesAppTypes {});
    constexpr auto haApps = CreateAppDescriptions(HomeAssistantAppTypes {});

    template <>
    struct AppTraits<Apps::TimeMenu> {
      static constexpr Apps app = Apps::TimeMenu;
      static constexpr const char* icon = Screens::Symbols::clock;

      static Screens::Screen* Create(AppControllers& controllers) {
        std::array<Screens::Tile::Applications, TimeAppTypes::Count> apps;
        std::ranges::transform(timeApps, apps.begin(), [&controllers](const auto& subApp) {
          return Screens::Tile::Applications {subApp.icon, subApp.app, subApp.isAvailable(controllers.filesystem)};
        });
        return new Screens::ApplicationList<TimeAppTypes::Count>(controllers.displayApp,
                                                                 controllers.settingsController,
                                                                 controllers.batteryController,
                                                                 controllers.bleController,
                                                                 controllers.alarmController,
                                                                 controllers.dateTimeController,
                                                                 controllers.filesystem,
                                                                 std::move(apps));
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::GamesMenu> {
      static constexpr Apps app = Apps::GamesMenu;
      static constexpr const char* icon = Screens::Symbols::gamepad;

      static Screens::Screen* Create(AppControllers& controllers) {
        std::array<Screens::Tile::Applications, GameAppTypes::Count> apps;
        std::ranges::transform(gameApps, apps.begin(), [&controllers](const auto& subApp) {
          return Screens::Tile::Applications {subApp.icon, subApp.app, subApp.isAvailable(controllers.filesystem)};
        });
        return new Screens::ApplicationList<GameAppTypes::Count>(controllers.displayApp,
                                                                 controllers.settingsController,
                                                                 controllers.batteryController,
                                                                 controllers.bleController,
                                                                 controllers.alarmController,
                                                                 controllers.dateTimeController,
                                                                 controllers.filesystem,
                                                                 std::move(apps));
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::ExerciseMenu> {
      static constexpr Apps app = Apps::ExerciseMenu;
      static constexpr const char* icon = Screens::Symbols::dumbbell;

      static Screens::Screen* Create(AppControllers& controllers) {
        std::array<Screens::Tile::Applications, ExerciseAppTypes::Count> apps;
        std::ranges::transform(exerciseApps, apps.begin(), [&controllers](const auto& subApp) {
          return Screens::Tile::Applications {subApp.icon, subApp.app, subApp.isAvailable(controllers.filesystem)};
        });
        return new Screens::ApplicationList<ExerciseAppTypes::Count>(controllers.displayApp,
                                                                     controllers.settingsController,
                                                                     controllers.batteryController,
                                                                     controllers.bleController,
                                                                     controllers.alarmController,
                                                                     controllers.dateTimeController,
                                                                     controllers.filesystem,
                                                                     std::move(apps));
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::UtilitiesMenu> {
      static constexpr Apps app = Apps::UtilitiesMenu;
      static constexpr const char* icon = Screens::Symbols::wrench;

      static Screens::Screen* Create(AppControllers& controllers) {
        std::array<Screens::Tile::Applications, UtilitiesAppTypes::Count> apps;
        std::ranges::transform(utilitiesApps, apps.begin(), [&controllers](const auto& subApp) {
          return Screens::Tile::Applications {subApp.icon, subApp.app, subApp.isAvailable(controllers.filesystem)};
        });
        return new Screens::ApplicationList<UtilitiesAppTypes::Count>(controllers.displayApp,
                                                                      controllers.settingsController,
                                                                      controllers.batteryController,
                                                                      controllers.bleController,
                                                                      controllers.alarmController,
                                                                      controllers.dateTimeController,
                                                                      controllers.filesystem,
                                                                      std::move(apps));
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct AppTraits<Apps::HomeAssistantMenu> {
      static constexpr Apps app = Apps::HomeAssistantMenu;
      static constexpr const char* icon = Screens::Symbols::home;

      static Screens::Screen* Create(AppControllers& controllers) {
        std::array<Screens::Tile::Applications, HomeAssistantAppTypes::Count> apps;
        std::ranges::transform(haApps, apps.begin(), [&controllers](const auto& subApp) {
          return Screens::Tile::Applications {subApp.icon, subApp.app, subApp.isAvailable(controllers.filesystem)};
        });
        return new Screens::ApplicationList<HomeAssistantAppTypes::Count>(controllers.displayApp,
                                                                          controllers.settingsController,
                                                                          controllers.batteryController,
                                                                          controllers.bleController,
                                                                          controllers.alarmController,
                                                                          controllers.dateTimeController,
                                                                          controllers.filesystem,
                                                                          std::move(apps));
      }

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    constexpr auto userApps = CreateAppDescriptions(UserAppTypes {});
    constexpr auto userWatchFaces = CreateWatchFaceDescriptions(UserWatchFaceTypes {});
  }
}
