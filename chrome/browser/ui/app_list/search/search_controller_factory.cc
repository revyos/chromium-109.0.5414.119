// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/search_controller_factory.h"

#include <stddef.h>

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/app_list/app_list_config.h"
#include "ash/public/cpp/app_list/app_list_features.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/default_clock.h"
#include "build/build_config.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/ash/arc/arc_util.h"
#include "chrome/browser/ash/crosapi/browser_util.h"
#include "chrome/browser/ash/crosapi/crosapi_manager.h"
#include "chrome/browser/ash/drive/drive_integration_service.h"
#include "chrome/browser/ash/web_applications/personalization_app/personalization_app_manager.h"
#include "chrome/browser/ash/web_applications/personalization_app/personalization_app_manager_factory.h"
#include "chrome/browser/ash/web_applications/personalization_app/personalization_app_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/search/app_search_provider.h"
#include "chrome/browser/ui/app_list/search/app_zero_state_provider.h"
#include "chrome/browser/ui/app_list/search/arc/arc_app_shortcuts_search_provider.h"
#include "chrome/browser/ui/app_list/search/arc/arc_playstore_search_provider.h"
#include "chrome/browser/ui/app_list/search/assistant_text_search_provider.h"
#include "chrome/browser/ui/app_list/search/files/drive_search_provider.h"
#include "chrome/browser/ui/app_list/search/files/file_search_provider.h"
#include "chrome/browser/ui/app_list/search/files/zero_state_drive_provider.h"
#include "chrome/browser/ui/app_list/search/files/zero_state_file_provider.h"
#include "chrome/browser/ui/app_list/search/games/game_provider.h"
#include "chrome/browser/ui/app_list/search/help_app_provider.h"
#include "chrome/browser/ui/app_list/search/help_app_zero_state_provider.h"
#include "chrome/browser/ui/app_list/search/keyboard_shortcut_provider.h"
#include "chrome/browser/ui/app_list/search/omnibox/omnibox_lacros_provider.h"
#include "chrome/browser/ui/app_list/search/omnibox/omnibox_provider.h"
#include "chrome/browser/ui/app_list/search/os_settings_provider.h"
#include "chrome/browser/ui/app_list/search/personalization_provider.h"
#include "chrome/browser/ui/app_list/search/search_controller.h"
#include "chrome/browser/ui/app_list/search/search_controller_impl.h"
#include "chrome/browser/ui/app_list/search/search_features.h"
#include "chrome/browser/ui/webui/settings/ash/os_settings_manager.h"
#include "chrome/browser/ui/webui/settings/ash/os_settings_manager_factory.h"
#include "components/session_manager/core/session_manager.h"

namespace app_list {

namespace {

// Maximum number of results to show in each mixer group.

constexpr size_t kMaxAppShortcutResults = 4;
constexpr size_t kMaxPlayStoreResults = 12;

}  // namespace

std::unique_ptr<SearchController> CreateSearchController(
    Profile* profile,
    AppListModelUpdater* model_updater,
    AppListControllerDelegate* list_controller,
    ash::AppListNotifier* notifier) {
  std::unique_ptr<SearchController> controller;
  controller = std::make_unique<SearchControllerImpl>(
      model_updater, list_controller, notifier, profile);

  // Add search providers.
  controller->AddProvider(std::make_unique<AppSearchProvider>(
      controller->GetAppSearchDataSource()));
  controller->AddProvider(std::make_unique<AppZeroStateProvider>(
      controller->GetAppSearchDataSource(), model_updater));

  if (crosapi::browser_util::IsLacrosEnabled()) {
    controller->AddProvider(std::make_unique<OmniboxLacrosProvider>(
        profile, list_controller, crosapi::CrosapiManager::Get()));
  } else {
    controller->AddProvider(
        std::make_unique<OmniboxProvider>(profile, list_controller));
  }

  controller->AddProvider(std::make_unique<AssistantTextSearchProvider>());

  // File search providers are added only when not in guest session and running
  // on Chrome OS.
  if (!profile->IsGuestSession()) {
    controller->AddProvider(std::make_unique<FileSearchProvider>(profile));
    controller->AddProvider(std::make_unique<DriveSearchProvider>(profile));
  }

  if (app_list_features::IsLauncherPlayStoreSearchEnabled()) {
    controller->AddProvider(std::make_unique<ArcPlayStoreSearchProvider>(
        kMaxPlayStoreResults, profile, list_controller));
  }

  if (arc::IsArcAllowedForProfile(profile)) {
    controller->AddProvider(std::make_unique<ArcAppShortcutsSearchProvider>(
        kMaxAppShortcutResults, profile, list_controller));
  }

  if (ash::features::IsProductivityLauncherEnabled() &&
      base::GetFieldTrialParamByFeatureAsBool(
          ash::features::kProductivityLauncher, "enable_continue", false)) {
    controller->AddProvider(std::make_unique<ZeroStateFileProvider>(profile));

    controller->AddProvider(std::make_unique<ZeroStateDriveProvider>(
        profile, controller.get(),
        drive::DriveIntegrationServiceFactory::GetForProfile(profile),
        session_manager::SessionManager::Get()));
  }

  auto* os_settings_manager =
      ash::settings::OsSettingsManagerFactory::GetForProfile(profile);
  auto* app_service_proxy =
      apps::AppServiceProxyFactory::GetForProfile(profile);
  if (os_settings_manager && app_service_proxy) {
    controller->AddProvider(std::make_unique<OsSettingsProvider>(
        profile, os_settings_manager->search_handler(),
        os_settings_manager->hierarchy(), app_service_proxy));
  }

  if (ash::features::IsProductivityLauncherEnabled() &&
      base::GetFieldTrialParamByFeatureAsBool(
          ash::features::kProductivityLauncher, "enable_shortcuts", true)) {
    controller->AddProvider(
        std::make_unique<KeyboardShortcutProvider>(profile));
  }

  controller->AddProvider(std::make_unique<HelpAppProvider>(profile));

  controller->AddProvider(
      std::make_unique<HelpAppZeroStateProvider>(profile, notifier));

  if (search_features::IsLauncherGameSearchEnabled()) {
    controller->AddProvider(
        std::make_unique<GameProvider>(profile, list_controller));
  }

  if (ash::personalization_app::CanSeeWallpaperOrPersonalizationApp(profile)) {
    auto* personalization_app_manager = ash::personalization_app::
        PersonalizationAppManagerFactory::GetForBrowserContext(profile);
    DCHECK(personalization_app_manager);

    if (personalization_app_manager) {
      controller->AddProvider(std::make_unique<PersonalizationProvider>(
          profile, personalization_app_manager->search_handler()));
    }
  }

  return controller;
}

}  // namespace app_list
