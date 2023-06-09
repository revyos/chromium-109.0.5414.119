// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_preload_service/app_preload_service_factory.h"

#include "chrome/browser/apps/app_preload_service/app_preload_service.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/web_app_provider_factory.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "chrome/common/chrome_features.h"

namespace apps {

AppPreloadServiceFactory::AppPreloadServiceFactory()
    : ProfileKeyedServiceFactory(
          "AppPreloadService",
          // Service is available in Kiosk, Chrome OS Guest mode (OTR), and
          // Regular but not in incognito profiles.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kOffTheRecordOnly)
              .Build()) {
  DependsOn(web_app::WebAppProviderFactory::GetInstance());
  DependsOn(apps::AppServiceProxyFactory::GetInstance());
}

AppPreloadServiceFactory::~AppPreloadServiceFactory() = default;

// static
AppPreloadService* AppPreloadServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<AppPreloadService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
AppPreloadServiceFactory* AppPreloadServiceFactory::GetInstance() {
  static base::NoDestructor<AppPreloadServiceFactory> instance;
  return instance.get();
}

// static
bool AppPreloadServiceFactory::IsAvailable(Profile* profile) {
  if (!base::FeatureList::IsEnabled(features::kAppPreloadService)) {
    return false;
  }

  if (!apps::AppServiceProxyFactory::IsAppServiceAvailableForProfile(profile)) {
    return false;
  }

  if (!web_app::AreWebAppsEnabled(profile)) {
    return false;
  }

  return true;
}

KeyedService* AppPreloadServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (!IsAvailable(profile)) {
    return nullptr;
  }
  return new AppPreloadService(profile);
}

bool AppPreloadServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace apps
