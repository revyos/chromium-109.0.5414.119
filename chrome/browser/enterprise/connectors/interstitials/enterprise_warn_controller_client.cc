// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/interstitials/enterprise_warn_controller_client.h"

#include <memory>
#include <utility>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/reputation/reputation_service.h"
#include "chrome/common/url_constants.h"
#include "components/security_interstitials/content/settings_page_helper.h"
#include "components/security_interstitials/core/metrics_helper.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"

namespace {
std::unique_ptr<security_interstitials::MetricsHelper> GetMetricsHelper(
    const GURL& url) {
  security_interstitials::MetricsHelper::ReportDetails settings;
  settings.metric_prefix = "enterprise-warn";

  return std::make_unique<security_interstitials::MetricsHelper>(url, settings,
                                                                 nullptr);
}
}  // namespace

EnterpriseWarnControllerClient::EnterpriseWarnControllerClient(
    content::WebContents* web_contents,
    const GURL& request_url)
    : SecurityInterstitialControllerClient(
          web_contents,
          GetMetricsHelper(request_url),
          Profile::FromBrowserContext(web_contents->GetBrowserContext())
              ->GetPrefs(),
          g_browser_process->GetApplicationLocale(),
          GURL(chrome::kChromeUINewTabURL),
          /*settings_page_helper=*/nullptr),
      request_url_(request_url) {}

EnterpriseWarnControllerClient::~EnterpriseWarnControllerClient() = default;

void EnterpriseWarnControllerClient::GoBack() {
  SecurityInterstitialControllerClient::GoBackAfterNavigationCommitted();
}

void EnterpriseWarnControllerClient::Proceed() {
  // Logic to bypass the warning.
  // TODO(b/251184055): Uncomment once hook is implemented for URLFiltering
  // WebProtego
  // ReputationService::Get(
  //     Profile::FromBrowserContext(web_contents_->GetBrowserContext()))
  //     ->SetUserIgnore(request_url_);
  // Reload();
}
