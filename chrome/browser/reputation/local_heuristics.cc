// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/reputation/local_heuristics.h"

#include "base/bind.h"
#include "base/callback.h"
#include "base/metrics/field_trial_params.h"
#include "base/strings/string_split.h"
#include "chrome/browser/lookalikes/lookalike_url_blocking_page.h"
#include "chrome/browser/lookalikes/lookalike_url_navigation_throttle.h"
#include "chrome/browser/lookalikes/lookalike_url_service.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_features.h"
#include "components/lookalikes/core/features.h"
#include "components/lookalikes/core/lookalike_url_util.h"
#include "components/reputation/core/safety_tips_config.h"
#include "components/url_formatter/spoof_checks/top_domains/top_domain_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace {

// Binary search through |words| to find |needle|.
bool SortedWordListContains(const std::string& needle,
                            const char* const words[],
                            const size_t num_words) {
  // We use a custom comparator for (char *) here, to avoid the costly
  // construction of two std::strings every time two values are compared,
  // and because (char *) orders by address, not lexicographically.
  return std::binary_search(words, words + num_words, needle.c_str(),
                            [](const char* str_one, const char* str_two) {
                              return strcmp(str_one, str_two) < 0;
                            });
}

}  // namespace

bool ShouldTriggerSafetyTipFromLookalike(
    const GURL& url,
    const DomainInfo& navigated_domain,
    const std::vector<DomainInfo>& engaged_sites,
    GURL* safe_url) {
  std::string matched_domain;
  LookalikeUrlMatchType match_type;

  // If the domain and registry is empty, this is a private domain and thus
  // should never be flagged as malicious.
  if (navigated_domain.domain_and_registry.empty()) {
    return false;
  }

  auto* config = reputation::GetSafetyTipsRemoteConfigProto();
  const LookalikeTargetAllowlistChecker in_target_allowlist =
      base::BindRepeating(
          &reputation::IsTargetHostAllowlistedBySafetyTipsComponent, config);
  if (!GetMatchingDomain(navigated_domain, engaged_sites, in_target_allowlist,
                         config, &matched_domain, &match_type)) {
    return false;
  }

  // If we're already displaying an interstitial, don't warn again.
  if (ShouldBlockLookalikeUrlNavigation(match_type)) {
    return false;
  }

  // Use https: scheme for top domain matches. Otherwise, use the lookalike
  // URL's scheme.
  // TODO(crbug.com/1190309): If the match is against an engaged site, this
  // should use the scheme of the engaged site instead.
  const std::string scheme =
      (match_type == LookalikeUrlMatchType::kEditDistance ||
       match_type == LookalikeUrlMatchType::kSkeletonMatchTop500 ||
       match_type == LookalikeUrlMatchType::kSkeletonMatchTop5k)
          ? url::kHttpsScheme
          : url.scheme();
  *safe_url = GURL(scheme + url::kStandardSchemeSeparator + matched_domain);

  switch (match_type) {
    case LookalikeUrlMatchType::kEditDistance:
      return false;
    case LookalikeUrlMatchType::kEditDistanceSiteEngagement:
      return true;
    case LookalikeUrlMatchType::kTargetEmbedding:
      // Target Embedding should block URL Navigation.
      return false;
    case LookalikeUrlMatchType::kTargetEmbeddingForSafetyTips:
      return true;
    case LookalikeUrlMatchType::kSkeletonMatchTop5k:
      return true;
    case LookalikeUrlMatchType::kFailedSpoofChecks:
      // For now, no safety tip is shown for domain names that fail spoof checks
      // and don't have a suggested URL.
      return false;
    case LookalikeUrlMatchType::kSiteEngagement:
    case LookalikeUrlMatchType::kSkeletonMatchTop500:
      // We should only ever reach these cases when the lookalike interstitial
      // is disabled. Now that interstitial is fully launched, this only happens
      // in tests.
      NOTREACHED();
      return false;
    case LookalikeUrlMatchType::kCharacterSwapSiteEngagement:
      return IsHeuristicEnabledForHostname(
          config,
          reputation::HeuristicLaunchConfig::
              HEURISTIC_CHARACTER_SWAP_ENGAGED_SITES,
          navigated_domain.domain_and_registry, chrome::GetChannel());
    case LookalikeUrlMatchType::kCharacterSwapTop500:
      return IsHeuristicEnabledForHostname(
          config,
          reputation::HeuristicLaunchConfig::HEURISTIC_CHARACTER_SWAP_TOP_SITES,
          navigated_domain.domain_and_registry, chrome::GetChannel());
    case LookalikeUrlMatchType::kComboSquatting:
      return IsHeuristicEnabledForHostname(
          config,
          reputation::HeuristicLaunchConfig::
              HEURISTIC_COMBO_SQUATTING_TOP_DOMAINS,
          navigated_domain.domain_and_registry, chrome::GetChannel());
    case LookalikeUrlMatchType::kComboSquattingSiteEngagement:
      return IsHeuristicEnabledForHostname(
          config,
          reputation::HeuristicLaunchConfig::
              HEURISTIC_COMBO_SQUATTING_ENGAGED_SITES,
          navigated_domain.domain_and_registry, chrome::GetChannel());
    case LookalikeUrlMatchType::kNone:
      NOTREACHED();
  }

  NOTREACHED();
  return false;
}

bool ShouldTriggerSafetyTipFromKeywordInURL(
    const GURL& url,
    const DomainInfo& navigated_domain,
    const char* const sensitive_keywords[],
    const size_t num_sensitive_keywords) {
  return HostnameContainsKeyword(url, navigated_domain.domain_and_registry,
                                 sensitive_keywords, num_sensitive_keywords,
                                 /* search_e2ld = */ true);
}

bool HostnameContainsKeyword(const GURL& url,
                             const std::string& eTLD_plus_one,
                             const char* const keywords[],
                             const size_t num_keywords,
                             bool search_e2ld) {
  // We never want to trigger this heuristic on any non-http / https sites.
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }

  // The URL's eTLD + 1 will be empty whenever we're given a host that's
  // invalid.
  if (eTLD_plus_one.empty()) {
    return false;
  }

  // TODO(jdeblasio): This should use GetETLDPlusOne() from Lookalike Utils to
  // benefit from de-facto-private registries.
  size_t registry_length = net::registry_controlled_domains::GetRegistryLength(
      url, net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
      net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);

  // Getting a registry length of 0 means that our URL has an unknown registry.
  if (registry_length == 0) {
    return false;
  }

  // e2LD: effective 2nd-level domain, e.g. "google" for "www.google.co.uk".
  std::string e2LD =
      eTLD_plus_one.substr(0, eTLD_plus_one.size() - registry_length - 1);
  // search_substr is the hostname except the eTLD (e.g. "www.google").
  std::string search_substr =
      url.host().substr(0, url.host().size() - registry_length - 1);

  // We should never end up with a "." in our e2LD.
  DCHECK_EQ(e2LD.find("."), std::string::npos);
  // Any problems that would result in an empty e2LD should have been caught via
  // the |eTLD_plus_one| check.

  // If we want to exclude the e2LD, or if the e2LD is itself a keyword, then
  // chop that off and only search the rest of it. Otherwise, we keep the full
  // e2LD included to detect hyphenated spoofs (e.g. "evil-google.com").
  if (!search_e2ld || SortedWordListContains(e2LD, keywords, num_keywords)) {
    // If the user visited the eTLD+1 directly, bail here.
    if (search_substr.size() == e2LD.size()) {
      return false;
    }

    search_substr =
        search_substr.substr(0, search_substr.size() - e2LD.size() - 1);
    // e.g. search_substr goes from "www.google" -> "www".
  }

  const std::vector<std::string> search_parts = base::SplitString(
      search_substr, ".-", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  for (const auto& part : search_parts) {
    if (SortedWordListContains(part, keywords, num_keywords)) {
      return true;
    }
  }

  return false;
}
