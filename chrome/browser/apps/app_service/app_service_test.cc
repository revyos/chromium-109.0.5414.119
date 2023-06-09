// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/app_service_test.h"

#include "base/run_loop.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "components/services/app_service/public/cpp/icon_types.h"
#include "ui/gfx/image/image_unittest_util.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/apps/app_service/publishers/arc_apps.h"
#include "chrome/browser/apps/app_service/publishers/arc_apps_factory.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

namespace apps {

AppServiceTest::AppServiceTest() = default;

AppServiceTest::~AppServiceTest() = default;

void AppServiceTest::SetUp(Profile* profile) {
  app_service_proxy_ = AppServiceProxyFactory::GetForProfile(profile);
  app_service_proxy_->ReinitializeForTesting(profile);
}

void AppServiceTest::UninstallAllApps(Profile* profile) {
  auto* app_service_proxy = AppServiceProxyFactory::GetForProfile(profile);
  std::vector<AppPtr> apps;
  app_service_proxy->AppRegistryCache().ForEachApp(
      [&apps](const apps::AppUpdate& update) {
        AppPtr app = std::make_unique<App>(update.AppType(), update.AppId());
        app->readiness = Readiness::kUninstalledByUser;
        apps.push_back(std::move(app));
      });
  app_service_proxy->AppRegistryCache().OnApps(
      std::move(apps), AppType::kUnknown,
      false /* should_notify_initialized */);
}

std::string AppServiceTest::GetAppName(const std::string& app_id) const {
  std::string name;
  if (!app_service_proxy_)
    return name;
  app_service_proxy_->AppRegistryCache().ForOneApp(
      app_id, [&name](const AppUpdate& update) { name = update.Name(); });
  return name;
}

gfx::ImageSkia AppServiceTest::LoadAppIconBlocking(AppType app_type,
                                                   const std::string& app_id,
                                                   int32_t size_hint_in_dip) {
  gfx::ImageSkia image_skia;
  base::RunLoop run_loop;

  app_service_proxy_->LoadIcon(
      app_type, app_id, IconType::kStandard, size_hint_in_dip,
      false /* allow_placeholder_icon */,
      base::BindOnce(
          [](gfx::ImageSkia* icon, base::OnceClosure load_app_icon_callback,
             IconValuePtr icon_value) {
            DCHECK_EQ(IconType::kStandard, icon_value->icon_type);
            *icon = icon_value->uncompressed;
            std::move(load_app_icon_callback).Run();
          },
          &image_skia, run_loop.QuitClosure()));
  run_loop.Run();
  return image_skia;
}

bool AppServiceTest::AreIconImageEqual(const gfx::ImageSkia& src,
                                       const gfx::ImageSkia& dst) {
  return gfx::test::AreBitmapsEqual(src.GetRepresentation(1.0f).GetBitmap(),
                                    dst.GetRepresentation(1.0f).GetBitmap());
}

}  // namespace apps
