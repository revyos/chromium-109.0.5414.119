// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/preloading/prerender/prerender_host_registry.h"

#include "base/callback_helpers.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/metrics/field_trial_params.h"
#include "base/notreached.h"
#include "base/observer_list.h"
#include "base/system/sys_info.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/trace_event/trace_conversion_helper.h"
#include "build/build_config.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/preloading/preloading_attempt_impl.h"
#include "content/browser/preloading/prerender/prerender_final_status.h"
#include "content/browser/preloading/prerender/prerender_metrics.h"
#include "content/browser/preloading/prerender/prerender_navigation_utils.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/content_client.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/memory_instrumentation.h"
#include "third_party/blink/public/common/features.h"

namespace content {

namespace {

bool DeviceHasEnoughMemoryForPrerender() {
  // This method disallows prerendering on low-end devices if the
  // kPrerender2MemoryControls feature is enabled.
  if (!base::FeatureList::IsEnabled(blink::features::kPrerender2MemoryControls))
    return true;

  // Use the same default threshold as the back/forward cache. See comments in
  // DeviceHasEnoughMemoryForBackForwardCache().
  static constexpr int kDefaultMemoryThresholdMb =
#if BUILDFLAG(IS_ANDROID)
      1700;
#else
      0;
#endif

  // The default is overridable by field trial param.
  int memory_threshold_mb = base::GetFieldTrialParamByFeatureAsInt(
      blink::features::kPrerender2MemoryControls,
      blink::features::kPrerender2MemoryThresholdParamName,
      kDefaultMemoryThresholdMb);

  return base::SysInfo::AmountOfPhysicalMemoryMB() > memory_threshold_mb;
}

PreloadingFailureReason ToPreloadingFailureReason(PrerenderFinalStatus status) {
  return static_cast<PreloadingFailureReason>(
      static_cast<int>(status) +
      static_cast<int>(
          PreloadingFailureReason::kPreloadingFailureReasonCommonEnd));
}

}  // namespace

PrerenderHostRegistry::PrerenderHostRegistry(WebContents& web_contents) {
  Observe(&web_contents);
  DCHECK(blink::features::IsPrerender2Enabled());
}

PrerenderHostRegistry::~PrerenderHostRegistry() {
  Observe(nullptr);
  for (Observer& obs : observers_)
    obs.OnRegistryDestroyed();
}

void PrerenderHostRegistry::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PrerenderHostRegistry::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

int PrerenderHostRegistry::CreateAndStartHost(
    const PrerenderAttributes& attributes,
    WebContents& web_contents,
    PreloadingAttempt* attempt) {
  std::string recorded_url =
      attributes.initiator_origin.has_value()
          ? attributes.initiator_origin.value().GetURL().spec()
          : "(empty_url)";

  TRACE_EVENT2("navigation", "PrerenderHostRegistry::CreateAndStartHost",
               "attributes", attributes, "initiator_origin", recorded_url);

  int frame_tree_node_id = RenderFrameHost::kNoFrameTreeNodeId;

  {
    // Ensure observers are notified that a trigger occurred.
    base::ScopedClosureRunner notify_trigger(
        base::BindOnce(&PrerenderHostRegistry::NotifyTrigger,
                       base::Unretained(this), attributes.prerendering_url));

    // Check whether preloading is enabled. If users disable this
    // setting, it means users do not want to preload pages.
    WebContentsImpl& web_contents_impl =
        static_cast<WebContentsImpl&>(web_contents);
    if (web_contents_impl.IsPrerender2Disabled()) {
      if (attempt)
        attempt->SetEligibility(PreloadingEligibility::kPreloadingDisabled);
      return RenderFrameHost::kNoFrameTreeNodeId;
    }

    // Don't prerender when the trigger is in the background.
    if (web_contents.GetVisibility() == Visibility::HIDDEN) {
      RecordPrerenderFinalStatus(PrerenderFinalStatus::kTriggerBackgrounded,
                                 attributes, ukm::kInvalidSourceId);
      if (attempt)
        attempt->SetEligibility(PreloadingEligibility::kHidden);
      return RenderFrameHost::kNoFrameTreeNodeId;
    }

    // Don't prerender on low-end devices.
    // TODO(https://crbug.com/1176120): Fallback to NoStatePrefetch
    // since the memory requirements are different.
    if (!DeviceHasEnoughMemoryForPrerender()) {
      RecordPrerenderFinalStatus(PrerenderFinalStatus::kLowEndDevice,
                                 attributes, ukm::kInvalidSourceId);
      if (attempt)
        attempt->SetEligibility(PreloadingEligibility::kLowMemory);
      return RenderFrameHost::kNoFrameTreeNodeId;
    }

    // Don't prerender when the Data Saver setting is enabled.
    if (GetContentClient()->browser()->IsDataSaverEnabled(
            web_contents.GetBrowserContext())) {
      RecordPrerenderFinalStatus(PrerenderFinalStatus::kDataSaverEnabled,
                                 attributes, ukm::kInvalidSourceId);
      if (attempt)
        attempt->SetEligibility(PreloadingEligibility::kDataSaverEnabled);
      return RenderFrameHost::kNoFrameTreeNodeId;
    }

    // TODO(crbug.com/1176054): Support cross-site prerendering.
    // The initiator origin is nullopt when prerendering is initiated by the
    // browser (not by a renderer using Speculation Rules API). In that case,
    // skip the  same-site and same-origin check.
    if (!attributes.IsBrowserInitiated()) {
      if (!prerender_navigation_utils::IsSameSite(
              attributes.prerendering_url,
              attributes.initiator_origin.value())) {
        RecordPrerenderFinalStatus(PrerenderFinalStatus::kCrossSiteNavigation,
                                   attributes, ukm::kInvalidSourceId);

        if (attempt)
          attempt->SetEligibility(PreloadingEligibility::kCrossOrigin);
        return RenderFrameHost::kNoFrameTreeNodeId;
      } else if (
          !blink::features::
              IsSameSiteCrossOriginForSpeculationRulesPrerender2Enabled() &&
          !attributes.initiator_origin.value().IsSameOriginWith(
              attributes.prerendering_url)) {
        RecordPrerenderFinalStatus(
            PrerenderFinalStatus::kSameSiteCrossOriginNavigation, attributes,
            ukm::kInvalidSourceId);

        if (attempt)
          attempt->SetEligibility(PreloadingEligibility::kCrossOrigin);
        return RenderFrameHost::kNoFrameTreeNodeId;
      }
    }

    // Disallow all pages that have an effective URL like host apps and NTP.
    if (SiteInstanceImpl::HasEffectiveURL(web_contents.GetBrowserContext(),
                                          web_contents.GetURL())) {
      RecordPrerenderFinalStatus(PrerenderFinalStatus::kHasEffectiveUrl,
                                 attributes, ukm::kInvalidSourceId);
      if (attempt)
        attempt->SetEligibility(PreloadingEligibility::kHasEffectiveUrl);
      return RenderFrameHost::kNoFrameTreeNodeId;
    }

    // Once all eligibility checks are completed, set the status to kEligible.
    if (attempt)
      attempt->SetEligibility(PreloadingEligibility::kEligible);

    // Check for the HoldbackStatus after checking the eligibility.
    if (base::FeatureList::IsEnabled(features::kPrerender2Holdback)) {
      if (attempt)
        attempt->SetHoldbackStatus(PreloadingHoldbackStatus::kHoldback);
      return RenderFrameHost::kNoFrameTreeNodeId;
    }
    if (attempt)
      attempt->SetHoldbackStatus(PreloadingHoldbackStatus::kAllowed);

    // Ignore prerendering requests for the same URL.
    for (auto& iter : prerender_host_by_frame_tree_node_id_) {
      if (iter.second->GetInitialUrl() == attributes.prerendering_url) {
        if (attempt) {
          attempt->SetTriggeringOutcome(
              PreloadingTriggeringOutcome::kDuplicate);
        }

        return RenderFrameHost::kNoFrameTreeNodeId;
      }
    }

    // TODO(crbug.com/1197133): Cancel the started prerender and start a new one
    // if the score of the new candidate is higher than the started one's.
    //
    // TODO(crbug.com/1355151): Enqueue the request exceeding the number limit
    // until the forerunners are cancelled, and suspend starting a new prerender
    // when the number reaches the limit.
    if (!IsAllowedToStartPrerenderingForTrigger(attributes.trigger_type)) {
      if (attempt) {
        // The reason we don't consider limit exceeded as an ineligibility
        // reason is because we can't replicate the behavior in our other
        // experiment groups for analysis. To prevent this we set
        // TriggeringOutcome to kFailure and look into the failure reason to
        // learn more.
        attempt->SetFailureReason(
            ToPreloadingFailureReason(
                PrerenderFinalStatus::kMaxNumOfRunningPrerendersExceeded));
      }
      RecordPrerenderFinalStatus(
          PrerenderFinalStatus::kMaxNumOfRunningPrerendersExceeded, attributes,
          ukm::kInvalidSourceId);
      return RenderFrameHost::kNoFrameTreeNodeId;
    }

    auto prerender_host = std::make_unique<PrerenderHost>(
        attributes, web_contents_impl,
        attempt ? attempt->GetWeakPtr() : nullptr);
    frame_tree_node_id = prerender_host->frame_tree_node_id();

    CHECK(!base::Contains(prerender_host_by_frame_tree_node_id_,
                          frame_tree_node_id));
    prerender_host_by_frame_tree_node_id_[frame_tree_node_id] =
        std::move(prerender_host);
  }

  // TODO(crbug.com/1355151): Complete the implementation of
  // `pending_prerenders_` handling such as removing the pending request from
  // the queue on cancellation to unwrap this feature flag.
  if (base::FeatureList::IsEnabled(
          blink::features::kPrerender2SequentialPrerendering)) {
    switch (attributes.trigger_type) {
      case PrerenderTriggerType::kSpeculationRule:
        pending_prerenders_.push_back(frame_tree_node_id);
        // Start the initial prerendering navigation of the pending request in
        // the head of the queue if there's no running prerender.
        if (running_prerender_host_id_ == RenderFrameHost::kNoFrameTreeNodeId) {
          // No running prerender means that no other prerender is waiting in
          // the pending queue, because the prerender sequence only stops when
          // all the pending prerenders are started.
          DCHECK_EQ(pending_prerenders_.size(), 1u);
          int started_frame_tree_node_id =
              StartPrerendering(RenderFrameHost::kNoFrameTreeNodeId);
          DCHECK(started_frame_tree_node_id == frame_tree_node_id ||
                 started_frame_tree_node_id ==
                     RenderFrameHost::kNoFrameTreeNodeId);
          frame_tree_node_id = started_frame_tree_node_id;
        }
        break;
      case PrerenderTriggerType::kEmbedder:
        // The prerendering request from embedder should have high-priority
        // because embedder prediction is more likely for the user to visit.
        // Hold the return value of `StartPrerendering` because the requested
        // prerender might be cancelled due to some restrictions and
        // `kNoFrameTreeNodeId` should be returned in that case.
        frame_tree_node_id = StartPrerendering(frame_tree_node_id);
    }
  } else {
    // Hold the return value of `StartPrerendering` because the requested
    // prerender might be cancelled due to some restrictions and
    // `kNoFrameTreeNodeId` should be returned in that case.
    frame_tree_node_id = StartPrerendering(frame_tree_node_id);
  }

  return frame_tree_node_id;
}

int PrerenderHostRegistry::StartPrerendering(int frame_tree_node_id) {
  if (frame_tree_node_id == RenderFrameHost::kNoFrameTreeNodeId) {
    DCHECK(base::FeatureList::IsEnabled(
        blink::features::kPrerender2SequentialPrerendering));
    DCHECK_EQ(running_prerender_host_id_, RenderFrameHost::kNoFrameTreeNodeId);

    // Don't start the pending prerender in the background tab.
    if (web_contents()->GetVisibility() == Visibility::HIDDEN) {
      return RenderFrameHost::kNoFrameTreeNodeId;
    }

    // Skip cancelled requests.
    while (!pending_prerenders_.empty()) {
      int host_id = pending_prerenders_.front();
      pending_prerenders_.pop_front();
      if (prerender_host_by_frame_tree_node_id_.contains(host_id)) {
        frame_tree_node_id = host_id;
        break;
      }
    }

    if (frame_tree_node_id == RenderFrameHost::kNoFrameTreeNodeId) {
      return RenderFrameHost::kNoFrameTreeNodeId;
    }
  }

  auto prerender_host_it =
      prerender_host_by_frame_tree_node_id_.find(frame_tree_node_id);
  DCHECK(prerender_host_it != prerender_host_by_frame_tree_node_id_.end());
  PrerenderHost& prerender_host = *prerender_host_it->second;
  devtools_instrumentation::WillInitiatePrerender(
      prerender_host.GetPrerenderFrameTree());
  if (!prerender_host.StartPrerendering()) {
    CancelHost(frame_tree_node_id, PrerenderFinalStatus::kStartFailed);
    return RenderFrameHost::kNoFrameTreeNodeId;
  }

  // Check the current memory usage and destroy a prerendering if the entire
  // browser uses excessive memory. This occurs asynchronously.
  switch (prerender_host_by_frame_tree_node_id_[frame_tree_node_id]
              ->trigger_type()) {
    case PrerenderTriggerType::kSpeculationRule:
      DestroyWhenUsingExcessiveMemory(frame_tree_node_id);
      break;
    case PrerenderTriggerType::kEmbedder:
      // We don't check the memory usage for embedder triggered prerenderings
      // for now.
      break;
  }

  if (base::FeatureList::IsEnabled(
          blink::features::kPrerender2SequentialPrerendering)) {
    // Update the `running_prerender_host_id` to the starting prerender's id.
    switch (prerender_host_by_frame_tree_node_id_[frame_tree_node_id]
                ->trigger_type()) {
      case PrerenderTriggerType::kSpeculationRule:
        running_prerender_host_id_ = frame_tree_node_id;
        break;
      case PrerenderTriggerType::kEmbedder:
        // `running_prerender_host_id` only tracks the id for speculation rules
        // trigger, so we don't update it in the case of embedder.
        break;
    }
  }

  RecordPrerenderTriggered(
      prerender_host_by_frame_tree_node_id_[frame_tree_node_id]
          ->initiator_ukm_id());
  return frame_tree_node_id;
}

void PrerenderHostRegistry::CancelHosts(
    const std::vector<int>& frame_tree_node_ids,
    PrerenderFinalStatus final_status) {
  TRACE_EVENT1("navigation", "PrerenderHostRegistry::CancelHosts",
               "frame_tree_node_ids", frame_tree_node_ids);

  // Cancel must not be requested during activation.
  CHECK(!reserved_prerender_host_);

  for (int host_id : frame_tree_node_ids) {
    // Look up the id in the non-reserved host map.
    auto iter = prerender_host_by_frame_tree_node_id_.find(host_id);
    if (iter == prerender_host_by_frame_tree_node_id_.end())
      continue;

    if (running_prerender_host_id_ == host_id)
      running_prerender_host_id_ = RenderFrameHost::kNoFrameTreeNodeId;

    // Remove the prerender host from the host map so that it's not used for
    // activation during asynchronous deletion.
    std::unique_ptr<PrerenderHost> prerender_host = std::move(iter->second);
    prerender_host_by_frame_tree_node_id_.erase(iter);

    // Asynchronously delete the prerender host.
    ScheduleToDeleteAbandonedHost(std::move(prerender_host), final_status);
  }

  // Start another prerender if the running prerender is cancelled.
  if (base::FeatureList::IsEnabled(
          blink::features::kPrerender2SequentialPrerendering) &&
      running_prerender_host_id_ == RenderFrameHost::kNoFrameTreeNodeId) {
    StartPrerendering(RenderFrameHost::kNoFrameTreeNodeId);
  }
}

bool PrerenderHostRegistry::CancelHost(int frame_tree_node_id,
                                       PrerenderFinalStatus final_status) {
  TRACE_EVENT1("navigation", "PrerenderHostRegistry::CancelHost",
               "frame_tree_node_id", frame_tree_node_id);

  // Cancel must not be requested during activation.
  CHECK(!reserved_prerender_host_);

  // Look up the id in the non-reserved host map, remove it from the map, and
  // record the cancellation reason.
  auto iter = prerender_host_by_frame_tree_node_id_.find(frame_tree_node_id);
  if (iter == prerender_host_by_frame_tree_node_id_.end())
    return false;

  // Remove the prerender host from the host map so that it's not used for
  // activation during asynchronous deletion.

  std::unique_ptr<PrerenderHost> prerender_host = std::move(iter->second);
  prerender_host_by_frame_tree_node_id_.erase(iter);

  // Asynchronously delete the prerender host.
  ScheduleToDeleteAbandonedHost(std::move(prerender_host), final_status);

  // Start another prerender if the running prerender is cancelled.
  if (running_prerender_host_id_ == frame_tree_node_id) {
    running_prerender_host_id_ = RenderFrameHost::kNoFrameTreeNodeId;
    StartPrerendering(RenderFrameHost::kNoFrameTreeNodeId);
  }

  return true;
}

void PrerenderHostRegistry::CancelAllHosts(PrerenderFinalStatus final_status) {
  // Cancel must not be requested during activation.
  CHECK(!reserved_prerender_host_);

  auto prerender_host_map = std::move(prerender_host_by_frame_tree_node_id_);
  for (auto& iter : prerender_host_map) {
    std::unique_ptr<PrerenderHost> prerender_host = std::move(iter.second);
    ScheduleToDeleteAbandonedHost(std::move(prerender_host), final_status);
  }

  pending_prerenders_.clear();
}

int PrerenderHostRegistry::FindPotentialHostToActivate(
    NavigationRequest& navigation_request) {
  TRACE_EVENT2(
      "navigation", "PrerenderHostRegistry::FindPotentialHostToActivate",
      "navigation_url", navigation_request.GetURL().spec(), "render_frame_host",
      navigation_request.frame_tree_node()->current_frame_host());
  return FindHostToActivateInternal(navigation_request);
}

int PrerenderHostRegistry::ReserveHostToActivate(
    NavigationRequest& navigation_request,
    int expected_host_id) {
  RenderFrameHostImpl* render_frame_host =
      navigation_request.frame_tree_node()->current_frame_host();
  TRACE_EVENT2("navigation", "PrerenderHostRegistry::ReserveHostToActivate",
               "navigation_url", navigation_request.GetURL().spec(),
               "render_frame_host", render_frame_host);

  // Find an available host for the navigation request.
  int host_id = FindHostToActivateInternal(navigation_request);
  if (host_id == RenderFrameHost::kNoFrameTreeNodeId)
    return RenderFrameHost::kNoFrameTreeNodeId;

  // Check if the host is what the NavigationRequest expects. The host can be
  // different when a trigger page removes the existing prerender and then
  // re-adds a new prerender for the same URL.
  //
  // NavigationRequest makes sure that the prerender is ready for activation by
  // waiting for PrerenderCommitDeferringCondition before this point. Without
  // this check, if the prerender is changed during the period,
  // NavigationRequest may attempt to activate the new prerender that is not
  // ready.
  if (host_id != expected_host_id)
    return RenderFrameHost::kNoFrameTreeNodeId;

  // Remove the host from the map of non-reserved hosts.
  std::unique_ptr<PrerenderHost> host =
      std::move(prerender_host_by_frame_tree_node_id_[host_id]);
  prerender_host_by_frame_tree_node_id_.erase(host_id);
  DCHECK_EQ(host_id, host->frame_tree_node_id());

  // Reserve the host for activation.
  DCHECK(!reserved_prerender_host_);
  reserved_prerender_host_ = std::move(host);

  return host_id;
}

RenderFrameHostImpl* PrerenderHostRegistry::GetRenderFrameHostForReservedHost(
    int frame_tree_node_id) {
  if (!reserved_prerender_host_)
    return nullptr;

  DCHECK_EQ(frame_tree_node_id, reserved_prerender_host_->frame_tree_node_id());

  return reserved_prerender_host_->GetPrerenderedMainFrameHost();
}

std::unique_ptr<StoredPage> PrerenderHostRegistry::ActivateReservedHost(
    int frame_tree_node_id,
    NavigationRequest& navigation_request) {
  CHECK(reserved_prerender_host_);
  CHECK_EQ(frame_tree_node_id, reserved_prerender_host_->frame_tree_node_id());

  std::unique_ptr<PrerenderHost> prerender_host =
      std::move(reserved_prerender_host_);
  return prerender_host->Activate(navigation_request);
}

void PrerenderHostRegistry::OnActivationFinished(int frame_tree_node_id) {
  // OnActivationFinished() should not be called for non-reserved hosts.
  DCHECK(!base::Contains(prerender_host_by_frame_tree_node_id_,
                         frame_tree_node_id));

  if (!reserved_prerender_host_)
    return;

  DCHECK_EQ(frame_tree_node_id, reserved_prerender_host_->frame_tree_node_id());
  reserved_prerender_host_.reset();
}

PrerenderHost* PrerenderHostRegistry::FindNonReservedHostById(
    int frame_tree_node_id) {
  auto id_iter = prerender_host_by_frame_tree_node_id_.find(frame_tree_node_id);
  if (id_iter == prerender_host_by_frame_tree_node_id_.end())
    return nullptr;
  return id_iter->second.get();
}

PrerenderHost* PrerenderHostRegistry::FindReservedHostById(
    int frame_tree_node_id) {
  if (!reserved_prerender_host_)
    return nullptr;

  if (frame_tree_node_id != reserved_prerender_host_->frame_tree_node_id())
    return nullptr;

  return reserved_prerender_host_.get();
}

std::vector<FrameTree*> PrerenderHostRegistry::GetPrerenderFrameTrees() {
  std::vector<FrameTree*> result;
  for (auto& i : prerender_host_by_frame_tree_node_id_) {
    result.push_back(&i.second->GetPrerenderFrameTree());
  }
  if (reserved_prerender_host_)
    result.push_back(&reserved_prerender_host_->GetPrerenderFrameTree());

  return result;
}

PrerenderHost* PrerenderHostRegistry::FindHostByUrlForTesting(
    const GURL& prerendering_url) {
  for (auto& iter : prerender_host_by_frame_tree_node_id_) {
    if (iter.second->GetInitialUrl() == prerendering_url)
      return iter.second.get();
  }
  return nullptr;
}

void PrerenderHostRegistry::CancelAllHostsForTesting() {
  DCHECK(!reserved_prerender_host_)
      << "It is not possible to cancel a reserved host, so they must not exist "
         "when trying to cancel all hosts";

  for (auto& iter : prerender_host_by_frame_tree_node_id_) {
    // Asynchronously delete the prerender host.
    ScheduleToDeleteAbandonedHost(
        std::move(iter.second),
        PrerenderFinalStatus::kCancelAllHostsForTesting);
  }

  // After we're done scheduling deletion, clear the map and the pending queue.
  prerender_host_by_frame_tree_node_id_.clear();
  pending_prerenders_.clear();
}

base::WeakPtr<PrerenderHostRegistry> PrerenderHostRegistry::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void PrerenderHostRegistry::ForEachPrerenderHost(
    base::RepeatingCallback<void(PrerenderHost&)> callback) {
  for (auto& iter : prerender_host_by_frame_tree_node_id_) {
    callback.Run(*iter.second);
  }

  if (reserved_prerender_host_)
    callback.Run(*reserved_prerender_host_);
}

void PrerenderHostRegistry::DidFinishNavigation(
    NavigationHandle* navigation_handle) {
  auto* navigation_request = NavigationRequest::From(navigation_handle);

  if (navigation_request->IsSameDocument())
    return;

  int main_frame_host_id = navigation_request->frame_tree_node()
                               ->frame_tree()
                               ->root()
                               ->frame_tree_node_id();
  PrerenderHost* prerender_host = FindNonReservedHostById(main_frame_host_id);
  if (!prerender_host)
    return;

  prerender_host->DidFinishNavigation(navigation_handle);

  if (base::FeatureList::IsEnabled(
          blink::features::kPrerender2SequentialPrerendering) &&
      running_prerender_host_id_ == main_frame_host_id) {
    running_prerender_host_id_ = RenderFrameHost::kNoFrameTreeNodeId;
    StartPrerendering(RenderFrameHost::kNoFrameTreeNodeId);
  }
}

void PrerenderHostRegistry::OnVisibilityChanged(Visibility visibility) {
  if (base::FeatureList::IsEnabled(blink::features::kPrerender2InBackground)) {
    // Update the timer for prerendering timeout in the background.
    switch (visibility) {
      case Visibility::HIDDEN:
        // Keep a prerendered page alive in the background when its visibility
        // state changes to HIDDEN if the feature is enabled.
        DCHECK(!timeout_timer_.IsRunning());

        timeout_timer_.SetTaskRunner(GetTimerTaskRunner());
        // Cancel PrerenderHost in the background when it exceeds a certain
        // amount of time defined in `kTimeToLiveInBackground`.
        timeout_timer_.Start(
            FROM_HERE, kTimeToLiveInBackground,
            base::BindOnce(&PrerenderHostRegistry::CancelAllHosts,
                           base::Unretained(this),
                           PrerenderFinalStatus::kTimeoutBackgrounded));
        break;
      case Visibility::OCCLUDED:
      case Visibility::VISIBLE:
        // Stop the timer when a prerendered page gets visible to users.
        timeout_timer_.Stop();
        break;
    }

    if (!base::FeatureList::IsEnabled(
            blink::features::kPrerender2SequentialPrerendering))
      return;

    // Start the next prerender when the page gets back to the foreground.
    switch (visibility) {
      case Visibility::VISIBLE:
      case Visibility::OCCLUDED:
        if (running_prerender_host_id_ == RenderFrameHost::kNoFrameTreeNodeId)
          StartPrerendering(RenderFrameHost::kNoFrameTreeNodeId);
        break;
      case Visibility::HIDDEN:
        break;
    }
    return;
  }

  if (visibility == Visibility::HIDDEN) {
    CancelAllHosts(PrerenderFinalStatus::kTriggerBackgrounded);
  }
}

void PrerenderHostRegistry::ResourceLoadComplete(
    RenderFrameHost* render_frame_host,
    const GlobalRequestID& request_id,
    const blink::mojom::ResourceLoadInfo& resource_load_info) {
  for (auto& iter : prerender_host_by_frame_tree_node_id_) {
    // Observe resource loads only in the prerendering frame tree.
    if (&render_frame_host->GetPage() !=
        &iter.second->GetPrerenderedMainFrameHost()->GetPage()) {
      continue;
    }

    if (resource_load_info.net_error == net::Error::ERR_BLOCKED_BY_CLIENT) {
      CancelHost(iter.first, PrerenderFinalStatus::kBlockedByClient);
    }
    break;
  }
}

int PrerenderHostRegistry::FindHostToActivateInternal(
    NavigationRequest& navigation_request) {
  RenderFrameHostImpl* render_frame_host =
      navigation_request.frame_tree_node()->current_frame_host();
  TRACE_EVENT2("navigation",
               "PrerenderHostRegistry::FindHostToActivateInternal",
               "navigation_url", navigation_request.GetURL().spec(),
               "render_frame_host", render_frame_host);

  // Disallow activation when the navigation is for a nested browsing context
  // (e.g., iframes, fenced frames). This is because nested browsing contexts
  // such as iframes are supposed to be created in the parent's browsing context
  // group and can script with the parent, but prerendered pages are created in
  // new browsing context groups. And also, we disallow activation when the
  // navigation is for a fenced frame to prevent the communication path from the
  // embedding page to the fenced frame.
  if (!navigation_request.IsInPrimaryMainFrame())
    return RenderFrameHost::kNoFrameTreeNodeId;

  // Disallow activation when the navigation happens in the prerendering frame
  // tree.
  if (navigation_request.IsInPrerenderedMainFrame())
    return RenderFrameHost::kNoFrameTreeNodeId;

  // Disallow activation when other auxiliary browsing contexts (e.g., pop-up
  // windows) exist in the same browsing context group. This is because these
  // browsing contexts should be able to script each other, but prerendered
  // pages are created in new browsing context groups.
  SiteInstance* site_instance = render_frame_host->GetSiteInstance();
  if (site_instance->GetRelatedActiveContentsCount() != 1u)
    return RenderFrameHost::kNoFrameTreeNodeId;

  // Find an available host for the navigation URL.
  PrerenderHost* host = nullptr;
  for (const auto& [host_id, it_prerender_host] :
       prerender_host_by_frame_tree_node_id_) {
    if (it_prerender_host->IsUrlMatch(navigation_request.GetURL())) {
      host = it_prerender_host.get();
      break;
    }
  }
  if (!host)
    return RenderFrameHost::kNoFrameTreeNodeId;

  if (!host->GetInitialNavigationId().has_value()) {
    DCHECK(base::FeatureList::IsEnabled(
        blink::features::kPrerender2SequentialPrerendering));
    CancelHost(host->frame_tree_node_id(),
               PrerenderFinalStatus::kActivatedBeforeStarted);
    return RenderFrameHost::kNoFrameTreeNodeId;
  }

  // Compare navigation params from activation with the navigation params
  // from the initial prerender navigation. If they don't match, the navigation
  // should not activate the prerendered page.
  if (!host->AreInitialPrerenderNavigationParamsCompatibleWithNavigation(
          navigation_request)) {
    // TODO(https://crbug.com/1328365): Report a detailed reason to devtools.
    // Currently users have to check
    // Prerender.Experimental.ActivationNavigationParamsMatch.
    // TODO(lingqi): We'd better cancel all hosts.
    CancelHost(host->frame_tree_node_id(),
               PrerenderFinalStatus::kActivationNavigationParameterMismatch);
    return RenderFrameHost::kNoFrameTreeNodeId;
  }

  if (!host->IsFramePolicyCompatibleWithPrimaryFrameTree()) {
    return RenderFrameHost::kNoFrameTreeNodeId;
  }

  // Cancel all the other prerender hosts because we no longer need the other
  // hosts after we determine the host to be activated.
  std::vector<int> cancelled_prerenders;
  for (const auto& [host_id, _] : prerender_host_by_frame_tree_node_id_) {
    if (host_id != host->frame_tree_node_id()) {
      cancelled_prerenders.push_back(host_id);
    }
  }
  CancelHosts(cancelled_prerenders, PrerenderFinalStatus::kTriggerDestroyed);
  pending_prerenders_.clear();

  return host->frame_tree_node_id();
}

void PrerenderHostRegistry::ScheduleToDeleteAbandonedHost(
    std::unique_ptr<PrerenderHost> prerender_host,
    PrerenderFinalStatus final_status) {
  prerender_host->RecordFinalStatus(PassKey(), final_status);

  // Asynchronously delete the prerender host.
  to_be_deleted_hosts_.push_back(std::move(prerender_host));
  base::SequencedTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&PrerenderHostRegistry::DeleteAbandonedHosts,
                                weak_factory_.GetWeakPtr()));
}

void PrerenderHostRegistry::DeleteAbandonedHosts() {
  to_be_deleted_hosts_.clear();
}

void PrerenderHostRegistry::NotifyTrigger(const GURL& url) {
  for (Observer& obs : observers_)
    obs.OnTrigger(url);
}

PrerenderTriggerType PrerenderHostRegistry::GetPrerenderTriggerType(
    int frame_tree_node_id) {
  PrerenderHost* prerender_host = FindReservedHostById(frame_tree_node_id);
  DCHECK(prerender_host);

  return prerender_host->trigger_type();
}

const std::string& PrerenderHostRegistry::GetPrerenderEmbedderHistogramSuffix(
    int frame_tree_node_id) {
  PrerenderHost* prerender_host = FindReservedHostById(frame_tree_node_id);
  DCHECK(prerender_host);

  return prerender_host->embedder_histogram_suffix();
}

bool PrerenderHostRegistry::IsAllowedToStartPrerenderingForTrigger(
    PrerenderTriggerType trigger_type) {
  int trigger_type_count = 0;
  for (const auto& host_by_id : prerender_host_by_frame_tree_node_id_) {
    if (host_by_id.second->trigger_type() == trigger_type)
      ++trigger_type_count;
  }

  switch (trigger_type) {
    case PrerenderTriggerType::kSpeculationRule:
      // The number of prerenders triggered by speculation rules is limited to a
      // Finch config param.
      return trigger_type_count <
             base::GetFieldTrialParamByFeatureAsInt(
                 blink::features::kPrerender2,
                 blink::features::kPrerender2MaxNumOfRunningSpeculationRules,
                 10);
    case PrerenderTriggerType::kEmbedder:
      // Currently the number of prerenders triggered by an embedder is limited
      // to two.
      return trigger_type_count < 2;
  }
}

void PrerenderHostRegistry::DestroyWhenUsingExcessiveMemory(
    int frame_tree_node_id) {
  if (!base::FeatureList::IsEnabled(blink::features::kPrerender2MemoryControls))
    return;

  memory_instrumentation::MemoryInstrumentation::GetInstance()
      ->RequestPrivateMemoryFootprint(
          base::kNullProcessId,
          base::BindOnce(&PrerenderHostRegistry::DidReceiveMemoryDump,
                         weak_factory_.GetWeakPtr(), frame_tree_node_id));
}

void PrerenderHostRegistry::DidReceiveMemoryDump(
    int frame_tree_node_id,
    bool success,
    std::unique_ptr<memory_instrumentation::GlobalMemoryDump> dump) {
  DCHECK(
      base::FeatureList::IsEnabled(blink::features::kPrerender2MemoryControls));
  // Stop a prerendering when we can't get the current memory usage.
  if (!success) {
    CancelHost(frame_tree_node_id, PrerenderFinalStatus::kFailToGetMemoryUsage);
    return;
  }

  int64_t private_footprint_total_kb = 0;
  for (const auto& pmd : dump->process_dumps()) {
    private_footprint_total_kb += pmd.os_dump().private_footprint_kb;
  }

  // TODO(crbug.com/1273341): Finalize the threshold after the experiment
  // completes. The default acceptable percent is 10% of the system memory.
  int acceptable_percent_of_system_memory =
      base::GetFieldTrialParamByFeatureAsInt(
          blink::features::kPrerender2MemoryControls,
          blink::features::
              kPrerender2MemoryAcceptablePercentOfSystemMemoryParamName,
          10);

  // When the current memory usage is higher than
  // `acceptable_percent_of_system_memory` % of the system memory, cancel a
  // prerendering with `frame_tree_node_id`.
  if (private_footprint_total_kb * 1024 >=
      acceptable_percent_of_system_memory * 0.01 *
          base::SysInfo::AmountOfPhysicalMemory()) {
    CancelHost(frame_tree_node_id, PrerenderFinalStatus::kMemoryLimitExceeded);
  }
}

scoped_refptr<base::SingleThreadTaskRunner>
PrerenderHostRegistry::GetTimerTaskRunner() {
  return timer_task_runner_for_testing_ ? timer_task_runner_for_testing_
                                        : base::ThreadTaskRunnerHandle::Get();
}

void PrerenderHostRegistry::SetTaskRunnerForTesting(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  timer_task_runner_for_testing_ = std::move(task_runner);
}

}  // namespace content
