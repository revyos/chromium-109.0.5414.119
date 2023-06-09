// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/first_party_sets/first_party_sets_navigation_throttle.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/first_party_sets/first_party_sets_policy_service.h"
#include "chrome/browser/first_party_sets/first_party_sets_policy_service_factory.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"

namespace first_party_sets {

namespace {

using ThrottleCheckResult = content::NavigationThrottle::ThrottleCheckResult;

void RecordResumeOnTimeout(bool is_timeout) {
  base::UmaHistogramBoolean("FirstPartySets.NavigationThrottle.ResumeOnTimeout",
                            is_timeout);
}

}  // namespace

FirstPartySetsNavigationThrottle::FirstPartySetsNavigationThrottle(
    content::NavigationHandle* navigation_handle,
    FirstPartySetsPolicyService& service)
    : content::NavigationThrottle(navigation_handle), service_(service) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

FirstPartySetsNavigationThrottle::~FirstPartySetsNavigationThrottle() = default;

ThrottleCheckResult FirstPartySetsNavigationThrottle::WillStartRequest() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (service_->is_enabled() && !service_->is_ready()) {
    service_->RegisterThrottleResumeCallback(
        base::BindOnce(&FirstPartySetsNavigationThrottle::OnReadyToResume,
                       weak_factory_.GetWeakPtr()));
    // Setup timer
    resume_navigation_timer_.Start(
        FROM_HERE, features::kFirstPartySetsNavigationThrottleTimeout.Get(),
        base::BindOnce(&FirstPartySetsNavigationThrottle::OnTimeOut,
                       weak_factory_.GetWeakPtr()));

    return content::NavigationThrottle::DEFER;
  }
  return content::NavigationThrottle::PROCEED;
}

const char* FirstPartySetsNavigationThrottle::GetNameForLogging() {
  return "FirstPartySetsNavigationThrottle";
}

// static
std::unique_ptr<FirstPartySetsNavigationThrottle>
FirstPartySetsNavigationThrottle::MaybeCreateNavigationThrottle(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  FirstPartySetsPolicyService* service =
      FirstPartySetsPolicyServiceFactory::GetForBrowserContext(
          navigation_handle->GetWebContents()->GetBrowserContext());
  if (!features::kFirstPartySetsClearSiteDataOnChangedSets.Get() ||
      navigation_handle->GetParentFrameOrOuterDocument() ||
      service->is_ready()) {
    return nullptr;
  }
  return std::make_unique<FirstPartySetsNavigationThrottle>(navigation_handle,
                                                            *service);
}

void FirstPartySetsNavigationThrottle::OnTimeOut() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!resume_navigation_timer_.IsRunning());
  RecordResumeOnTimeout(true);
  Resume();
}

void FirstPartySetsNavigationThrottle::OnReadyToResume() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // If the timer is not running, that means the timeout has occurred and the
  // navigation has been resumed by `OnTimeOut`, so we don't need to resume
  // again.
  if (!resume_navigation_timer_.IsRunning()) {
    DCHECK(resumed_);
    return;
  }
  // Stop the timer to make sure we won't try to resume again due to hitting
  // the timeout.
  resume_navigation_timer_.Stop();
  RecordResumeOnTimeout(false);
  Resume();
}

void FirstPartySetsNavigationThrottle::Resume() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!resumed_);
  resumed_ = true;
  NavigationThrottle::Resume();
}

}  // namespace first_party_sets
