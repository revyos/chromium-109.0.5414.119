// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_CHROME_BROWSER_MAIN_EXTRA_PARTS_ASH_H_
#define CHROME_BROWSER_UI_ASH_CHROME_BROWSER_MAIN_EXTRA_PARTS_ASH_H_

#include <memory>

#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/ui/ash/in_session_auth_token_provider_impl.h"
#include "chrome/common/buildflags.h"

namespace ash {
class NetworkPortalNotificationController;
class NewWindowDelegateProvider;
class NightLightClient;
}  // namespace ash

namespace game_mode {
class GameModeController;
}

namespace policy {
class DisplaySettingsHandler;
}

class AccessibilityControllerClient;
class AmbientClientImpl;
class AppAccessNotifier;
class AppListClientImpl;
class ArcOpenUrlDelegateImpl;
class AshShellInit;
class AshWebViewFactoryImpl;
class CastConfigControllerMediaRouter;
class DesksClient;
class ImeControllerClientImpl;
class InSessionAuthDialogClient;
class LoginScreenClientImpl;
class MediaClientImpl;
class MobileDataNotifications;
class NetworkConnectDelegate;
class NightLightClient;
class ProjectorAppClientImpl;
class ProjectorClientImpl;
class QuickAnswersController;
class ScreenOrientationDelegateChromeos;
class SessionControllerClientImpl;
class SystemTrayClientImpl;
class TabClusterUIClient;
class TabletModePageBehavior;
class VpnListForwarder;
class WallpaperControllerClientImpl;

#if BUILDFLAG(ENABLE_WAYLAND_SERVER)
class ExoParts;
#endif

namespace internal {
class ChromeShelfControllerInitializer;
}

// Browser initialization for Ash UI. Only use this for Ash specific
// intitialization (e.g. initialization of chrome/browser/ui/ash classes).
class ChromeBrowserMainExtraPartsAsh : public ChromeBrowserMainExtraParts {
 public:
  ChromeBrowserMainExtraPartsAsh();

  ChromeBrowserMainExtraPartsAsh(const ChromeBrowserMainExtraPartsAsh&) =
      delete;
  ChromeBrowserMainExtraPartsAsh& operator=(
      const ChromeBrowserMainExtraPartsAsh&) = delete;

  ~ChromeBrowserMainExtraPartsAsh() override;

  // Overridden from ChromeBrowserMainExtraParts:
  void PreCreateMainMessageLoop() override;
  void PreProfileInit() override;
  void PostProfileInit(Profile* profile, bool is_initial_profile) override;
  void PostBrowserStart() override;
  void PostMainMessageLoopRun() override;

 private:
  class UserProfileLoadedObserver;

  std::unique_ptr<UserProfileLoadedObserver> user_profile_loaded_observer_;

  // Initialized in PreProfileInit in all configs before Shell init:
  std::unique_ptr<NetworkConnectDelegate> network_connect_delegate_;
  std::unique_ptr<CastConfigControllerMediaRouter>
      cast_config_controller_media_router_;

  // Initialized in PreProfileInit if ash config != MASH:
  std::unique_ptr<AshShellInit> ash_shell_init_;

  // Initialized in PreProfileInit in all configs after Shell init:
  std::unique_ptr<AccessibilityControllerClient>
      accessibility_controller_client_;
  std::unique_ptr<AppListClientImpl> app_list_client_;
  std::unique_ptr<ash::NewWindowDelegateProvider> new_window_delegate_provider_;
  std::unique_ptr<ArcOpenUrlDelegateImpl> arc_open_url_delegate_impl_;
  std::unique_ptr<ImeControllerClientImpl> ime_controller_client_;
  std::unique_ptr<InSessionAuthDialogClient> in_session_auth_dialog_client_;
  std::unique_ptr<ash::InSessionAuthTokenProviderImpl>
      in_session_auth_token_provider_;
  std::unique_ptr<ScreenOrientationDelegateChromeos>
      screen_orientation_delegate_;
  std::unique_ptr<SessionControllerClientImpl> session_controller_client_;
  std::unique_ptr<SystemTrayClientImpl> system_tray_client_;
  std::unique_ptr<TabClusterUIClient> tab_cluster_ui_client_;
  std::unique_ptr<TabletModePageBehavior> tablet_mode_page_behavior_;
  std::unique_ptr<VpnListForwarder> vpn_list_forwarder_;
  std::unique_ptr<WallpaperControllerClientImpl> wallpaper_controller_client_;
  std::unique_ptr<ProjectorClientImpl> projector_client_;
  std::unique_ptr<ProjectorAppClientImpl> projector_app_client_;
  std::unique_ptr<QuickAnswersController> quick_answers_controller_;
  std::unique_ptr<game_mode::GameModeController> game_mode_controller_;
  std::unique_ptr<ash::NetworkPortalNotificationController>
      network_portal_notification_controller_;

  std::unique_ptr<internal::ChromeShelfControllerInitializer>
      chrome_shelf_controller_initializer_;
  std::unique_ptr<DesksClient> desks_client_;

#if BUILDFLAG(ENABLE_WAYLAND_SERVER)
  std::unique_ptr<ExoParts> exo_parts_;
#endif

  // Initialized in PostProfileInit in all configs:
  std::unique_ptr<LoginScreenClientImpl> login_screen_client_;
  std::unique_ptr<MediaClientImpl> media_client_;
  std::unique_ptr<AppAccessNotifier> app_access_notifier_;
  std::unique_ptr<policy::DisplaySettingsHandler> display_settings_handler_;
  std::unique_ptr<AshWebViewFactoryImpl> ash_web_view_factory_;

  // Initialized in PostBrowserStart in all configs:
  std::unique_ptr<MobileDataNotifications> mobile_data_notifications_;
  std::unique_ptr<ash::NightLightClient> night_light_client_;
  std::unique_ptr<AmbientClientImpl> ambient_client_;
};

#endif  // CHROME_BROWSER_UI_ASH_CHROME_BROWSER_MAIN_EXTRA_PARTS_ASH_H_