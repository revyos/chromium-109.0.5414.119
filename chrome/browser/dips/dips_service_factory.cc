// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dips/dips_service_factory.h"

#include "base/memory/singleton.h"
#include "chrome/browser/dips/dips_service.h"
#include "chrome/browser/engagement/site_engagement_service_factory.h"

// static
DIPSService* DIPSServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<DIPSService*>(
      GetInstance()->GetServiceForBrowserContext(context, /*create=*/true));
}

DIPSServiceFactory* DIPSServiceFactory::GetInstance() {
  return base::Singleton<DIPSServiceFactory>::get();
}

DIPSServiceFactory::DIPSServiceFactory()
    : ProfileKeyedServiceFactory(
          "DIPSService",
          ProfileSelections::BuildForRegularAndIncognito()) {
  DependsOn(site_engagement::SiteEngagementServiceFactory::GetInstance());
}

DIPSServiceFactory::~DIPSServiceFactory() = default;

KeyedService* DIPSServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new DIPSService(context);
}
