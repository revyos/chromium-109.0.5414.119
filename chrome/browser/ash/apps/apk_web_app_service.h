// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_APPS_APK_WEB_APP_SERVICE_H_
#define CHROME_BROWSER_ASH_APPS_APK_WEB_APP_SERVICE_H_

#include <string>
#include <vector>

#include "ash/components/arc/mojom/app.mojom-forward.h"
#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ash/apps/apk_web_app_installer.h"
#include "chrome/browser/ash/crosapi/browser_manager.h"
#include "chrome/browser/ash/crosapi/web_app_service_ash.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/web_applications/web_app_id.h"
#include "chrome/browser/web_applications/web_app_install_manager.h"
#include "chrome/browser/web_applications/web_app_install_manager_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/services/app_service/public/cpp/app_registry_cache.h"

class ArcAppListPrefs;
class Profile;
class GURL;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace webapps {
enum class InstallResultCode;
enum class UninstallResultCode;
}  // namespace webapps

namespace web_app {
class WebAppProvider;
}  // namespace web_app

namespace ash {

class ApkWebAppService : public KeyedService,
                         public ApkWebAppInstaller::Owner,
                         public ArcAppListPrefs::Observer,
                         public web_app::WebAppInstallManagerObserver,
                         public apps::AppRegistryCache::Observer,
                         public crosapi::WebAppServiceAsh::Observer {
 public:
  // Handles app install/uninstall operations to external processes (ARC and
  // Lacros) to stub out in tests.
  class Delegate {
   public:
    using WebAppInstallCallback = base::OnceCallback<void(
        const web_app::AppId& web_app_id,
        bool is_web_only_twa,
        const absl::optional<std::string> sha256_fingerprint,
        webapps::InstallResultCode code)>;

    virtual ~Delegate();

    // Kicks off installation of a web app in Lacros. It will first fetch the
    // icon of a package identified by |package_name| from ARC, and then use
    // |web_app_info| and the icon to perform the installation in Lacros. If
    // either ARC or Lacros are not connected, the function does nothing.
    virtual void MaybeInstallWebAppInLacros(
        const std::string& package_name,
        arc::mojom::WebAppInfoPtr web_app_info,
        WebAppInstallCallback callback) = 0;

    // Tells Lacros to remove a web app install source "ARC" for a web app with
    // ID |web_app_id|. If no other sources left, the web app will be
    // uninstalled. Does nothing if Lacros is not connected.
    virtual void MaybeUninstallWebAppInLacros(
        const web_app::AppId& web_app_id) = 0;

    // Tells ARC to uninstall a package identified by |package_name|. Returns
    // true if the call to ARC was successful, false if ARC is not running.
    virtual void MaybeUninstallPackageInArc(
        const std::string& package_name) = 0;
  };

  static ApkWebAppService* Get(Profile* profile);
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  explicit ApkWebAppService(Profile* profile, Delegate* test_delegate);

  ApkWebAppService(const ApkWebAppService&) = delete;
  ApkWebAppService& operator=(const ApkWebAppService&) = delete;

  ~ApkWebAppService() override;

  void SetArcAppListPrefsForTesting(ArcAppListPrefs* prefs);

  bool IsWebOnlyTwa(const web_app::AppId& app_id);
  bool IsWebOnlyTwaDeprecated(const web_app::AppId& app_id);

  bool IsWebAppInstalledFromArc(const web_app::AppId& web_app_id);

  bool IsWebAppShellPackage(const std::string& package_name);

  absl::optional<std::string> GetPackageNameForWebApp(
      const web_app::AppId& app_id);

  absl::optional<std::string> GetPackageNameForWebApp(const GURL& url);

  absl::optional<std::string> GetWebAppIdForPackageName(
      const std::string& package_name);

  absl::optional<std::string> GetCertificateSha256Fingerprint(
      const web_app::AppId& app_id);
  absl::optional<std::string> GetCertificateSha256FingerprintDeprecated(
      const web_app::AppId& app_id);

  using WebAppCallbackForTesting =
      base::OnceCallback<void(const std::string& package_name,
                              const web_app::AppId& web_app_id)>;
  void SetWebAppInstalledCallbackForTesting(
      WebAppCallbackForTesting web_app_installed_callback);
  void SetWebAppUninstalledCallbackForTesting(
      WebAppCallbackForTesting web_app_uninstalled_callback);

 private:
  FRIEND_TEST_ALL_PREFIXES(ApkWebAppInstallerDelayedArcStartBrowserTest,
                           DelayedUninstall);

  Delegate& GetDelegate() {
    return test_delegate_ ? *test_delegate_ : *real_delegate_;
  }

  // Uninstalls a web app with id |web_app_id| iff it was installed via calling
  // ApkWebAppInstaller::Install().
  void UninstallWebApp(const web_app::AppId& web_app_id);

  // If the app has updated from a web app to Android app or vice-versa,
  // this function pins the new app in the old app's place on the shelf if it
  // was pinned prior to the update.
  void UpdateShelfPin(const std::string& package_name,
                      const arc::mojom::WebAppInfoPtr& web_app_info);

  // KeyedService:
  void Shutdown() override;

  // ArcAppListPrefs::Observer:
  void OnPackageInstalled(
      const arc::mojom::ArcPackageInfo& package_info) override;
  void OnPackageRemoved(const std::string& package_name,
                        bool uninstalled) override;
  void OnPackageListInitialRefreshed() override;
  void OnArcAppListPrefsDestroyed() override;

  // web_app::WebAppInstallManagerObserver overrides.
  void OnWebAppWillBeUninstalled(const web_app::AppId& web_app_id) override;
  void OnWebAppInstallManagerDestroyed() override;

  // apps::AppRegistryCache::Observer overrides:
  void OnAppUpdate(const apps::AppUpdate& update) override;
  void OnAppTypeInitialized(apps::AppType app_type) override;
  void OnAppRegistryCacheWillBeDestroyed(
      apps::AppRegistryCache* cache) override;

  // croapi::WebAppServiceAsh::Observer overrides:
  void OnWebAppProviderBridgeConnected() override;

  void MaybeRemoveArcPackageForWebApp(const web_app::AppId& web_app_id);
  void OnDidGetWebAppIcon(const std::string& package_name,
                          arc::mojom::WebAppInfoPtr web_app_info,
                          arc::mojom::RawIconPngDataPtr icon);
  void OnDidFinishInstall(const std::string& package_name,
                          const web_app::AppId& web_app_id,
                          bool is_web_only_twa,
                          const absl::optional<std::string> sha256_fingerprint,
                          webapps::InstallResultCode code);
  void UpdatePackageInfo(const std::string& app_id,
                         const arc::mojom::WebAppInfoPtr& web_app_info);
  const base::Value::Dict& WebAppToApks() const;
  void SyncArcAndWebApps();

  WebAppCallbackForTesting web_app_installed_callback_;
  WebAppCallbackForTesting web_app_uninstalled_callback_;

  Profile* profile_;
  ArcAppListPrefs* arc_app_list_prefs_;
  web_app::WebAppProvider* provider_{nullptr};

  // Delegate implementation used in production.
  std::unique_ptr<Delegate> real_delegate_;
  // And override delegate implementation for tests. See |GetDelegate()|.
  raw_ptr<Delegate> test_delegate_;

  base::ScopedObservation<web_app::WebAppInstallManager,
                          web_app::WebAppInstallManagerObserver>
      install_manager_observer_{this};
  base::ScopedObservation<apps::AppRegistryCache,
                          apps::AppRegistryCache::Observer>
      app_registry_cache_observer_{this};

  base::ScopedObservation<ArcAppListPrefs, ArcAppListPrefs::Observer>
      arc_app_list_prefs_observer_{this};

  base::ScopedObservation<crosapi::WebAppServiceAsh,
                          crosapi::WebAppServiceAsh::Observer>
      web_app_service_observer_{this};

  // Web app installation currently requires Lacros to be always running.
  // TODO(crbug.com/1174246): support web app installation in lacros when lacros
  // is not running all the time (idempotent installation).
  std::unique_ptr<crosapi::BrowserManager::ScopedKeepAlive> keep_alive_;

  // Must go last.
  base::WeakPtrFactory<ApkWebAppService> weak_ptr_factory_{this};
};

}  // namespace ash

#endif  // CHROME_BROWSER_ASH_APPS_APK_WEB_APP_SERVICE_H_
