// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_SUBSCRIBER_CROSAPI_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_SUBSCRIBER_CROSAPI_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/apps/app_service/app_service_proxy_forward.h"
#include "chromeos/crosapi/mojom/app_service.mojom.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/services/app_service/public/cpp/icon_types.h"
#include "components/services/app_service/public/cpp/preferred_app.h"
#include "components/services/app_service/public/mojom/app_service.mojom.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"

class Profile;

namespace apps {

// App service subscriber to support App Service Proxy in Lacros.
// This object is used as a proxy to communicate between the
// crosapi and App Service.
//
// See components/services/app_service/README.md.
//
// TODO(crbug.com/1253250): Remove dependency on apps::mojom::Subscriber.
class SubscriberCrosapi : public KeyedService,
                          public apps::mojom::Subscriber,
                          public crosapi::mojom::AppServiceProxy {
 public:
  explicit SubscriberCrosapi(Profile* profile);
  SubscriberCrosapi(const SubscriberCrosapi&) = delete;
  SubscriberCrosapi& operator=(const SubscriberCrosapi&) = delete;
  ~SubscriberCrosapi() override;

  void RegisterAppServiceProxyFromCrosapi(
      mojo::PendingReceiver<crosapi::mojom::AppServiceProxy> receiver);

  void OnApps(const std::vector<AppPtr>& deltas,
              AppType app_type,
              bool should_notify_initialized);

  virtual void InitializeApps();
  virtual void InitializePreferredApps(PreferredApps preferred_apps);
  virtual void OnPreferredAppsChanged(PreferredAppChangesPtr changes);

 protected:
  // apps::mojom::Subscriber overrides.
  void OnApps(std::vector<apps::mojom::AppPtr> deltas,
              apps::mojom::AppType mojom_app_type,
              bool should_notify_initialized) override;
  void OnCapabilityAccesses(
      std::vector<apps::mojom::CapabilityAccessPtr> deltas) override;
  void Clone(mojo::PendingReceiver<apps::mojom::Subscriber> receiver) override;
  void OnCrosapiDisconnected();

  // crosapi::mojom::AppServiceProxy overrides.
  void RegisterAppServiceSubscriber(
      mojo::PendingRemote<crosapi::mojom::AppServiceSubscriber> subscriber)
      override;
  void Launch(crosapi::mojom::LaunchParamsPtr launch_params) override;
  void LoadIcon(const std::string& app_id,
                IconKeyPtr icon_key,
                IconType icon_type,
                int32_t size_hint_in_dip,
                apps::LoadIconCallback callback) override;
  void AddPreferredApp(const std::string& app_id,
                       crosapi::mojom::IntentPtr intent) override;
  void ShowAppManagementPage(const std::string& app_id) override;
  void SetSupportedLinksPreference(const std::string& app_id) override;
  void UninstallSilently(const std::string& app_id,
                         UninstallSource uninstall_source) override;

  void OnSubscriberDisconnected();

  mojo::Receiver<crosapi::mojom::AppServiceProxy> crosapi_receiver_{this};
  mojo::ReceiverSet<apps::mojom::Subscriber> receivers_;
  mojo::Remote<crosapi::mojom::AppServiceSubscriber> subscriber_;

  raw_ptr<Profile> profile_;
  raw_ptr<apps::AppServiceProxy> proxy_ = nullptr;
};

}  // namespace apps

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_SUBSCRIBER_CROSAPI_H_
