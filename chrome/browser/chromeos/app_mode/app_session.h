// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_APP_MODE_APP_SESSION_H_
#define CHROME_BROWSER_CHROMEOS_APP_MODE_APP_SESSION_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/app_mode/app_session_browser_window_handler.h"
#include "chrome/browser/chromeos/app_mode/app_session_metrics_service.h"
#include "ppapi/buildflags/buildflags.h"

class PrefRegistrySimple;
class PrefService;
class Profile;
class Browser;

namespace content {
class WebContents;
}

namespace extensions {
class AppWindow;
}

namespace chromeos {

class KioskSessionPluginHandler;
class KioskSessionPluginHandlerDelegate;

// AppSession maintains a kiosk session and handles its lifetime.
class AppSession {
 public:
  AppSession();
  AppSession(base::OnceClosure attempt_user_exit, PrefService* local_state);
  AppSession(const AppSession&) = delete;
  AppSession& operator=(const AppSession&) = delete;
  virtual ~AppSession();

  static std::unique_ptr<AppSession> CreateForTesting(
      base::OnceClosure attempt_user_exit,
      PrefService* local_state,
      const std::vector<std::string>& crash_dirs);

  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Initializes an app session for Chrome App Kiosk.
  virtual void Init(Profile* profile, const std::string& app_id);

  // Initializes an app session for Web kiosk.
  virtual void InitForWebKiosk(Browser* browser);

  // Invoked when GuestViewManager adds a guest web contents.
  void OnGuestAdded(content::WebContents* guest_web_contents);

  // Replaces chrome::AttemptUserExit() by |closure|.
  void SetAttemptUserExitForTesting(base::OnceClosure closure);

  Browser* GetSettingsBrowserForTesting();
  void SetOnHandleBrowserCallbackForTesting(
      base::RepeatingCallback<void(bool is_closing)> callback);

  KioskSessionPluginHandlerDelegate* GetPluginHandlerDelegateForTesting();

  bool is_shutting_down() const { return is_shutting_down_; }

 protected:
  AppSession(base::OnceClosure attempt_user_exit,
             PrefService* local_state,
             std::unique_ptr<AppSessionMetricsService> metrics_service);

  // Set the |profile_| object.
  void SetProfile(Profile* profile);

  // Create a |browser_window_handler_| object.
  void CreateBrowserWindowHandler(absl::optional<std::string> web_app_name);

 private:
  // AppWindowHandler watches for app window and exits the session when the
  // last window of a given app is closed. This class is only used for Chrome
  // App Kiosk.
  class AppWindowHandler;

  // PluginHandlerDelegateImpl handles callbacks from `plugin_handler_`.
  class PluginHandlerDelegateImpl;

  void OnHandledNewBrowserWindow(bool is_closing);
  void OnAppWindowAdded(extensions::AppWindow* app_window);
  void OnLastAppWindowClosed();

  bool is_shutting_down_ = false;

  std::unique_ptr<AppWindowHandler> app_window_handler_;
  std::unique_ptr<AppSessionBrowserWindowHandler> browser_window_handler_;
#if BUILDFLAG(ENABLE_PLUGINS)
  std::unique_ptr<PluginHandlerDelegateImpl> plugin_handler_delegate_;
  std::unique_ptr<KioskSessionPluginHandler> plugin_handler_;
#endif

  raw_ptr<Profile> profile_ = nullptr;

  base::OnceClosure attempt_user_exit_;
  const std::unique_ptr<AppSessionMetricsService> metrics_service_;

  // Is called whenever a new browser creation was handled by the
  // BrowserWindowHandler.
  base::RepeatingCallback<void(bool is_closing)> on_handle_browser_callback_;

  base::WeakPtrFactory<AppSession> weak_ptr_factory_{this};
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_APP_MODE_APP_SESSION_H_
