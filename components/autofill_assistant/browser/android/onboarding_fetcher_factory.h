// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ANDROID_ONBOARDING_FETCHER_FACTORY_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ANDROID_ONBOARDING_FETCHER_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

namespace content {
class BrowserContext;
}  // namespace content

namespace autofill_assistant {
class AutofillAssistantOnboardingFetcher;
}  // namespace autofill_assistant

// Creates instances of |AutofillAssistantOnboardingFetcher| per
// |BrowserContext|.
class OnboardingFetcherFactory : public BrowserContextKeyedServiceFactory {
 public:
  OnboardingFetcherFactory();
  ~OnboardingFetcherFactory() override;

  static OnboardingFetcherFactory* GetInstance();
  static autofill_assistant::AutofillAssistantOnboardingFetcher*
  GetForBrowserContext(content::BrowserContext* browser_context);

 private:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* browser_context) const override;
};

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_ANDROID_ONBOARDING_FETCHER_FACTORY_H_
