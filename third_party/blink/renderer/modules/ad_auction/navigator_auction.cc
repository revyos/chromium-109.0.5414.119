// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/ad_auction/navigator_auction.h"

#include <utility>

#include "base/check.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/map_traits_wtf_hash_map.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/browser_interface_broker_proxy.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/fenced_frame/fenced_frame_utils.h"
#include "third_party/blink/public/common/interest_group/ad_auction_constants.h"
#include "third_party/blink/public/mojom/interest_group/interest_group_types.mojom-blink.h"
#include "third_party/blink/public/mojom/parakeet/ad_request.mojom-blink.h"
#include "third_party/blink/public/mojom/permissions_policy/permissions_policy.mojom-blink.h"
#include "third_party/blink/public/web/web_console_message.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise_resolver.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_throw_dom_exception.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_usvstring_usvstringsequence.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ad_properties.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ad_request_config.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ad_targeting.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_ads.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_auction_ad.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_auction_ad_config.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_auction_ad_interest_group.h"
#include "third_party/blink/renderer/bindings/modules/v8/v8_union_adproperties_adpropertiessequence.h"
#include "third_party/blink/renderer/core/dom/abort_signal.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/dom_exception.h"
#include "third_party/blink/renderer/core/dom/scoped_abort_state.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/navigator.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/modules/ad_auction/ads.h"
#include "third_party/blink/renderer/modules/ad_auction/join_leave_queue.h"
#include "third_party/blink/renderer/modules/ad_auction/validate_blink_interest_group.h"
#include "third_party/blink/renderer/modules/geolocation/geolocation_coordinates.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin_hash.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "v8/include/v8-primitive.h"

namespace blink {

namespace {

// The maximum number of active cross-site joins and leaves. Once these are hit,
// cross-site joins/leaves are queued until they drop below this number. Queued
// pending operations are dropped on destruction / navigation away.
const int kMaxActiveCrossSiteJoins = 20;
const int kMaxActiveCrossSiteLeaves = 20;

// Error string builders.

String ErrorInvalidInterestGroup(const AuctionAdInterestGroup& group,
                                 const String& field_name,
                                 const String& field_value,
                                 const String& error) {
  return String::Format(
      "%s '%s' for AuctionAdInterestGroup with owner '%s' and name '%s' %s",
      field_name.Utf8().c_str(), field_value.Utf8().c_str(),
      group.owner().Utf8().c_str(), group.name().Utf8().c_str(),
      error.Utf8().c_str());
}

String ErrorInvalidInterestGroupJson(const AuctionAdInterestGroup& group,
                                     const String& field_name) {
  return String::Format(
      "%s for AuctionAdInterestGroup with owner '%s' and name '%s' must be a "
      "JSON-serializable object.",
      field_name.Utf8().c_str(), group.owner().Utf8().c_str(),
      group.name().Utf8().c_str());
}

String ErrorInvalidAuctionConfig(const AuctionAdConfig& config,
                                 const String& field_name,
                                 const String& field_value,
                                 const String& error) {
  return String::Format("%s '%s' for AuctionAdConfig with seller '%s' %s",
                        field_name.Utf8().c_str(), field_value.Utf8().c_str(),
                        config.seller().Utf8().c_str(), error.Utf8().c_str());
}

String ErrorInvalidAuctionConfigJson(const AuctionAdConfig& config,
                                     const String& field_name) {
  return String::Format(
      "%s for AuctionAdConfig with seller '%s' must be a JSON-serializable "
      "object.",
      field_name.Utf8().c_str(), config.seller().Utf8().c_str());
}

String ErrorInvalidAdRequestConfig(const AdRequestConfig& config,
                                   const String& field_name,
                                   const String& field_value,
                                   const String& error) {
  return String::Format("%s '%s' for AdRequestConfig with URL '%s' %s",
                        field_name.Utf8().c_str(), field_value.Utf8().c_str(),
                        config.adRequestUrl().Utf8().c_str(),
                        error.Utf8().c_str());
}

String WarningPermissionsPolicy(const String& feature, const String& api) {
  return String::Format(
      "In the future, Permissions Policy feature %s will not be enabled by "
      "default in cross-origin iframes or same-origin iframes nested in "
      "cross-origin iframes. Calling %s will be rejected with NotAllowedError "
      "if it is not explicitly enabled",
      feature.Utf8().c_str(), api.Utf8().c_str());
}

// JSON and Origin conversion helpers.

bool Jsonify(const ScriptState& script_state,
             const v8::Local<v8::Value>& value,
             String& output) {
  v8::Local<v8::String> v8_string;
  // v8::JSON throws on certain inputs that can't be converted to JSON (like
  // recursive structures). Use TryCatch to consume them. Otherwise, they'd take
  // precedence over the returned ExtensionState for methods that return
  // ScriptPromises, since ExceptionState is used to generate a rejected
  // promise, which V8 exceptions take precedence over.
  v8::TryCatch try_catch(script_state.GetIsolate());
  if (!v8::JSON::Stringify(script_state.GetContext(), value)
           .ToLocal(&v8_string) ||
      try_catch.HasCaught()) {
    return false;
  }

  output = ToCoreString(v8_string);
  // JSON.stringify can fail to produce a string value in one of two ways: it
  // can throw an exception (as with unserializable objects), or it can return
  // `undefined` (as with e.g. passing a function). If JSON.stringify returns
  // `undefined`, the v8 API then coerces it to the string value "undefined".
  // Check for this, and consider it a failure (since we didn't properly
  // serialize a value, and v8::JSON::Parse() rejects "undefined").
  return output != "undefined";
}

// Returns nullptr if |origin_string| couldn't be parsed into an acceptable
// origin.
scoped_refptr<const SecurityOrigin> ParseOrigin(const String& origin_string) {
  scoped_refptr<const SecurityOrigin> origin =
      SecurityOrigin::CreateFromString(origin_string);
  if (origin->Protocol() != url::kHttpsScheme)
    return nullptr;
  return origin;
}

// WebIDL -> Mojom copy functions -- each return true if successful (including
// the not present, nothing to copy case), returns false and throws JS exception
// for invalid input.

// joinAdInterestGroup() copy functions.

bool CopyOwnerFromIdlToMojo(const ExecutionContext& execution_context,
                            ExceptionState& exception_state,
                            const AuctionAdInterestGroup& input,
                            mojom::blink::InterestGroup& output) {
  scoped_refptr<const SecurityOrigin> owner = ParseOrigin(input.owner());
  if (!owner) {
    exception_state.ThrowTypeError(String::Format(
        "owner '%s' for AuctionAdInterestGroup with name '%s' must be a valid "
        "https origin.",
        input.owner().Utf8().c_str(), input.name().Utf8().c_str()));
    return false;
  }

  output.owner = std::move(owner);
  return true;
}

// Converts a sparse vector used in `priority_vector` and
// `priority_signals_overrides` to a WTF::HashMap, as is used in mojom structs.
// Has no failure cases.
WTF::HashMap<WTF::String, double> ConvertSparseVectorIdlToMojo(
    const Vector<std::pair<WTF::String, double>>& priority_signals_in) {
  WTF::HashMap<WTF::String, double> priority_signals_out;
  for (const auto& key_value_pair : priority_signals_in) {
    priority_signals_out.insert(key_value_pair.first, key_value_pair.second);
  }
  return priority_signals_out;
}

bool CopyExecutionModeFromIdlToMojo(const ExecutionContext& execution_context,
                                    ExceptionState& exception_state,
                                    const AuctionAdInterestGroup& input,
                                    mojom::blink::InterestGroup& output) {
  if (!input.hasExecutionMode())
    return true;

  switch (input.executionMode().AsEnum()) {
    case V8WorkletExecutionMode::Enum::kCompatibility:
      output.execution_mode =
          mojom::blink::InterestGroup::ExecutionMode::kCompatibilityMode;
      break;
    case V8WorkletExecutionMode::Enum::kGroupByOrigin:
      output.execution_mode =
          mojom::blink::InterestGroup::ExecutionMode::kGroupedByOriginMode;
      break;
    default:
      exception_state.ThrowTypeError(ErrorInvalidInterestGroup(
          input, "executionMode", input.executionMode().AsString(),
          "is not a supported execution mode."));
      return false;
  }
  return true;
}

bool CopyBiddingLogicUrlFromIdlToMojo(const ExecutionContext& context,
                                      ExceptionState& exception_state,
                                      const AuctionAdInterestGroup& input,
                                      mojom::blink::InterestGroup& output) {
  if (!input.hasBiddingLogicUrl())
    return true;
  KURL bidding_url = context.CompleteURL(input.biddingLogicUrl());
  if (!bidding_url.IsValid()) {
    exception_state.ThrowTypeError(ErrorInvalidInterestGroup(
        input, "biddingLogicUrl", input.biddingLogicUrl(),
        "cannot be resolved to a valid URL."));
    return false;
  }
  // TODO(https://crbug.com/1271540): Validate against interest group owner
  // origin.
  output.bidding_url = bidding_url;
  return true;
}

bool CopyWasmHelperUrlFromIdlToMojo(const ExecutionContext& context,
                                    ExceptionState& exception_state,
                                    const AuctionAdInterestGroup& input,
                                    mojom::blink::InterestGroup& output) {
  if (!input.hasBiddingWasmHelperUrl())
    return true;
  KURL wasm_url = context.CompleteURL(input.biddingWasmHelperUrl());
  if (!wasm_url.IsValid()) {
    exception_state.ThrowTypeError(ErrorInvalidInterestGroup(
        input, "biddingWasmHelperUrl", input.biddingWasmHelperUrl(),
        "cannot be resolved to a valid URL."));
    return false;
  }
  // ValidateBlinkInterestGroup will checks whether this follows all the rules.
  output.bidding_wasm_helper_url = wasm_url;
  return true;
}

bool CopyDailyUpdateUrlFromIdlToMojo(const ExecutionContext& context,
                                     ExceptionState& exception_state,
                                     const AuctionAdInterestGroup& input,
                                     mojom::blink::InterestGroup& output) {
  if (!input.hasDailyUpdateUrl())
    return true;
  KURL daily_update_url = context.CompleteURL(input.dailyUpdateUrl());
  if (!daily_update_url.IsValid()) {
    exception_state.ThrowTypeError(ErrorInvalidInterestGroup(
        input, "dailyUpdateUrl", input.dailyUpdateUrl(),
        "cannot be resolved to a valid URL."));
    return false;
  }
  // TODO(https://crbug.com/1271540): Validate against interest group owner
  // origin.
  output.daily_update_url = daily_update_url;
  return true;
}

bool CopyTrustedBiddingSignalsUrlFromIdlToMojo(
    const ExecutionContext& context,
    ExceptionState& exception_state,
    const AuctionAdInterestGroup& input,
    mojom::blink::InterestGroup& output) {
  if (!input.hasTrustedBiddingSignalsUrl())
    return true;
  KURL trusted_bidding_signals_url =
      context.CompleteURL(input.trustedBiddingSignalsUrl());
  if (!trusted_bidding_signals_url.IsValid()) {
    exception_state.ThrowTypeError(ErrorInvalidInterestGroup(
        input, "trustedBiddingSignalsUrl", input.trustedBiddingSignalsUrl(),
        "cannot be resolved to a valid URL."));
    return false;
  }
  // TODO(https://crbug.com/1271540): Validate against interest group owner
  // origin.
  output.trusted_bidding_signals_url = trusted_bidding_signals_url;
  return true;
}

bool CopyTrustedBiddingSignalsKeysFromIdlToMojo(
    const AuctionAdInterestGroup& input,
    mojom::blink::InterestGroup& output) {
  if (!input.hasTrustedBiddingSignalsKeys())
    return true;
  output.trusted_bidding_signals_keys.emplace();
  for (const auto& key : input.trustedBiddingSignalsKeys()) {
    output.trusted_bidding_signals_keys->push_back(key);
  }
  return true;
}

bool CopyUserBiddingSignalsFromIdlToMojo(const ScriptState& script_state,
                                         ExceptionState& exception_state,
                                         const AuctionAdInterestGroup& input,
                                         mojom::blink::InterestGroup& output) {
  if (!input.hasUserBiddingSignals())
    return true;
  if (!Jsonify(script_state, input.userBiddingSignals().V8Value(),
               output.user_bidding_signals)) {
    exception_state.ThrowTypeError(
        ErrorInvalidInterestGroupJson(input, "userBiddingSignals"));
    return false;
  }

  return true;
}

bool CopyAdsFromIdlToMojo(const ExecutionContext& context,
                          const ScriptState& script_state,
                          ExceptionState& exception_state,
                          const AuctionAdInterestGroup& input,
                          mojom::blink::InterestGroup& output) {
  if (!input.hasAds())
    return true;
  output.ads.emplace();
  for (const auto& ad : input.ads()) {
    auto mojo_ad = mojom::blink::InterestGroupAd::New();
    KURL render_url = context.CompleteURL(ad->renderUrl());
    if (!render_url.IsValid()) {
      exception_state.ThrowTypeError(
          ErrorInvalidInterestGroup(input, "ad renderUrl", ad->renderUrl(),
                                    "cannot be resolved to a valid URL."));
      return false;
    }
    mojo_ad->render_url = render_url;
    if (ad->hasMetadata()) {
      if (!Jsonify(script_state, ad->metadata().V8Value(), mojo_ad->metadata)) {
        exception_state.ThrowTypeError(
            ErrorInvalidInterestGroupJson(input, "ad metadata"));
        return false;
      }
    }
    output.ads->push_back(std::move(mojo_ad));
  }
  return true;
}

bool CopyAdComponentsFromIdlToMojo(const ExecutionContext& context,
                                   const ScriptState& script_state,
                                   ExceptionState& exception_state,
                                   const AuctionAdInterestGroup& input,
                                   mojom::blink::InterestGroup& output) {
  if (!input.hasAdComponents())
    return true;
  output.ad_components.emplace();
  for (const auto& ad : input.adComponents()) {
    auto mojo_ad = mojom::blink::InterestGroupAd::New();
    KURL render_url = context.CompleteURL(ad->renderUrl());
    if (!render_url.IsValid()) {
      exception_state.ThrowTypeError(
          ErrorInvalidInterestGroup(input, "ad renderUrl", ad->renderUrl(),
                                    "cannot be resolved to a valid URL."));
      return false;
    }
    mojo_ad->render_url = render_url;
    if (ad->hasMetadata()) {
      if (!Jsonify(script_state, ad->metadata().V8Value(), mojo_ad->metadata)) {
        exception_state.ThrowTypeError(
            ErrorInvalidInterestGroupJson(input, "ad metadata"));
        return false;
      }
    }
    output.ad_components->push_back(std::move(mojo_ad));
  }
  return true;
}

// createAdRequest copy functions.
bool CopyAdRequestUrlFromIdlToMojo(const ExecutionContext& context,
                                   ExceptionState& exception_state,
                                   const AdRequestConfig& input,
                                   mojom::blink::AdRequestConfig& output) {
  KURL ad_request_url = context.CompleteURL(input.adRequestUrl());
  if (!ad_request_url.IsValid() ||
      (ad_request_url.Protocol() != url::kHttpsScheme)) {
    exception_state.ThrowTypeError(
        String::Format("adRequestUrl '%s' for AdRequestConfig must "
                       "be a valid https origin.",
                       input.adRequestUrl().Utf8().c_str()));
    return false;
  }
  output.ad_request_url = ad_request_url;
  return true;
}

bool CopyAdPropertiesFromIdlToMojo(const ExecutionContext& context,
                                   ExceptionState& exception_state,
                                   const AdRequestConfig& input,
                                   mojom::blink::AdRequestConfig& output) {
  if (!input.hasAdProperties()) {
    exception_state.ThrowTypeError(
        ErrorInvalidAdRequestConfig(input, "adProperties", input.adRequestUrl(),
                                    "must be provided to createAdRequest."));
    return false;
  }

  // output.ad_properties = mojom::blink::AdProperties::New();
  switch (input.adProperties()->GetContentType()) {
    case V8UnionAdPropertiesOrAdPropertiesSequence::ContentType::
        kAdProperties: {
      const auto* ad_properties = input.adProperties()->GetAsAdProperties();
      auto mojo_ad_properties = mojom::blink::AdProperties::New();
      mojo_ad_properties->width =
          ad_properties->hasWidth() ? ad_properties->width() : "";
      mojo_ad_properties->height =
          ad_properties->hasHeight() ? ad_properties->height() : "";
      mojo_ad_properties->slot =
          ad_properties->hasSlot() ? ad_properties->slot() : "";
      mojo_ad_properties->lang =
          ad_properties->hasLang() ? ad_properties->lang() : "";
      mojo_ad_properties->ad_type =
          ad_properties->hasAdtype() ? ad_properties->adtype() : "";
      mojo_ad_properties->bid_floor =
          ad_properties->hasBidFloor() ? ad_properties->bidFloor() : 0.0;

      output.ad_properties.push_back(std::move(mojo_ad_properties));
      break;
    }
    case V8UnionAdPropertiesOrAdPropertiesSequence::ContentType::
        kAdPropertiesSequence: {
      if (input.adProperties()->GetAsAdPropertiesSequence().size() <= 0) {
        exception_state.ThrowTypeError(ErrorInvalidAdRequestConfig(
            input, "adProperties", input.adRequestUrl(),
            "must be non-empty to createAdRequest."));
        return false;
      }

      for (const auto& ad_properties :
           input.adProperties()->GetAsAdPropertiesSequence()) {
        auto mojo_ad_properties = mojom::blink::AdProperties::New();
        mojo_ad_properties->width =
            ad_properties->hasWidth() ? ad_properties->width() : "";
        mojo_ad_properties->height =
            ad_properties->hasHeight() ? ad_properties->height() : "";
        mojo_ad_properties->slot =
            ad_properties->hasSlot() ? ad_properties->slot() : "";
        mojo_ad_properties->lang =
            ad_properties->hasLang() ? ad_properties->lang() : "";
        mojo_ad_properties->ad_type =
            ad_properties->hasAdtype() ? ad_properties->adtype() : "";
        mojo_ad_properties->bid_floor =
            ad_properties->hasBidFloor() ? ad_properties->bidFloor() : 0.0;

        output.ad_properties.push_back(std::move(mojo_ad_properties));
      }
      break;
    }
  }
  return true;
}

bool CopyTargetingFromIdlToMojo(const ExecutionContext& context,
                                ExceptionState& exception_state,
                                const AdRequestConfig& input,
                                mojom::blink::AdRequestConfig& output) {
  if (!input.hasTargeting()) {
    // Targeting information is not required.
    return true;
  }

  output.targeting = mojom::blink::AdTargeting::New();

  if (input.targeting()->hasInterests()) {
    output.targeting->interests.emplace();
    for (const auto& interest : input.targeting()->interests()) {
      output.targeting->interests->push_back(interest);
    }
  }

  if (input.targeting()->hasGeolocation()) {
    output.targeting->geolocation = mojom::blink::AdGeolocation::New();
    output.targeting->geolocation->latitude =
        input.targeting()->geolocation()->latitude();
    output.targeting->geolocation->longitude =
        input.targeting()->geolocation()->longitude();
  }

  return true;
}

bool CopyAdSignalsFromIdlToMojo(const ExecutionContext& context,
                                ExceptionState& exception_state,
                                const AdRequestConfig& input,
                                mojom::blink::AdRequestConfig& output) {
  if (!input.hasAnonymizedProxiedSignals()) {
    // AdSignals information is not required.
    return true;
  }

  output.anonymized_proxied_signals.emplace();

  for (const auto& signal : input.anonymizedProxiedSignals()) {
    if (signal == "coarse-geolocation") {
      output.anonymized_proxied_signals->push_back(
          blink::mojom::AdSignals::kCourseGeolocation);
    } else if (signal == "coarse-ua") {
      output.anonymized_proxied_signals->push_back(
          blink::mojom::AdSignals::kCourseUserAgent);
    } else if (signal == "targeting") {
      output.anonymized_proxied_signals->push_back(
          blink::mojom::AdSignals::kTargeting);
    } else if (signal == "user-ad-interests") {
      output.anonymized_proxied_signals->push_back(
          blink::mojom::AdSignals::kUserAdInterests);
    }
  }
  return true;
}

bool CopyFallbackSourceFromIdlToMojo(const ExecutionContext& context,
                                     ExceptionState& exception_state,
                                     const AdRequestConfig& input,
                                     mojom::blink::AdRequestConfig& output) {
  if (!input.hasFallbackSource()) {
    // FallbackSource information is not required.
    return true;
  }

  KURL fallback_source = context.CompleteURL(input.fallbackSource());
  if (!fallback_source.IsValid() ||
      (fallback_source.Protocol() != url::kHttpsScheme)) {
    exception_state.ThrowTypeError(
        String::Format("fallbackSource '%s' for AdRequestConfig must "
                       "be a valid https origin.",
                       input.fallbackSource().Utf8().c_str()));
    return false;
  }
  output.fallback_source = fallback_source;
  return true;
}

// runAdAuction() copy functions.

bool CopySellerFromIdlToMojo(ExceptionState& exception_state,
                             const AuctionAdConfig& input,
                             mojom::blink::AuctionAdConfig& output) {
  scoped_refptr<const SecurityOrigin> seller = ParseOrigin(input.seller());
  if (!seller) {
    exception_state.ThrowTypeError(String::Format(
        "seller '%s' for AuctionAdConfig must be a valid https origin.",
        input.seller().Utf8().c_str()));
    return false;
  }
  output.seller = seller;
  return true;
}

bool CopyDecisionLogicUrlFromIdlToMojo(const ExecutionContext& context,
                                       ExceptionState& exception_state,
                                       const AuctionAdConfig& input,
                                       mojom::blink::AuctionAdConfig& output) {
  KURL decision_logic_url = context.CompleteURL(input.decisionLogicUrl());
  if (!decision_logic_url.IsValid()) {
    exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
        input, "decisionLogicUrl", input.decisionLogicUrl(),
        "cannot be resolved to a valid URL."));
    return false;
  }

  // Need to check scheme of the URL in addition to comparing origins because
  // FLEDGE currently only supports HTTPS URLs, and some non-HTTPS URLs can have
  // HTTPS origins.
  if (decision_logic_url.Protocol() != url::kHttpsScheme ||
      !output.seller->IsSameOriginWith(
          SecurityOrigin::Create(decision_logic_url).get())) {
    exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
        input, "decisionLogicUrl", input.decisionLogicUrl(),
        "must match seller origin."));
    return false;
  }

  output.decision_logic_url = decision_logic_url;
  return true;
}

bool CopyTrustedScoringSignalsFromIdlToMojo(
    const ExecutionContext& context,
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    mojom::blink::AuctionAdConfig& output) {
  if (!input.hasTrustedScoringSignalsUrl())
    return true;
  KURL trusted_scoring_signals_url =
      context.CompleteURL(input.trustedScoringSignalsUrl());
  if (!trusted_scoring_signals_url.IsValid()) {
    exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
        input, "trustedScoringSignalsUrl", input.trustedScoringSignalsUrl(),
        "cannot be resolved to a valid URL."));
    return false;
  }

  // Need to check scheme of the URL in addition to comparing origins because
  // FLEDGE currently only supports HTTPS URLs, and some non-HTTPS URLs can have
  // HTTPS origins.
  if (trusted_scoring_signals_url.Protocol() != url::kHttpsScheme ||
      !output.seller->IsSameOriginWith(
          SecurityOrigin::Create(trusted_scoring_signals_url).get())) {
    exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
        input, "trustedScoringSignalsUrl", input.trustedScoringSignalsUrl(),
        "must match seller origin."));
    return false;
  }

  output.trusted_scoring_signals_url = trusted_scoring_signals_url;
  return true;
}

bool CopyInterestGroupBuyersFromIdlToMojo(
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    mojom::blink::AuctionAdConfig& output) {
  DCHECK(!output.auction_ad_config_non_shared_params->interest_group_buyers);

  if (!input.hasInterestGroupBuyers())
    return true;

  Vector<scoped_refptr<const SecurityOrigin>> buyers;
  for (const auto& buyer_str : input.interestGroupBuyers()) {
    scoped_refptr<const SecurityOrigin> buyer = ParseOrigin(buyer_str);
    if (!buyer) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "interestGroupBuyers buyer", buyer_str,
          "must be a valid https origin."));
      return false;
    }
    buyers.push_back(buyer);
  }
  output.auction_ad_config_non_shared_params->interest_group_buyers =
      std::move(buyers);
  return true;
}

bool CopyAuctionSignalsFromIdlToMojo(const ScriptState& script_state,
                                     ExceptionState& exception_state,
                                     const AuctionAdConfig& input,
                                     mojom::blink::AuctionAdConfig& output) {
  if (!input.hasAuctionSignals())
    return true;
  if (!Jsonify(script_state, input.auctionSignals().V8Value(),
               output.auction_ad_config_non_shared_params->auction_signals)) {
    exception_state.ThrowTypeError(
        ErrorInvalidAuctionConfigJson(input, "auctionSignals"));
    return false;
  }
  return true;
}

bool CopySellerSignalsFromIdlToMojo(const ScriptState& script_state,
                                    ExceptionState& exception_state,
                                    const AuctionAdConfig& input,
                                    mojom::blink::AuctionAdConfig& output) {
  if (!input.hasSellerSignals())
    return true;
  if (!Jsonify(script_state, input.sellerSignals().V8Value(),
               output.auction_ad_config_non_shared_params->seller_signals)) {
    exception_state.ThrowTypeError(
        ErrorInvalidAuctionConfigJson(input, "sellerSignals"));
    return false;
  }

  return true;
}

// Attempts to build a DirectFromSellerSignalsSubresource. If there is no
// registered subresource URL `subresource_url` returns nullptr -- processing
// may continue with the next `subresource_url`.
mojom::blink::DirectFromSellerSignalsSubresourcePtr
TryToBuildDirectFromSellerSignalsSubresource(
    const KURL& subresource_url,
    const SecurityOrigin& seller,
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    const ResourceFetcher& resource_fetcher) {
  DCHECK(subresource_url.IsValid());
  DCHECK(
      subresource_url.ProtocolIs(url::kHttpsScheme) &&
      seller.IsSameOriginWith(SecurityOrigin::Create(subresource_url).get()));
  // NOTE: If subresource bundles are disabled, GetSubresourceBundleToken() will
  // always return absl::nullopt.
  absl::optional<base::UnguessableToken> token =
      resource_fetcher.GetSubresourceBundleToken(subresource_url);
  if (!token)
    return nullptr;
  absl::optional<KURL> bundle_url =
      resource_fetcher.GetSubresourceBundleSourceUrl(subresource_url);
  DCHECK(bundle_url->ProtocolIs(url::kHttpsScheme));
  DCHECK(seller.IsSameOriginWith(SecurityOrigin::Create(*bundle_url).get()));
  auto mojo_bundle = mojom::blink::DirectFromSellerSignalsSubresource::New();
  mojo_bundle->token = *token;
  mojo_bundle->bundle_url = *bundle_url;
  return mojo_bundle;
}

bool CopyDirectFromSellerSignalsFromIdlToMojo(
    const ExecutionContext& context,
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    const ResourceFetcher& resource_fetcher,
    mojom::blink::AuctionAdConfig& output) {
  if (!input.hasDirectFromSellerSignals())
    return true;
  const KURL direct_from_seller_signals_prefix =
      context.CompleteURL(input.directFromSellerSignals());
  if (!direct_from_seller_signals_prefix.IsValid()) {
    exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
        input, "directFromSellerSignals", input.directFromSellerSignals(),
        "cannot be resolved to a valid URL."));
    return false;
  }
  if (!direct_from_seller_signals_prefix.ProtocolIs(url::kHttpsScheme) ||
      !output.seller->IsSameOriginWith(
          SecurityOrigin::Create(direct_from_seller_signals_prefix).get())) {
    exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
        input, "directFromSellerSignals", input.directFromSellerSignals(),
        "must match seller origin; only https scheme is supported."));
    return false;
  }
  if (!direct_from_seller_signals_prefix.Query().empty()) {
    exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
        input, "directFromSellerSignals", input.directFromSellerSignals(),
        "URL prefix must not have a query string."));
    return false;
  }
  auto mojo_direct_from_seller_signals =
      mojom::blink::DirectFromSellerSignals::New();
  mojo_direct_from_seller_signals->prefix = direct_from_seller_signals_prefix;

  for (scoped_refptr<const SecurityOrigin> buyer :
       *output.auction_ad_config_non_shared_params->interest_group_buyers) {
    // Replace "/" with "%2F" to match the behavior of
    // base::EscapeQueryParamValue(). Also, the subresource won't be found if
    // the URL doesn't match.
    const KURL subresource_url(
        direct_from_seller_signals_prefix.GetString() + "?perBuyerSignals=" +
        EncodeWithURLEscapeSequences(buyer->ToString()).Replace("/", "%2F"));
    mojom::blink::DirectFromSellerSignalsSubresourcePtr maybe_mojo_bundle =
        TryToBuildDirectFromSellerSignalsSubresource(
            subresource_url, *output.seller, exception_state, input,
            resource_fetcher);
    if (!maybe_mojo_bundle)
      continue;  // The bundle wasn't found, try the next one.
    mojo_direct_from_seller_signals->per_buyer_signals.insert(
        buyer, std::move(maybe_mojo_bundle));
  }

  {
    const KURL subresource_url(direct_from_seller_signals_prefix.GetString() +
                               "?sellerSignals");
    mojom::blink::DirectFromSellerSignalsSubresourcePtr maybe_mojo_bundle =
        TryToBuildDirectFromSellerSignalsSubresource(
            subresource_url, *output.seller, exception_state, input,
            resource_fetcher);
    // May be null if the signals weren't found.
    mojo_direct_from_seller_signals->seller_signals =
        std::move(maybe_mojo_bundle);
  }

  {
    const KURL subresource_url(direct_from_seller_signals_prefix.GetString() +
                               "?auctionSignals");
    mojom::blink::DirectFromSellerSignalsSubresourcePtr maybe_mojo_bundle =
        TryToBuildDirectFromSellerSignalsSubresource(
            subresource_url, *output.seller, exception_state, input,
            resource_fetcher);
    // May be null if the signals weren't found.
    mojo_direct_from_seller_signals->auction_signals =
        std::move(maybe_mojo_bundle);
  }

  output.direct_from_seller_signals =
      std::move(mojo_direct_from_seller_signals);
  return true;
}

bool CopyPerBuyerSignalsFromIdlToMojo(const ScriptState& script_state,
                                      ExceptionState& exception_state,
                                      const AuctionAdConfig& input,
                                      mojom::blink::AuctionAdConfig& output) {
  if (!input.hasPerBuyerSignals())
    return true;
  output.auction_ad_config_non_shared_params->per_buyer_signals.emplace();
  for (const auto& per_buyer_signal : input.perBuyerSignals()) {
    scoped_refptr<const SecurityOrigin> buyer =
        ParseOrigin(per_buyer_signal.first);
    if (!buyer) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "perBuyerSignals buyer", per_buyer_signal.first,
          "must be a valid https origin."));
      return false;
    }
    String buyer_signals_str;
    if (!Jsonify(script_state, per_buyer_signal.second.V8Value(),
                 buyer_signals_str)) {
      exception_state.ThrowTypeError(
          ErrorInvalidAuctionConfigJson(input, "perBuyerSignals"));
      return false;
    }
    output.auction_ad_config_non_shared_params->per_buyer_signals->insert(
        buyer, std::move(buyer_signals_str));
  }

  return true;
}

bool CopyPerBuyerTimeoutsFromIdlToMojo(const ScriptState& script_state,
                                       ExceptionState& exception_state,
                                       const AuctionAdConfig& input,
                                       mojom::blink::AuctionAdConfig& output) {
  if (!input.hasPerBuyerTimeouts())
    return true;
  output.auction_ad_config_non_shared_params->per_buyer_timeouts.emplace();
  for (const auto& per_buyer_timeout : input.perBuyerTimeouts()) {
    if (per_buyer_timeout.first == "*") {
      output.auction_ad_config_non_shared_params->all_buyers_timeout =
          base::Milliseconds(per_buyer_timeout.second);
      continue;
    }
    scoped_refptr<const SecurityOrigin> buyer =
        ParseOrigin(per_buyer_timeout.first);
    if (!buyer) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "perBuyerTimeouts buyer", per_buyer_timeout.first,
          "must be \"*\" (wildcard) or a valid https origin."));
      return false;
    }
    output.auction_ad_config_non_shared_params->per_buyer_timeouts->insert(
        buyer, base::Milliseconds(per_buyer_timeout.second));
  }

  return true;
}

bool CopyPerBuyerExperimentIdsFromIdlToMojo(
    const ScriptState& script_state,
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    mojom::blink::AuctionAdConfig& output) {
  if (!input.hasPerBuyerExperimentGroupIds())
    return true;
  for (const auto& per_buyer_experiment_id :
       input.perBuyerExperimentGroupIds()) {
    if (per_buyer_experiment_id.first == "*") {
      output.has_all_buyer_experiment_group_id = true;
      output.all_buyer_experiment_group_id = per_buyer_experiment_id.second;
      continue;
    }
    scoped_refptr<const SecurityOrigin> buyer =
        ParseOrigin(per_buyer_experiment_id.first);
    if (!buyer) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "perBuyerExperimentGroupIds buyer",
          per_buyer_experiment_id.first,
          "must be \"*\" (wildcard) or a valid https origin."));
      return false;
    }
    output.per_buyer_experiment_group_ids.insert(
        buyer, per_buyer_experiment_id.second);
  }

  return true;
}

bool CopyPerBuyerGroupLimitsFromIdlToMojo(
    const ScriptState& script_state,
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    mojom::blink::AuctionAdConfig& output) {
  if (!input.hasPerBuyerGroupLimits())
    return true;
  for (const auto& per_buyer_group_limit : input.perBuyerGroupLimits()) {
    if (per_buyer_group_limit.second <= 0) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "perBuyerGroupLimits value",
          String::Number(per_buyer_group_limit.second),
          "must be greater than 0."));
      return false;
    }
    if (per_buyer_group_limit.first == "*") {
      output.auction_ad_config_non_shared_params->all_buyers_group_limit =
          per_buyer_group_limit.second;
      continue;
    }
    scoped_refptr<const SecurityOrigin> buyer =
        ParseOrigin(per_buyer_group_limit.first);
    if (!buyer) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "perBuyerGroupLimits buyer", per_buyer_group_limit.first,
          "must be \"*\" (wildcard) or a valid https origin."));
      return false;
    }
    output.auction_ad_config_non_shared_params->per_buyer_group_limits.insert(
        buyer, per_buyer_group_limit.second);
  }

  return true;
}

bool ConvertAuctionConfigPrioritySignalsFromIdlToMojo(
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    const Vector<std::pair<WTF::String, double>>& priority_signals_in,
    WTF::HashMap<WTF::String, double>& priority_signals_out) {
  for (const auto& key_value_pair : priority_signals_in) {
    if (key_value_pair.first.StartsWith("browserSignals.")) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "perBuyerPrioritySignals key", key_value_pair.first,
          "must not start with reserved \"browserSignals.\" prefix."));
      return false;
    }
    priority_signals_out.insert(key_value_pair.first, key_value_pair.second);
  }
  return true;
}

bool CopyPerBuyerPrioritySignalsFromIdlToMojo(
    ExceptionState& exception_state,
    const AuctionAdConfig& input,
    mojom::blink::AuctionAdConfig& output) {
  if (!input.hasPerBuyerPrioritySignals())
    return true;

  output.auction_ad_config_non_shared_params->per_buyer_priority_signals
      .emplace();
  for (const auto& per_buyer_priority_signals :
       input.perBuyerPrioritySignals()) {
    WTF::HashMap<WTF::String, double> signals;
    if (!ConvertAuctionConfigPrioritySignalsFromIdlToMojo(
            exception_state, input, per_buyer_priority_signals.second,
            signals)) {
      return false;
    }
    if (per_buyer_priority_signals.first == "*") {
      output.auction_ad_config_non_shared_params->all_buyers_priority_signals =
          std::move(signals);
      continue;
    }
    scoped_refptr<const SecurityOrigin> buyer =
        ParseOrigin(per_buyer_priority_signals.first);
    if (!buyer) {
      exception_state.ThrowTypeError(ErrorInvalidAuctionConfig(
          input, "perBuyerPrioritySignals buyer",
          per_buyer_priority_signals.first,
          "must be \"*\" (wildcard) or a valid https origin."));
      return false;
    }
    output.auction_ad_config_non_shared_params->per_buyer_priority_signals
        ->insert(buyer, std::move(signals));
  }

  return true;
}

// Attempts to convert the AuctionAdConfig `config`, passed in via Javascript,
// to a `mojom::blink::AuctionAdConfig`. Throws a Javascript exception and
// return null on failure.
mojom::blink::AuctionAdConfigPtr IdlAuctionConfigToMojo(
    bool is_top_level,
    ScriptState& script_state,
    const ExecutionContext& context,
    ExceptionState& exception_state,
    const ResourceFetcher& resource_fetcher,
    const AuctionAdConfig& config) {
  auto mojo_config = mojom::blink::AuctionAdConfig::New();
  mojo_config->auction_ad_config_non_shared_params =
      mojom::blink::AuctionAdConfigNonSharedParams::New();
  if (!CopySellerFromIdlToMojo(exception_state, config, *mojo_config) ||
      !CopyDecisionLogicUrlFromIdlToMojo(context, exception_state, config,
                                         *mojo_config) ||
      !CopyTrustedScoringSignalsFromIdlToMojo(context, exception_state, config,
                                              *mojo_config) ||
      !CopyInterestGroupBuyersFromIdlToMojo(exception_state, config,
                                            *mojo_config) ||
      !CopyAuctionSignalsFromIdlToMojo(script_state, exception_state, config,
                                       *mojo_config) ||
      !CopySellerSignalsFromIdlToMojo(script_state, exception_state, config,
                                      *mojo_config) ||
      !CopyDirectFromSellerSignalsFromIdlToMojo(
          context, exception_state, config, resource_fetcher, *mojo_config) ||
      !CopyPerBuyerSignalsFromIdlToMojo(script_state, exception_state, config,
                                        *mojo_config) ||
      !CopyPerBuyerTimeoutsFromIdlToMojo(script_state, exception_state, config,
                                         *mojo_config) ||
      !CopyPerBuyerExperimentIdsFromIdlToMojo(script_state, exception_state,
                                              config, *mojo_config) ||
      !CopyPerBuyerGroupLimitsFromIdlToMojo(script_state, exception_state,
                                            config, *mojo_config) ||
      !CopyPerBuyerPrioritySignalsFromIdlToMojo(exception_state, config,
                                                *mojo_config)) {
    return mojom::blink::AuctionAdConfigPtr();
  }

  if (config.hasSellerTimeout()) {
    mojo_config->auction_ad_config_non_shared_params->seller_timeout =
        base::Milliseconds(config.sellerTimeout());
  }

  // Recursively handle component auctions, if there are any.
  if (config.hasComponentAuctions()) {
    for (const auto& idl_component_auction : config.componentAuctions()) {
      // Component auctions may not have their own nested component auctions.
      if (!is_top_level) {
        exception_state.ThrowTypeError(
            "Auctions listed in componentAuctions may not have their own "
            "nested componentAuctions.");
        return mojom::blink::AuctionAdConfigPtr();
      }

      auto mojo_component_auction = IdlAuctionConfigToMojo(
          /*is_top_level=*/false, script_state, context, exception_state,
          resource_fetcher, *idl_component_auction);
      if (!mojo_component_auction)
        return mojom::blink::AuctionAdConfigPtr();
      mojo_config->auction_ad_config_non_shared_params->component_auctions
          .emplace_back(std::move(mojo_component_auction));
    }
  }

  if (config.hasSellerExperimentGroupId()) {
    mojo_config->has_seller_experiment_group_id = true;
    mojo_config->seller_experiment_group_id = config.sellerExperimentGroupId();
  }

  return mojo_config;
}

// finalizeAd() validation methods
bool ValidateAdsObject(ExceptionState& exception_state, const Ads* ads) {
  if (!ads || !ads->IsValid()) {
    exception_state.ThrowTypeError(
        "Ads used for finalizeAds() must be a valid Ads object from "
        "navigator.createAdRequest.");
    return false;
  }
  return true;
}

// Modified from
// LocalFrame::CountUseIfFeatureWouldBeBlockedByPermissionsPolicy.
//
// Checks whether or not a policy-controlled feature would be blocked by our
// restricted permissions policy EnableForSelf.
// Under EnableForSelf policy, the features will not be available in
// cross-origin document unless explicitly enabled.
// Returns true if the frame is cross-origin relative to the top-level document,
// or if it is same-origin with the top level, but is embedded in any way
// through a cross-origin frame (A->B->A embedding).
bool FeatureWouldBeBlockedByRestrictedPermissionsPolicy(Navigator& navigator) {
  const Frame* frame = navigator.DomWindow()->GetFrame();

  // Fenced Frames block all permissions, so we shouldn't end up here because
  // the policy is checked before this method is called.
  DCHECK(!frame->IsInFencedFrameTree());

  // Get the origin of the top-level document.
  const SecurityOrigin* top_origin =
      frame->Tree().Top().GetSecurityContext()->GetSecurityOrigin();

  // Walk up the frame tree looking for any cross-origin embeds. Even if this
  // frame is same-origin with the top-level, if it is embedded by a cross-
  // origin frame (like A->B->A) it would be blocked without a permissions
  // policy.
  while (!frame->IsMainFrame()) {
    if (!frame->GetSecurityContext()->GetSecurityOrigin()->CanAccess(
            top_origin)) {
      return true;
    }
    frame = frame->Tree().Parent();
  }
  return false;
}

void AddWarningMessageToConsole(ScriptState* script_state,
                                const String& feature,
                                const String& api) {
  auto* window = To<LocalDOMWindow>(ExecutionContext::From(script_state));
  WebLocalFrameImpl::FromFrame(window->GetFrame())
      ->AddMessageToConsole(
          WebConsoleMessage(mojom::ConsoleMessageLevel::kWarning,
                            WarningPermissionsPolicy(feature, api)),
          /*discard_duplicates=*/true);
}

void RecordCommonFledgeUseCounters(Document* document) {
  if (!document)
    return;
  UseCounter::Count(document, mojom::blink::WebFeature::kFledge);
  // Only record the ads APIs counter if enabled in that manner.
  if (RuntimeEnabledFeatures::PrivacySandboxAdsAPIsEnabled(
          document->GetExecutionContext())) {
    UseCounter::Count(document,
                      mojom::blink::WebFeature::kPrivacySandboxAdsAPIs);
  }
}

}  // namespace

// Helper to manage runtime of abort pipe and to connect it to AbortController.
class NavigatorAuction::AuctionHandle final : public AbortSignal::Algorithm {
 public:
  AuctionHandle(ExecutionContext* context,
                mojo::PendingRemote<mojom::blink::AbortableAdAuction> remote)
      : abortable_ad_auction_(context) {
    abortable_ad_auction_.Bind(
        std::move(remote), context->GetTaskRunner(TaskType::kMiscPlatformAPI));
  }

  ~AuctionHandle() override = default;

  // AbortSignal::Algorithm implementation:
  void Run() override { abortable_ad_auction_->Abort(); }

  void Trace(Visitor* visitor) const override {
    visitor->Trace(abortable_ad_auction_);
    AbortSignal::Algorithm::Trace(visitor);
  }

 private:
  HeapMojoRemote<mojom::blink::AbortableAdAuction> abortable_ad_auction_;
};

NavigatorAuction::NavigatorAuction(Navigator& navigator)
    : Supplement(navigator),
      queued_cross_site_joins_(kMaxActiveCrossSiteJoins,
                               WTF::BindRepeating(&NavigatorAuction::StartJoin,
                                                  WrapWeakPersistent(this))),
      queued_cross_site_leaves_(
          kMaxActiveCrossSiteLeaves,
          WTF::BindRepeating(&NavigatorAuction::StartLeave,
                             WrapWeakPersistent(this))),
      ad_auction_service_(navigator.GetExecutionContext()) {
  navigator.GetExecutionContext()->GetBrowserInterfaceBroker().GetInterface(
      ad_auction_service_.BindNewPipeAndPassReceiver(
          navigator.GetExecutionContext()->GetTaskRunner(
              TaskType::kMiscPlatformAPI)));
}

NavigatorAuction& NavigatorAuction::From(ExecutionContext* context,
                                         Navigator& navigator) {
  NavigatorAuction* supplement =
      Supplement<Navigator>::From<NavigatorAuction>(navigator);
  if (!supplement) {
    supplement = MakeGarbageCollected<NavigatorAuction>(navigator);
    ProvideTo(navigator, supplement);
  }
  return *supplement;
}

const char NavigatorAuction::kSupplementName[] = "NavigatorAuction";

ScriptPromise NavigatorAuction::joinAdInterestGroup(
    ScriptState* script_state,
    const AuctionAdInterestGroup* group,
    double duration_seconds,
    ExceptionState& exception_state) {
  const ExecutionContext* context = ExecutionContext::From(script_state);

  auto mojo_group = mojom::blink::InterestGroup::New();
  mojo_group->expiry = base::Time::Now() + base::Seconds(duration_seconds);
  if (!CopyOwnerFromIdlToMojo(*context, exception_state, *group, *mojo_group))
    return ScriptPromise();
  mojo_group->name = group->name();
  mojo_group->priority = (group->hasPriority()) ? group->priority() : 0.0;

  mojo_group->enable_bidding_signals_prioritization =
      group->hasEnableBiddingSignalsPrioritization()
          ? group->enableBiddingSignalsPrioritization()
          : false;
  if (group->hasPriorityVector()) {
    mojo_group->priority_vector =
        ConvertSparseVectorIdlToMojo(group->priorityVector());
  }
  if (group->hasPrioritySignalsOverrides()) {
    mojo_group->priority_signals_overrides =
        ConvertSparseVectorIdlToMojo(group->prioritySignalsOverrides());
  }

  if (!CopyExecutionModeFromIdlToMojo(*context, exception_state, *group,
                                      *mojo_group)) {
    return ScriptPromise();
  }
  if (!CopyBiddingLogicUrlFromIdlToMojo(*context, exception_state, *group,
                                        *mojo_group)) {
    return ScriptPromise();
  }
  if (!CopyWasmHelperUrlFromIdlToMojo(*context, exception_state, *group,
                                      *mojo_group)) {
    return ScriptPromise();
  }
  if (!CopyDailyUpdateUrlFromIdlToMojo(*context, exception_state, *group,
                                       *mojo_group)) {
    return ScriptPromise();
  }
  if (!CopyTrustedBiddingSignalsUrlFromIdlToMojo(*context, exception_state,
                                                 *group, *mojo_group)) {
    return ScriptPromise();
  }
  if (!CopyTrustedBiddingSignalsKeysFromIdlToMojo(*group, *mojo_group))
    return ScriptPromise();
  if (!CopyUserBiddingSignalsFromIdlToMojo(*script_state, exception_state,
                                           *group, *mojo_group)) {
    return ScriptPromise();
  }
  if (!CopyAdsFromIdlToMojo(*context, *script_state, exception_state, *group,
                            *mojo_group)) {
    return ScriptPromise();
  }
  if (!CopyAdComponentsFromIdlToMojo(*context, *script_state, exception_state,
                                     *group, *mojo_group)) {
    return ScriptPromise();
  }

  String error_field_name;
  String error_field_value;
  String error;
  if (!ValidateBlinkInterestGroup(*mojo_group, error_field_name,
                                  error_field_value, error)) {
    exception_state.ThrowTypeError(ErrorInvalidInterestGroup(
        *group, error_field_name, error_field_value, error));
    return ScriptPromise();
  }

  bool is_cross_origin =
      !context->GetSecurityOrigin()->IsSameOriginWith(mojo_group->owner.get());

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise promise = resolver->Promise();
  mojom::blink::AdAuctionService::JoinInterestGroupCallback callback =
      resolver->WrapCallbackInScriptScope(
          WTF::BindOnce(&NavigatorAuction::JoinComplete,
                        WrapWeakPersistent(this), is_cross_origin));

  PendingJoin pending_join{std::move(mojo_group), std::move(callback)};
  if (is_cross_origin) {
    queued_cross_site_joins_.Enqueue(std::move(pending_join));
  } else {
    StartJoin(std::move(pending_join));
  }

  return promise;
}

/* static */
ScriptPromise NavigatorAuction::joinAdInterestGroup(
    ScriptState* script_state,
    Navigator& navigator,
    const AuctionAdInterestGroup* group,
    double duration_seconds,
    ExceptionState& exception_state) {
  RecordCommonFledgeUseCounters(navigator.DomWindow()->document());
  const ExecutionContext* context = ExecutionContext::From(script_state);
  if (!context->IsFeatureEnabled(
          blink::mojom::PermissionsPolicyFeature::kJoinAdInterestGroup)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "Feature join-ad-interest-group is not enabled by Permissions Policy");
    return ScriptPromise();
  }
  if (!base::FeatureList::IsEnabled(
          blink::features::kAdInterestGroupAPIRestrictedPolicyByDefault) &&
      FeatureWouldBeBlockedByRestrictedPermissionsPolicy(navigator)) {
    AddWarningMessageToConsole(script_state, "join-ad-interest-group",
                               "joinAdInterestGroup");
  }

  return From(ExecutionContext::From(script_state), navigator)
      .joinAdInterestGroup(script_state, group, duration_seconds,
                           exception_state);
}

ScriptPromise NavigatorAuction::leaveAdInterestGroup(
    ScriptState* script_state,
    const AuctionAdInterestGroup* group,
    ExceptionState& exception_state) {
  scoped_refptr<const SecurityOrigin> owner = ParseOrigin(group->owner());
  if (!owner) {
    exception_state.ThrowTypeError("owner '" + group->owner() +
                                   "' for AuctionAdInterestGroup with name '" +
                                   group->name() +
                                   "' must be a valid https origin.");
    return ScriptPromise();
  }

  bool is_cross_origin = !ExecutionContext::From(script_state)
                              ->GetSecurityOrigin()
                              ->IsSameOriginWith(owner.get());

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise promise = resolver->Promise();
  mojom::blink::AdAuctionService::LeaveInterestGroupCallback callback =
      resolver->WrapCallbackInScriptScope(
          WTF::BindOnce(&NavigatorAuction::LeaveComplete,
                        WrapWeakPersistent(this), is_cross_origin));

  PendingLeave pending_leave{std::move(owner), std::move(group->name()),
                             std::move(callback)};
  if (is_cross_origin) {
    queued_cross_site_leaves_.Enqueue(std::move(pending_leave));
  } else {
    StartLeave(std::move(pending_leave));
  }

  return promise;
}

ScriptPromise NavigatorAuction::leaveAdInterestGroupForDocument(
    ScriptState* script_state,
    ExceptionState& exception_state) {
  LocalDOMWindow* window = GetSupplementable()->DomWindow();

  if (!window) {
    exception_state.ThrowSecurityError(
        "May not leaveAdInterestGroup from a Document that is not fully "
        "active");
    return ScriptPromise();
  }
  if (!window->GetFrame()->IsInFencedFrameTree()) {
    exception_state.ThrowTypeError(
        "owner and name are required outside of a fenced frame.");
    return ScriptPromise();
  }
  // The renderer does not have enough information to verify that this document
  // is the result of a FLEDGE auction. The browser will silently ignore
  // this request if this document is not the result of a FLEDGE auction.
  ad_auction_service_->LeaveInterestGroupForDocument();

  // Return resolved promise. The browser-side code doesn't do anything
  // meaningful in this case (no .well-known fetches), and if it ever does do
  // them, likely don't want to expose timing information to the fenced frame,
  // anyways.
  return ScriptPromise::CastUndefined(script_state);
}

/* static */
ScriptPromise NavigatorAuction::leaveAdInterestGroup(
    ScriptState* script_state,
    Navigator& navigator,
    const AuctionAdInterestGroup* group,
    ExceptionState& exception_state) {
  RecordCommonFledgeUseCounters(navigator.DomWindow()->document());
  ExecutionContext* context = ExecutionContext::From(script_state);
  if (!context->IsFeatureEnabled(
          blink::mojom::PermissionsPolicyFeature::kJoinAdInterestGroup)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "Feature join-ad-interest-group is not enabled by Permissions Policy");
    return ScriptPromise();
  }
  if (!base::FeatureList::IsEnabled(
          blink::features::kAdInterestGroupAPIRestrictedPolicyByDefault) &&
      FeatureWouldBeBlockedByRestrictedPermissionsPolicy(navigator)) {
    AddWarningMessageToConsole(script_state, "join-ad-interest-group",
                               "leaveAdInterestGroup");
  }

  return From(context, navigator)
      .leaveAdInterestGroup(script_state, group, exception_state);
}

/* static */
ScriptPromise NavigatorAuction::leaveAdInterestGroup(
    ScriptState* script_state,
    Navigator& navigator,
    ExceptionState& exception_state) {
  ExecutionContext* context = ExecutionContext::From(script_state);
  // According to the spec, implicit leave bypasses permission policy.
  return From(context, navigator)
      .leaveAdInterestGroupForDocument(script_state, exception_state);
}

void NavigatorAuction::updateAdInterestGroups() {
  ad_auction_service_->UpdateAdInterestGroups();
}

/* static */
void NavigatorAuction::updateAdInterestGroups(ScriptState* script_state,
                                              Navigator& navigator,
                                              ExceptionState& exception_state) {
  RecordCommonFledgeUseCounters(navigator.DomWindow()->document());
  ExecutionContext* context = ExecutionContext::From(script_state);
  if (!context->IsFeatureEnabled(
          blink::mojom::PermissionsPolicyFeature::kJoinAdInterestGroup)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "Feature join-ad-interest-group is not enabled by Permissions Policy");
    return;
  }
  if (!base::FeatureList::IsEnabled(
          blink::features::kAdInterestGroupAPIRestrictedPolicyByDefault) &&
      FeatureWouldBeBlockedByRestrictedPermissionsPolicy(navigator)) {
    AddWarningMessageToConsole(script_state, "join-ad-interest-group",
                               "updateAdInterestGroups");
  }

  return From(context, navigator).updateAdInterestGroups();
}

ScriptPromise NavigatorAuction::runAdAuction(ScriptState* script_state,
                                             const AuctionAdConfig* config,
                                             ExceptionState& exception_state) {
  ExecutionContext* context = ExecutionContext::From(script_state);
  auto mojo_config = IdlAuctionConfigToMojo(
      /*is_top_level=*/true, *script_state, *context, exception_state,
      /*resource_fetcher=*/
      *GetSupplementable()->DomWindow()->document()->Fetcher(), *config);
  if (!mojo_config)
    return ScriptPromise();

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  mojo::PendingReceiver<mojom::blink::AbortableAdAuction> abort_receiver;
  ScriptPromise promise = resolver->Promise();
  std::unique_ptr<ScopedAbortState> scoped_abort_state = nullptr;
  if (auto* signal = config->getSignalOr(nullptr)) {
    if (signal->aborted()) {
      resolver->Reject(signal->reason(script_state));
      return promise;
    }
    auto* auction_handle = MakeGarbageCollected<AuctionHandle>(
        context, abort_receiver.InitWithNewPipeAndPassRemote());
    auto* abort_handle = signal->AddAlgorithm(auction_handle);
    scoped_abort_state =
        std::make_unique<ScopedAbortState>(signal, abort_handle);
  }
  ad_auction_service_->RunAdAuction(
      std::move(mojo_config), std::move(abort_receiver),
      WTF::BindOnce(&NavigatorAuction::AuctionComplete, WrapPersistent(this),
                    WrapPersistent(resolver), std::move(scoped_abort_state)));
  return promise;
}

/* static */
ScriptPromise NavigatorAuction::runAdAuction(ScriptState* script_state,
                                             Navigator& navigator,
                                             const AuctionAdConfig* config,
                                             ExceptionState& exception_state) {
  RecordCommonFledgeUseCounters(navigator.DomWindow()->document());
  const ExecutionContext* context = ExecutionContext::From(script_state);
  if (!context->IsFeatureEnabled(
          blink::mojom::PermissionsPolicyFeature::kRunAdAuction)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotAllowedError,
        "Feature run-ad-auction is not enabled by Permissions Policy");
    return ScriptPromise();
  }
  if (!base::FeatureList::IsEnabled(
          blink::features::kAdInterestGroupAPIRestrictedPolicyByDefault) &&
      FeatureWouldBeBlockedByRestrictedPermissionsPolicy(navigator)) {
    AddWarningMessageToConsole(script_state, "run-ad-auction", "runAdAuction");
  }

  return From(ExecutionContext::From(script_state), navigator)
      .runAdAuction(script_state, config, exception_state);
}

/* static */
Vector<String> NavigatorAuction::adAuctionComponents(
    ScriptState* script_state,
    Navigator& navigator,
    uint16_t num_ad_components,
    ExceptionState& exception_state) {
  RecordCommonFledgeUseCounters(navigator.DomWindow()->document());
  const auto& ad_auction_components =
      navigator.DomWindow()->document()->Loader()->AdAuctionComponents();
  Vector<String> out;
  if (!ad_auction_components) {
    exception_state.ThrowDOMException(DOMExceptionCode::kInvalidStateError,
                                      "This frame was not loaded with the "
                                      "result of an interest group auction.");
    return out;
  }

  // Clamp the number of ad components at blink::kMaxAdAuctionAdComponents.
  if (num_ad_components >
      static_cast<int16_t>(blink::kMaxAdAuctionAdComponents)) {
    num_ad_components = blink::kMaxAdAuctionAdComponents;
  }

  DCHECK_EQ(kMaxAdAuctionAdComponents, ad_auction_components->size());

  for (int i = 0; i < num_ad_components; ++i) {
    out.push_back((*ad_auction_components)[i].GetString());
  }
  return out;
}

ScriptPromise NavigatorAuction::deprecatedURNToURL(
    ScriptState* script_state,
    const String& uuid_url_string,
    bool send_reports,
    ExceptionState& exception_state) {
  KURL uuid_url(uuid_url_string);
  if (!blink::IsValidUrnUuidURL(GURL(uuid_url))) {
    exception_state.ThrowTypeError("Passed URL must be a valid URN URL.");
    return ScriptPromise();
  }

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise promise = resolver->Promise();
  ad_auction_service_->DeprecatedGetURLFromURN(
      std::move(uuid_url), send_reports,
      resolver->WrapCallbackInScriptScope(WTF::BindOnce(
          &NavigatorAuction::GetURLFromURNComplete, WrapPersistent(this))));
  return promise;
}

ScriptPromise NavigatorAuction::deprecatedURNToURL(
    ScriptState* script_state,
    Navigator& navigator,
    const String& uuid_url,
    bool send_reports,
    ExceptionState& exception_state) {
  return From(ExecutionContext::From(script_state), navigator)
      .deprecatedURNToURL(script_state, uuid_url, send_reports,
                          exception_state);
}

ScriptPromise NavigatorAuction::deprecatedReplaceInURN(
    ScriptState* script_state,
    const String& uuid_url_string,
    const Vector<std::pair<String, String>>& replacements,
    ExceptionState& exception_state) {
  KURL uuid_url(uuid_url_string);
  if (!blink::IsValidUrnUuidURL(GURL(uuid_url))) {
    exception_state.ThrowTypeError("Passed URL must be a valid URN URL.");
    return ScriptPromise();
  }
  Vector<mojom::blink::ReplacementPtr> replacements_list;
  for (const auto& replacement : replacements) {
    if (!(replacement.first.StartsWith("${") &&
          replacement.first.EndsWith("}")) &&
        !(replacement.first.StartsWith("%%") &&
          replacement.first.EndsWith("%%"))) {
      exception_state.ThrowTypeError(
          "Replacements must be of the form '${...}' or '%%...%%'");
      return ScriptPromise();
    }
    replacements_list.push_back(
        mojom::blink::Replacement::New(replacement.first, replacement.second));
  }
  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise promise = resolver->Promise();
  ad_auction_service_->DeprecatedReplaceInURN(
      std::move(uuid_url), std::move(replacements_list),
      resolver->WrapCallbackInScriptScope(WTF::BindOnce(
          &NavigatorAuction::ReplaceInURNComplete, WrapPersistent(this))));
  return promise;
}

ScriptPromise NavigatorAuction::deprecatedReplaceInURN(
    ScriptState* script_state,
    Navigator& navigator,
    const String& uuid_url_string,
    const Vector<std::pair<String, String>>& replacements,
    ExceptionState& exception_state) {
  return From(ExecutionContext::From(script_state), navigator)
      .deprecatedReplaceInURN(script_state, uuid_url_string,
                              std::move(replacements), exception_state);
}

ScriptPromise NavigatorAuction::createAdRequest(
    ScriptState* script_state,
    const AdRequestConfig* config,
    ExceptionState& exception_state) {
  const ExecutionContext* context = ExecutionContext::From(script_state);
  auto mojo_config = mojom::blink::AdRequestConfig::New();

  if (!CopyAdRequestUrlFromIdlToMojo(*context, exception_state, *config,
                                     *mojo_config))
    return ScriptPromise();

  if (!CopyAdPropertiesFromIdlToMojo(*context, exception_state, *config,
                                     *mojo_config))
    return ScriptPromise();

  if (config->hasPublisherCode()) {
    mojo_config->publisher_code = config->publisherCode();
  }

  if (!CopyTargetingFromIdlToMojo(*context, exception_state, *config,
                                  *mojo_config))
    return ScriptPromise();

  if (!CopyAdSignalsFromIdlToMojo(*context, exception_state, *config,
                                  *mojo_config))
    return ScriptPromise();

  if (!CopyFallbackSourceFromIdlToMojo(*context, exception_state, *config,
                                       *mojo_config))
    return ScriptPromise();

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise promise = resolver->Promise();
  ad_auction_service_->CreateAdRequest(
      std::move(mojo_config),
      resolver->WrapCallbackInScriptScope(WTF::BindOnce(
          &NavigatorAuction::AdsRequested, WrapPersistent(this))));
  return promise;
}

/* static */
ScriptPromise NavigatorAuction::createAdRequest(
    ScriptState* script_state,
    Navigator& navigator,
    const AdRequestConfig* config,
    ExceptionState& exception_state) {
  return From(ExecutionContext::From(script_state), navigator)
      .createAdRequest(script_state, config, exception_state);
}

void NavigatorAuction::AdsRequested(ScriptPromiseResolver* resolver,
                                    const WTF::String&) {
  // TODO(https://crbug.com/1249186): Add full impl of methods.
  resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
      resolver->GetScriptState()->GetIsolate(),
      DOMExceptionCode::kNotSupportedError,
      "createAdRequest API not yet implemented"));
}

ScriptPromise NavigatorAuction::finalizeAd(ScriptState* script_state,
                                           const Ads* ads,
                                           const AuctionAdConfig* config,
                                           ExceptionState& exception_state) {
  const ExecutionContext* context = ExecutionContext::From(script_state);
  auto mojo_config = mojom::blink::AuctionAdConfig::New();

  // For finalizing an Ad PARAKEET only really cares about the decisionLogicUrl,
  // auctionSignals, sellerSignals, and perBuyerSignals. Also need seller, since
  // it's used to validate the decision logic URL. We can ignore
  // copying/validating other fields on AuctionAdConfig.
  if (!CopySellerFromIdlToMojo(exception_state, *config, *mojo_config) ||
      !CopyDecisionLogicUrlFromIdlToMojo(*context, exception_state, *config,
                                         *mojo_config) ||
      !CopyAuctionSignalsFromIdlToMojo(*script_state, exception_state, *config,
                                       *mojo_config) ||
      !CopySellerSignalsFromIdlToMojo(*script_state, exception_state, *config,
                                      *mojo_config) ||
      !CopyPerBuyerSignalsFromIdlToMojo(*script_state, exception_state, *config,
                                        *mojo_config)) {
    return ScriptPromise();
  }

  if (!ValidateAdsObject(exception_state, ads))
    return ScriptPromise();

  auto* resolver = MakeGarbageCollected<ScriptPromiseResolver>(script_state);
  ScriptPromise promise = resolver->Promise();
  ad_auction_service_->FinalizeAd(
      ads->GetGuid(), std::move(mojo_config),
      resolver->WrapCallbackInScriptScope(WTF::BindOnce(
          &NavigatorAuction::FinalizeAdComplete, WrapPersistent(this))));
  return promise;
}

/* static */
ScriptPromise NavigatorAuction::finalizeAd(ScriptState* script_state,
                                           Navigator& navigator,
                                           const Ads* ads,
                                           const AuctionAdConfig* config,
                                           ExceptionState& exception_state) {
  return From(ExecutionContext::From(script_state), navigator)
      .finalizeAd(script_state, ads, config, exception_state);
}

void NavigatorAuction::FinalizeAdComplete(
    ScriptPromiseResolver* resolver,
    const absl::optional<KURL>& creative_url) {
  if (creative_url) {
    resolver->Resolve(creative_url);
  } else {
    // TODO(https://crbug.com/1249186): Add full impl of methods.
    resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
        resolver->GetScriptState()->GetIsolate(),
        DOMExceptionCode::kNotSupportedError,
        "finalizeAd API not yet implemented"));
  }
}

void NavigatorAuction::StartJoin(PendingJoin&& pending_join) {
  ad_auction_service_->JoinInterestGroup(std::move(pending_join.interest_group),
                                         std::move(pending_join.callback));
}

void NavigatorAuction::JoinComplete(bool is_cross_origin,
                                    ScriptPromiseResolver* resolver,
                                    bool failed_well_known_check) {
  if (is_cross_origin)
    queued_cross_site_joins_.OnComplete();

  if (failed_well_known_check) {
    resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
        resolver->GetScriptState()->GetIsolate(),
        DOMExceptionCode::kNotAllowedError,
        "Permission to join interest group denied."));
    return;
  }
  resolver->Resolve();
}

void NavigatorAuction::StartLeave(PendingLeave&& pending_leave) {
  ad_auction_service_->LeaveInterestGroup(pending_leave.owner,
                                          pending_leave.name,
                                          std::move(pending_leave.callback));
}

void NavigatorAuction::LeaveComplete(bool is_cross_origin,
                                     ScriptPromiseResolver* resolver,
                                     bool failed_well_known_check) {
  if (is_cross_origin)
    queued_cross_site_leaves_.OnComplete();

  if (failed_well_known_check) {
    resolver->Reject(V8ThrowDOMException::CreateOrEmpty(
        resolver->GetScriptState()->GetIsolate(),
        DOMExceptionCode::kNotAllowedError,
        "Permission to leave interest group denied."));
    return;
  }
  resolver->Resolve();
}

void NavigatorAuction::AuctionComplete(
    ScriptPromiseResolver* resolver,
    std::unique_ptr<ScopedAbortState> scoped_abort_state,
    bool manually_aborted,
    const absl::optional<KURL>& result_url) {
  if (!resolver->GetExecutionContext() ||
      resolver->GetExecutionContext()->IsContextDestroyed())
    return;
  AbortSignal* abort_signal =
      scoped_abort_state ? scoped_abort_state->Signal() : nullptr;
  ScriptState* script_state = resolver->GetScriptState();
  ScriptState::Scope script_state_scope(script_state);
  if (manually_aborted) {
    DCHECK(abort_signal && abort_signal->aborted());
    resolver->Reject(abort_signal->reason(script_state));
  } else if (result_url) {
    resolver->Resolve(result_url);
  } else {
    resolver->Resolve(v8::Null(script_state->GetIsolate()));
  }
}

void NavigatorAuction::GetURLFromURNComplete(
    ScriptPromiseResolver* resolver,
    const absl::optional<KURL>& decoded_url) {
  if (decoded_url) {
    resolver->Resolve(*decoded_url);
  } else {
    resolver->Resolve(v8::Null(resolver->GetScriptState()->GetIsolate()));
  }
}

void NavigatorAuction::ReplaceInURNComplete(ScriptPromiseResolver* resolver) {
  resolver->Resolve();
}

}  // namespace blink
