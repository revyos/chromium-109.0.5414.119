// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/preloading/prerender/prerender_host.h"

#include "base/feature_list.h"
#include "base/functional/callback_forward.h"
#include "base/observer_list.h"
#include "base/run_loop.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/trace_event/trace_conversion_helper.h"
#include "base/trace_event/typed_macros.h"
#include "content/browser/client_hints/client_hints.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/preloading/prerender/prerender_final_status.h"
#include "content/browser/preloading/prerender/prerender_host_registry.h"
#include "content/browser/preloading/prerender/prerender_metrics.h"
#include "content/browser/renderer_host/frame_tree.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_controller_impl.h"
#include "content/browser/renderer_host/navigation_entry_restore_context_impl.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/site_info.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/prerender_trigger_type.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/referrer.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "third_party/blink/public/common/client_hints/enabled_client_hints.h"
#include "third_party/blink/public/common/features.h"
#include "url/origin.h"

namespace content {

namespace {

bool AreHttpRequestHeadersCompatible(
    const std::string& potential_activation_headers_str,
    const std::string& prerender_headers_str,
    PrerenderTriggerType trigger_type,
    const std::string& embedder_histogram_suffix) {
  net::HttpRequestHeaders prerender_headers;
  prerender_headers.AddHeadersFromString(prerender_headers_str);

  net::HttpRequestHeaders potential_activation_headers;
  potential_activation_headers.AddHeadersFromString(
      potential_activation_headers_str);

  // `prerender_headers` contains the "Purpose: prefetch" and "Sec-Purpose:
  // prefetch;prerender" to notify servers of prerender requests, while
  // `potential_activation_headers` doesn't contain it. Remove "Purpose" and
  // "Sec-Purpose" matching from consideration so that activation works with the
  // header.
  prerender_headers.RemoveHeader("Purpose");
  potential_activation_headers.RemoveHeader("Purpose");
  prerender_headers.RemoveHeader("Sec-Purpose");
  potential_activation_headers.RemoveHeader("Sec-Purpose");

  // Compare headers in serialized strings. The spec doesn't require serialized
  // string matches, but practically Chrome generates headers in a decisive way,
  // i.e. in the same order and cases. So we can expect serialized string
  // comparisons just work. We ensure this assumption through the metric we
  // handled in AnalyzePrerenderActivationHeader.
  if (prerender_headers.ToString() == potential_activation_headers.ToString()) {
    return true;
  }

  // The headers mismatch. Analyze the headers asynchronously.
  // TODO(https://crbug.com/1378921): This is only used to detect if prerender
  // fails to set headers correctly. Remove this logic after we draw the
  // conclusion.
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&AnalyzePrerenderActivationHeader,
                     std::move(potential_activation_headers),
                     std::move(prerender_headers), trigger_type,
                     embedder_histogram_suffix));
  return false;
}

PreloadingFailureReason ToPreloadingFailureReason(PrerenderFinalStatus status) {
  return static_cast<PreloadingFailureReason>(
      static_cast<int>(status) +
      static_cast<int>(
          PreloadingFailureReason::kPreloadingFailureReasonCommonEnd));
}

}  // namespace

// static
PrerenderHost* PrerenderHost::GetPrerenderHostFromFrameTreeNode(
    FrameTreeNode& frame_tree_node) {
  WebContentsImpl* web_contents =
      static_cast<WebContentsImpl*>(WebContentsImpl::FromRenderFrameHost(
          frame_tree_node.current_frame_host()));
  DCHECK(web_contents);
  PrerenderHostRegistry* prerender_registry =
      web_contents->GetPrerenderHostRegistry();
  int prerender_host_id =
      frame_tree_node.frame_tree()->root()->frame_tree_node_id();

  if (PrerenderHost* host =
          prerender_registry->FindNonReservedHostById(prerender_host_id)) {
    return host;
  } else {
    // TODO(https://crbug.com/1355279): This function can be called during
    // prerender activation so we have to call FindReservedHostById here and
    // give it another shot. Consider using delegate after PrerenderHost
    // implements FrameTree::Delegate.
    return prerender_registry->FindReservedHostById(prerender_host_id);
  }
}

PrerenderHost::PrerenderHost(const PrerenderAttributes& attributes,
                             WebContentsImpl& web_contents,
                             base::WeakPtr<PreloadingAttempt> attempt)
    : attributes_(attributes),
      attempt_(std::move(attempt)),
      web_contents_(web_contents),
      frame_tree_(
          std::make_unique<FrameTree>(web_contents.GetBrowserContext(),
                                      this,
                                      this,
                                      &web_contents,
                                      &web_contents,
                                      &web_contents,
                                      &web_contents,
                                      &web_contents,
                                      &web_contents,
                                      FrameTree::Type::kPrerender,
                                      base::UnguessableToken::Create())) {
  DCHECK(blink::features::IsPrerender2Enabled());
  // If the prerendering is browser-initiated, it is expected to have no
  // initiator. All initiator related information should be null or invalid. On
  // the other hand, renderer-initiated prerendering should have valid initiator
  // information.
  if (attributes.IsBrowserInitiated()) {
    DCHECK(!attributes.initiator_origin.has_value());
    DCHECK(!attributes.initiator_frame_token.has_value());
    DCHECK_EQ(attributes.initiator_process_id,
              ChildProcessHost::kInvalidUniqueID);
    DCHECK_EQ(attributes.initiator_ukm_id, ukm::kInvalidSourceId);
    DCHECK_EQ(attributes.initiator_frame_tree_node_id,
              RenderFrameHost::kNoFrameTreeNodeId);
  } else {
    DCHECK(attributes.initiator_origin.has_value());
    DCHECK(attributes.initiator_frame_token.has_value());
    // TODO(https://crbug.com/1325211): Add back the following DCHECKs after
    // fixing prerendering activation for embedder-triggered prerendering in
    // unittests.
    // DCHECK_NE(attributes.initiator_process_id,
    // ChildProcessHost::kInvalidUniqueID);
    // DCHECK_NE(attributes.initiator_ukm_id, ukm::kInvalidSourceId);
    // DCHECK_NE(attributes.initiator_frame_tree_node_id,
    //           RenderFrameHost::kNoFrameTreeNodeId);
  }

  // When `kPrerender2SequentialPrerendering` feature is enabled, the prerender
  // host can be pending until the host starts or is cancelled. So the outcome
  // is set here to track the pending status.
  if (base::FeatureList::IsEnabled(
          blink::features::kPrerender2SequentialPrerendering) &&
      attempt_) {
    attempt_->SetTriggeringOutcome(
        PreloadingTriggeringOutcome::kTriggeredButPending);
  }

  scoped_refptr<SiteInstance> site_instance =
      SiteInstance::Create(web_contents.GetBrowserContext());
  frame_tree_->Init(site_instance.get(),
                    /*renderer_initiated_creation=*/false,
                    /*main_frame_name=*/"", /*opener_for_origin=*/nullptr,
                    /*frame_policy=*/blink::FramePolicy());

  // Use the same SessionStorageNamespace as the primary page for the
  // prerendering page.
  frame_tree_->controller().SetSessionStorageNamespace(
      site_instance->GetStoragePartitionConfig(),
      web_contents_->GetPrimaryFrameTree()
          .controller()
          .GetSessionStorageNamespace(
              site_instance->GetStoragePartitionConfig()));

  // TODO(https://crbug.com/1199679): This should be moved to FrameTree::Init
  web_contents_->NotifySwappedFromRenderManager(
      /*old_frame=*/nullptr,
      frame_tree_->root()->render_manager()->current_frame_host());

  frame_tree_node_id_ = frame_tree_->root()->frame_tree_node_id();
}

PrerenderHost::~PrerenderHost() {
  for (auto& observer : observers_) {
    observer.OnHostDestroyed(
        final_status_.value_or(PrerenderFinalStatus::kDestroyed));
  }

  if (!final_status_) {
    RecordFinalStatus(PrerenderFinalStatus::kDestroyed,
                      attributes_.initiator_ukm_id, ukm::kInvalidSourceId);
  }

  // If we are still waiting on test loop, we can assume the page loading step
  // has been cancelled and the PrerenderHost is being discarded without
  // completing loading the page.
  if (on_wait_loading_finished_) {
    std::move(on_wait_loading_finished_)
        .Run(PrerenderHost::LoadingOutcome::kPrerenderingCancelled);
  }

  if (frame_tree_) {
    frame_tree_->Shutdown();
  }
}

void PrerenderHost::DidStopLoading() {
  if (on_wait_loading_finished_) {
    std::move(on_wait_loading_finished_).Run(LoadingOutcome::kLoadingCompleted);
  }
}

bool PrerenderHost::IsHidden() {
  return true;
}

FrameTree* PrerenderHost::LoadingTree() {
  // For prerendering loading tree is the same as its frame tree as loading is
  // done at a frame tree level in the background, unlike the loading visible
  // to the user where we account for nested frame tree loading state.
  return frame_tree_.get();
}

int PrerenderHost::GetOuterDelegateFrameTreeNodeId() {
  // A prerendered FrameTree is not "inner to" or "nested inside" another
  // FrameTree; it exists in parallel to the primary FrameTree of the current
  // WebContents. Therefore, it must not attempt to access the primary
  // FrameTree in the sense of an "outer delegate" relationship, so we return
  // the invalid ID here.
  return FrameTreeNode::kFrameTreeNodeInvalidId;
}

bool PrerenderHost::IsPortal() {
  return false;
}

void PrerenderHost::ActivateAndShowRepostFormWarningDialog() {
  // Not supported, cancel pending reload.
  GetNavigationController().CancelPendingReload();
}

bool PrerenderHost::ShouldPreserveAbortedURLs() {
  return false;
}

WebContents* PrerenderHost::DeprecatedGetWebContents() {
  return &*web_contents_;
}

// TODO(https://crbug.com/1132746): Inspect diffs from the current
// no-state-prefetch implementation. See PrerenderContents::StartPrerendering()
// for example.
bool PrerenderHost::StartPrerendering() {
  TRACE_EVENT0("navigation", "PrerenderHost::StartPrerendering");

  // Since prerender started we mark it as eligible and set it to running.
  SetTriggeringOutcome(PreloadingTriggeringOutcome::kRunning);

  // Start prerendering navigation.
  NavigationController::LoadURLParams load_url_params(
      attributes_.prerendering_url);
  load_url_params.initiator_origin = attributes_.initiator_origin;
  load_url_params.initiator_process_id = attributes_.initiator_process_id;
  load_url_params.initiator_frame_token = attributes_.initiator_frame_token;
  load_url_params.is_renderer_initiated = !attributes_.IsBrowserInitiated();
  load_url_params.transition_type =
      ui::PageTransitionFromInt(attributes_.transition_type);

  // Just use the referrer from attributes, as NoStatePrefetch does.
  load_url_params.referrer = attributes_.referrer;

  // TODO(https://crbug.com/1189034): Should we set `override_user_agent` here?
  // Things seem to work without it.

  // TODO(https://crbug.com/1132746): Set up other fields of `load_url_params`
  // as well, and add tests for them.
  base::WeakPtr<NavigationHandle> created_navigation_handle =
      GetNavigationController().LoadURLWithParams(load_url_params);

  if (!created_navigation_handle)
    return false;

  // Even when LoadURLWithParams() returns a valid navigation handle, navigation
  // can fail during navigation start, for example, due to prerendering a
  // non-supported URL scheme that is filtered out in
  // PrerenderNavigationThrottle.
  if (final_status_.has_value())
    return false;

  if (initial_navigation_id_.has_value()) {
    // In usual code path, `initial_navigation_id_` should be set by
    // PrerenderNavigationThrottle during `LoadURLWithParams` above.
    DCHECK_EQ(*initial_navigation_id_,
              created_navigation_handle->GetNavigationId());
    DCHECK(begin_params_);
    DCHECK(common_params_);
  } else {
    // In some exceptional code path, such as the navigation failed due to CSP
    // violations, PrerenderNavigationThrottle didn't run at this point. So,
    // set the ID here.
    initial_navigation_id_ = created_navigation_handle->GetNavigationId();
    // |begin_params_| and |common_params_| is null here, but it doesn't matter
    // as this branch is reached only when the initial navigation fails,
    // so this PrerenderHost can't be activated.
  }

  NavigationRequest* navigation_request =
      NavigationRequest::From(created_navigation_handle.get());
  // The initial navigation in the prerender frame tree should not wait for
  // `beforeunload` in the old page, so BeginNavigation stage should be reached
  // synchronously.
  DCHECK_GE(navigation_request->state(),
            NavigationRequest::WAITING_FOR_RENDERER_RESPONSE);
  return true;
}

void PrerenderHost::DidFinishNavigation(NavigationHandle* navigation_handle) {
  auto* navigation_request = NavigationRequest::From(navigation_handle);

  // Observe navigation only in the prerendering frame tree.
  DCHECK_EQ(navigation_request->frame_tree_node()->frame_tree(),
            frame_tree_.get());

  const bool is_prerender_main_frame =
      navigation_request->GetFrameTreeNodeId() == frame_tree_node_id_;

  // Cancel prerendering on navigation request failure.
  //
  // Check net::Error here rather than PrerenderNavigationThrottle as CSP
  // blocking occurs before NavigationThrottles so cannot be observed in
  // NavigationThrottle::WillFailRequest().
  net::Error net_error = navigation_request->GetNetErrorCode();

  absl::optional<PrerenderFinalStatus> status;
  if (net_error == net::Error::ERR_BLOCKED_BY_CSP) {
    status = PrerenderFinalStatus::kNavigationRequestBlockedByCsp;
  } else if (net_error == net::Error::ERR_BLOCKED_BY_CLIENT) {
    status = PrerenderFinalStatus::kBlockedByClient;
  } else if (is_prerender_main_frame && net_error != net::Error::OK) {
    status = PrerenderFinalStatus::kNavigationRequestNetworkError;
  } else if (is_prerender_main_frame && !navigation_request->HasCommitted()) {
    status = PrerenderFinalStatus::kNavigationNotCommitted;
  }
  if (status.has_value()) {
    Cancel(*status);
    return;
  }

  // The prerendered contents are considered ready for activation when the
  // main frame navigation reaches DidFinishNavigation and the prerender host
  // has not been canceled yet.
  if (is_prerender_main_frame && !final_status_) {
    DCHECK(!is_ready_for_activation_);
    is_ready_for_activation_ = true;

    // Prerender is ready to activate. Set the status to kReady.
    SetTriggeringOutcome(PreloadingTriggeringOutcome::kReady);
  }
}

std::unique_ptr<StoredPage> PrerenderHost::Activate(
    NavigationRequest& navigation_request) {
  TRACE_EVENT1("navigation", "PrerenderHost::Activate", "navigation_request",
               &navigation_request);

  DCHECK(is_ready_for_activation_);
  is_ready_for_activation_ = false;

  FrameTree& target_frame_tree = web_contents_->GetPrimaryFrameTree();

  // There should be no ongoing main-frame navigation during activation.
  // TODO(https://crbug.com/1190644): Make sure sub-frame navigations are
  // fine.
  DCHECK(!frame_tree_->root()->HasNavigation());

  // Before the root's current_frame_host is cleared, collect the subframes of
  // `frame_tree_` whose FrameTree will need to be updated.
  FrameTree::NodeRange node_range = frame_tree_->Nodes();
  std::vector<FrameTreeNode*> subframe_nodes(std::next(node_range.begin()),
                                             node_range.end());

  // Before the root's current_frame_host is cleared, collect the replication
  // state so that it can be used for post-activation validation.
  blink::mojom::FrameReplicationState prior_replication_state =
      frame_tree_->root()->current_replication_state();

  // Update FrameReplicationState::has_received_user_gesture_before_nav of the
  // prerendered page.
  //
  // On regular navigation, it is updated via a renderer => browser IPC
  // (RenderFrameHostImpl::HadStickyUserActivationBeforeNavigationChanged),
  // which is sent from blink::DocumentLoader::CommitNavigation. However,
  // this doesn't happen on prerender page activation, so the value is not
  // correctly updated without this treatment.
  //
  // The updated value will be sent to the renderer on
  // blink::mojom::Page::ActivatePrerenderedPage.
  prior_replication_state.has_received_user_gesture_before_nav =
      navigation_request.frame_tree_node()
          ->has_received_user_gesture_before_nav();

  // frame_tree_->root(). Do not add any code between here and
  // frame_tree_.reset() that calls into observer functions to minimize the
  // duration of current_frame_host being null.
  //
  // TODO(https://crbug.com/1176148): Investigate how to combine taking the
  // prerendered page and frame_tree_ destruction.
  std::unique_ptr<StoredPage> page =
      frame_tree_->root()->render_manager()->TakePrerenderedPage();

  std::unique_ptr<NavigationEntryRestoreContextImpl> context =
      std::make_unique<NavigationEntryRestoreContextImpl>();
  std::unique_ptr<NavigationEntryImpl> nav_entry =
      GetNavigationController()
          .GetEntryWithUniqueID(page->render_frame_host()->nav_entry_id())
          ->CloneWithoutSharing(context.get());

  navigation_request.SetPrerenderActivationNavigationState(
      std::move(nav_entry), prior_replication_state);

  DCHECK_EQ(&target_frame_tree,
            navigation_request.frame_tree_node()->frame_tree());

  // We support activating the prerendered page only to the topmost
  // RenderFrameHost.
  CHECK(!page->render_frame_host()->GetParentOrOuterDocumentOrEmbedder());

  page->render_frame_host()->SetFrameTreeNode(*(target_frame_tree.root()));

  page->render_frame_host()->SetRenderFrameHostOwner(target_frame_tree.root());

  // Copy frame name into the replication state of the primary main frame to
  // ensure that the replication state of the primary main frame after
  // activation matches the replication state stored in the renderer.
  // TODO(https://crbug.com/1237091): Copying frame name here is suboptimal
  // and ideally we'd do this at the same time when transferring the proxies
  // from the StoredPage into RenderFrameHostManager. However, this is a
  // temporary solution until we move this into BrowsingContextState,
  // along with RenderFrameProxyHost.
  page->render_frame_host()->frame_tree_node()->set_frame_name_for_activation(
      prior_replication_state.unique_name, prior_replication_state.name);
  for (auto& it : page->proxy_hosts()) {
    it.second->set_frame_tree_node(*(target_frame_tree.root()));
  }

  // Iterate over the root RenderFrameHost's subframes and update the
  // associated frame tree. Note that subframe proxies don't need their
  // FrameTrees independently updated, since their FrameTreeNodes don't
  // change, and FrameTree references in those FrameTreeNodes will be updated
  // through RenderFrameHosts.
  //
  // TODO(https://crbug.com/1199693): Need to investigate if and how
  // pending delete RenderFrameHost objects should be handled if prerendering
  // runs all of the unload handlers; they are not currently handled here.
  // This is because pending delete RenderFrameHosts can still receive and
  // process some messages while the RenderFrameHost FrameTree and
  // FrameTreeNode are stale.
  for (FrameTreeNode* subframe_node : subframe_nodes) {
    subframe_node->SetFrameTree(target_frame_tree);
  }

  page->render_frame_host()->ForEachRenderFrameHostIncludingSpeculative(
      [this](RenderFrameHostImpl* rfh) {
        // The visibility state of the prerendering page has not been
        // updated by
        // WebContentsImpl::UpdateVisibilityAndNotifyPageAndView(). So
        // updates the visibility state using the PageVisibilityState of
        // |web_contents|.
        rfh->render_view_host()->SetFrameTreeVisibility(
            web_contents_->GetPageVisibilityState());
      });

  frame_tree_->Shutdown();
  frame_tree_.reset();

  for (auto& observer : observers_)
    observer.OnActivated();

  // The activated page is on the primary tree now. It can propagate the client
  // hints to the global settings.
  BrowserContext* browser_context =
      target_frame_tree.controller().GetBrowserContext();
  ClientHintsControllerDelegate* client_hints_delegate =
      browser_context->GetClientHintsControllerDelegate();
  if (client_hints_delegate) {
    for (auto& [origin, client_hint] : client_hints_type_) {
      PersistAcceptCH(origin, *(target_frame_tree.root()),
                      client_hints_delegate, client_hint);
    }
  }

  // TODO(crbug.com/1299330): Replace
  // `navigation_request.GetNextPageUkmSourceId()` with prerendered page's UKM
  // source ID.
  RecordFinalStatus(PrerenderFinalStatus::kActivated,
                    attributes_.initiator_ukm_id,
                    navigation_request.GetNextPageUkmSourceId());

  // Prerender is activated. Set the status to kSuccess.
  SetTriggeringOutcome(PreloadingTriggeringOutcome::kSuccess);

  devtools_instrumentation::DidActivatePrerender(navigation_request);
  return page;
}

// Ensure that the frame policies are compatible between primary main frame and
// prerendering main frame:
// a) primary main frame's pending_frame_policy would normally apply to the new
// document during its creation. However, for prerendering we can't apply it as
// the document is already created.
// b) prerender main frame's pending_frame_policy can't be transferred to the
// primary main frame, we should not activate if it's non-zero.
// c) Existing  document can't change the frame_policy it is affected by, so we
// can't transfer RenderFrameHosts between FrameTreeNodes with different frame
// policies.
//
// Usually frame policy for the main frame is empty as in the most common case a
// parent document sets a policy on the child iframe.
bool PrerenderHost::IsFramePolicyCompatibleWithPrimaryFrameTree() {
  FrameTreeNode* prerender_root_ftn = frame_tree_->root();
  FrameTreeNode* primary_root_ftn = web_contents_->GetPrimaryFrameTree().root();

  // Ensure that the pending frame policy is not set on the main frames, as it
  // is usually set on frames by their parent frames.
  if (prerender_root_ftn->pending_frame_policy() != blink::FramePolicy()) {
    return false;
  }

  if (primary_root_ftn->pending_frame_policy() != blink::FramePolicy()) {
    return false;
  }

  if (prerender_root_ftn->current_replication_state().frame_policy !=
      primary_root_ftn->current_replication_state().frame_policy) {
    return false;
  }

  return true;
}

bool PrerenderHost::AreInitialPrerenderNavigationParamsCompatibleWithNavigation(
    NavigationRequest& navigation_request) {
  // TODO(crbug.com/1181763): compare the rest of the navigation parameters. We
  // should introduce compile-time parameter checks as well, to ensure how new
  // fields should be compared for compatibility.

  // As the initial prerender navigation is a) limited to HTTP(s) URLs and b)
  // initiated by the PrerenderHost, we do not expect some navigation parameters
  // connected to certain navigation types to be set and the DCHECKS below
  // enforce that.
  // The parameters of the potential activation, however, are coming from the
  // renderer and we mostly don't have any guarantees what they are, so we
  // should not DCHECK them. Instead, by default we compare them with initial
  // prerender activation parameters and fail to activate when they differ.
  // Note: some of those parameters should be never set (or should be ignored)
  // for main-frame / HTTP(s) navigations, but we still compare them here as a
  // defence-in-depth measure.
  DCHECK(navigation_request.IsInPrimaryMainFrame());

  // Compare BeginNavigationParams.
  ActivationNavigationParamsMatch result =
      AreBeginNavigationParamsCompatibleWithNavigation(
          navigation_request.begin_params());
  if (result != ActivationNavigationParamsMatch::kOk) {
    RecordPrerenderActivationNavigationParamsMatch(result, trigger_type(),
                                                   embedder_histogram_suffix());
    return false;
  }

  // Compare CommonNavigationParams.
  result = AreCommonNavigationParamsCompatibleWithNavigation(
      navigation_request.common_params());
  if (result != ActivationNavigationParamsMatch::kOk) {
    RecordPrerenderActivationNavigationParamsMatch(result, trigger_type(),
                                                   embedder_histogram_suffix());
    return false;
  }

  RecordPrerenderActivationNavigationParamsMatch(
      ActivationNavigationParamsMatch::kOk, trigger_type(),
      embedder_histogram_suffix());
  return true;
}

PrerenderHost::ActivationNavigationParamsMatch
PrerenderHost::AreBeginNavigationParamsCompatibleWithNavigation(
    const blink::mojom::BeginNavigationParams& potential_activation) {
  if (potential_activation.initiator_frame_token !=
      begin_params_->initiator_frame_token) {
    return ActivationNavigationParamsMatch::kInitiatorFrameToken;
  }

  if (!AreHttpRequestHeadersCompatible(potential_activation.headers,
                                       begin_params_->headers, trigger_type(),
                                       embedder_histogram_suffix())) {
    return ActivationNavigationParamsMatch::kHttpRequestHeader;
  }

  // Don't activate a prerendered page if the potential activation request
  // requires validation or bypass of the browser cache, as the prerendered page
  // is a kind of caches.
  // TODO(https://crbug.com/1213299): Instead of checking the load flags on
  // activation, we should cancel prerendering when the prerender initial
  // navigation has the flags.
  int cache_load_flags = net::LOAD_VALIDATE_CACHE | net::LOAD_BYPASS_CACHE |
                         net::LOAD_DISABLE_CACHE;
  if (potential_activation.load_flags & cache_load_flags) {
    return ActivationNavigationParamsMatch::kCacheLoadFlags;
  }
  if (potential_activation.load_flags != begin_params_->load_flags) {
    return ActivationNavigationParamsMatch::kLoadFlags;
  }

  if (potential_activation.skip_service_worker !=
      begin_params_->skip_service_worker) {
    return ActivationNavigationParamsMatch::kSkipServiceWorker;
  }

  if (potential_activation.mixed_content_context_type !=
      begin_params_->mixed_content_context_type) {
    return ActivationNavigationParamsMatch::kMixedContentContextType;
  }

  // Initial prerender navigation cannot be a form submission.
  DCHECK(!begin_params_->is_form_submission);
  if (potential_activation.is_form_submission !=
      begin_params_->is_form_submission) {
    return ActivationNavigationParamsMatch::kIsFormSubmission;
  }

  if (potential_activation.searchable_form_url !=
      begin_params_->searchable_form_url) {
    return ActivationNavigationParamsMatch::kSearchableFormUrl;
  }

  if (potential_activation.searchable_form_encoding !=
      begin_params_->searchable_form_encoding) {
    return ActivationNavigationParamsMatch::kSearchableFormEncoding;
  }

  // Trust token params can be set only on subframe navigations, so both values
  // should be null here.
  DCHECK(!begin_params_->trust_token_params);
  if (potential_activation.trust_token_params !=
      begin_params_->trust_token_params) {
    return ActivationNavigationParamsMatch::kTrustTokenParams;
  }

  // Web bundle token cannot be set due because it is only set for child
  // frame navigations.
  DCHECK(!begin_params_->web_bundle_token);
  if (potential_activation.web_bundle_token) {
    return ActivationNavigationParamsMatch::kWebBundleToken;
  }

  // Don't require equality for request_context_type because link clicks
  // (HYPERLINK) should be allowed for activation, whereas prerender always has
  // type LOCATION.
  DCHECK_EQ(begin_params_->request_context_type,
            blink::mojom::RequestContextType::LOCATION);
  switch (potential_activation.request_context_type) {
    case blink::mojom::RequestContextType::HYPERLINK:
    case blink::mojom::RequestContextType::LOCATION:
      break;
    default:
      return ActivationNavigationParamsMatch::kRequestContextType;
  }

  // Since impression should not be set, no need to compare contents.
  DCHECK(!begin_params_->impression);
  if (potential_activation.impression.has_value()) {
    return ActivationNavigationParamsMatch::kImpressionHasValue;
  }

  // No need to test for devtools_initiator because this field is used for
  // tracking what triggered a network request, and prerender activation will
  // not use network requests.

  return ActivationNavigationParamsMatch::kOk;
}

PrerenderHost::ActivationNavigationParamsMatch
PrerenderHost::AreCommonNavigationParamsCompatibleWithNavigation(
    const blink::mojom::CommonNavigationParams& potential_activation) {
  // The CommonNavigationParams::url field is expected to be the same for both
  // initial and activation prerender navigations, as the PrerenderHost
  // selection would have already checked for matching values. Adding a DCHECK
  // here to be safe.
  if (attributes_.url_match_predicate) {
    DCHECK(
        attributes_.url_match_predicate.value().Run(potential_activation.url));
  } else {
    DCHECK_EQ(potential_activation.url, common_params_->url);
  }
  if (potential_activation.initiator_origin !=
      common_params_->initiator_origin) {
    return ActivationNavigationParamsMatch::kInitiatorOrigin;
  }

  if (potential_activation.transition != common_params_->transition) {
    return ActivationNavigationParamsMatch::kTransition;
  }

  DCHECK_EQ(common_params_->navigation_type,
            blink::mojom::NavigationType::DIFFERENT_DOCUMENT);
  if (potential_activation.navigation_type != common_params_->navigation_type) {
    return ActivationNavigationParamsMatch::kNavigationType;
  }

  // We don't check download_policy as it affects whether the download triggered
  // by the NavigationRequest is allowed to proceed (or logs metrics) and
  // doesn't affect the behaviour of the document created by a non-download
  // navigation after commit (e.g. it doesn't affect future downloads in child
  // frames). PrerenderNavigationThrottle has already ensured that the initial
  // prerendering navigation isn't a download and as prerendering activation
  // won't reach out to the network, it won't turn into a navigation as well.

  DCHECK(common_params_->base_url_for_data_url.is_empty());
  if (potential_activation.base_url_for_data_url !=
      common_params_->base_url_for_data_url) {
    return ActivationNavigationParamsMatch::kBaseUrlForDataUrl;
  }

  // The method parameter is compared only by DCHECK_EQ because that change is
  // detected earlier by checking the HTTP request headers changes.
  DCHECK_EQ(potential_activation.method, common_params_->method);

  // Initial prerender navigation can't be a form submission.
  DCHECK(!common_params_->post_data);
  if (potential_activation.post_data != common_params_->post_data) {
    return ActivationNavigationParamsMatch::kPostData;
  }

  // No need to compare source_location, as it's only passed to the DevTools for
  // debugging purposes and does not impact the properties of the document
  // created by this navigation.

  DCHECK(!common_params_->started_from_context_menu);
  if (potential_activation.started_from_context_menu !=
      common_params_->started_from_context_menu) {
    return ActivationNavigationParamsMatch::kStartedFromContextMenu;
  }

  // has_user_gesture doesn't affect any of the security properties of the
  // document created by navigation, so equality of the values is not required.
  // TODO(crbug.com/1232915): ensure that the user activation status is
  // propagated to the activated document.

  // text_fragment_token doesn't affect any of the security properties of the
  // document created by navigation, so equality of the values is not required.
  // TODO(crbug.com/1232919): ensure the activated document consumes
  // text_fragment_token and scrolls to the corresponding viewport.

  // No need to compare should_check_main_world_csp, as if the CSP blocks the
  // initial navigation, it cancels prerendering, and we don't reach here for
  // matching. So regardless of the activation's capability to bypass the main
  // world CSP, the prerendered page is eligible for the activation. This also
  // permits content scripts to activate the page.

  if (potential_activation.initiator_origin_trial_features !=
      common_params_->initiator_origin_trial_features) {
    return ActivationNavigationParamsMatch::kInitiatorOriginTrialFeature;
  }

  if (potential_activation.href_translate != common_params_->href_translate) {
    return ActivationNavigationParamsMatch::kHrefTranslate;
  }

  // Initial prerender navigation can't be a history navigation.
  DCHECK(!common_params_->is_history_navigation_in_new_child_frame);
  if (potential_activation.is_history_navigation_in_new_child_frame !=
      common_params_->is_history_navigation_in_new_child_frame) {
    return ActivationNavigationParamsMatch::kIsHistoryNavigationInNewChildFrame;
  }

  // We intentionally don't check referrer or referrer->policy. See spec
  // discussion at https://github.com/WICG/nav-speculation/issues/18.

  if (potential_activation.request_destination !=
      common_params_->request_destination) {
    return ActivationNavigationParamsMatch::kRequestDestination;
  }

  return ActivationNavigationParamsMatch::kOk;
}

RenderFrameHostImpl* PrerenderHost::GetPrerenderedMainFrameHost() {
  DCHECK(frame_tree_);
  DCHECK(frame_tree_->root()->current_frame_host());
  return frame_tree_->root()->current_frame_host();
}

FrameTree& PrerenderHost::GetPrerenderFrameTree() {
  DCHECK(frame_tree_);
  return *frame_tree_;
}

void PrerenderHost::RecordFinalStatus(base::PassKey<PrerenderHostRegistry>,
                                      PrerenderFinalStatus status) {
  RecordFinalStatus(status, attributes_.initiator_ukm_id,
                    ukm::kInvalidSourceId);

  // Set failure reason for this PreloadingAttempt specific to the
  // FinalStatus.
  SetFailureReason(status);
}

PrerenderHost::LoadingOutcome PrerenderHost::WaitForLoadStopForTesting() {
  LoadingOutcome status = LoadingOutcome::kLoadingCompleted;

  if (!frame_tree_->IsLoadingIncludingInnerFrameTrees() &&
      GetInitialNavigationId().has_value())
    return status;

  base::RunLoop loop;
  on_wait_loading_finished_ = base::BindOnce(
      [](base::OnceClosure on_close, LoadingOutcome* result,
         LoadingOutcome status) {
        *result = status;
        std::move(on_close).Run();
      },
      loop.QuitClosure(), &status);
  loop.Run();
  return status;
}

void PrerenderHost::RecordFinalStatus(PrerenderFinalStatus status,
                                      ukm::SourceId initiator_ukm_id,
                                      ukm::SourceId prerendered_ukm_id) {
  DCHECK(!final_status_);
  final_status_ = status;
  RecordPrerenderFinalStatus(status, attributes_, prerendered_ukm_id);
}

const GURL& PrerenderHost::GetInitialUrl() const {
  return attributes_.prerendering_url;
}

void PrerenderHost::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void PrerenderHost::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

absl::optional<int64_t> PrerenderHost::GetInitialNavigationId() const {
  return initial_navigation_id_;
}

void PrerenderHost::SetInitialNavigation(NavigationRequest* navigation) {
  DCHECK(!initial_navigation_id_.has_value());
  initial_navigation_id_ = navigation->GetNavigationId();
  begin_params_ = navigation->begin_params().Clone();
  common_params_ = navigation->common_params().Clone();

  // The prerendered page should be checked by the main world CSP. See also
  // relevant comments in AreCommonNavigationParamsCompatibleWithNavigation().
  DCHECK_EQ(common_params_->should_check_main_world_csp,
            network::mojom::CSPDisposition::CHECK);
}

void PrerenderHost::SetTriggeringOutcome(PreloadingTriggeringOutcome outcome) {
  if (!attempt_)
    return;

  attempt_->SetTriggeringOutcome(outcome);
}

void PrerenderHost::SetEligibility(PreloadingEligibility eligibility) {
  if (!attempt_)
    return;

  attempt_->SetEligibility(eligibility);
}

void PrerenderHost::SetFailureReason(PrerenderFinalStatus status) {
  if (!attempt_)
    return;

  switch (status) {
    // When adding a new failure reason, consider whether it should be
    // propagated to `attempt_`. Most values should be propagated, but we
    // explicitly do not propagate failure reasons if:
    // 1. the prerender was actually successful (kActivated).
    // 2. prerender was successfully prepared but then destroyed because it
    //    wasn't needed for a subsequent navigation (kTriggerDestroyed).
    // 3. the prerender was still pending for its initial navigation when it was
    //    activated (kActivatedBeforeStarted).
    case PrerenderFinalStatus::kActivated:
    case PrerenderFinalStatus::kTriggerDestroyed:
    case PrerenderFinalStatus::kActivatedBeforeStarted:
      return;
    case PrerenderFinalStatus::kDestroyed:
    case PrerenderFinalStatus::kLowEndDevice:
    case PrerenderFinalStatus::kInvalidSchemeRedirect:
    case PrerenderFinalStatus::kInvalidSchemeNavigation:
    case PrerenderFinalStatus::kInProgressNavigation:
    case PrerenderFinalStatus::kNavigationRequestBlockedByCsp:
    case PrerenderFinalStatus::kMainFrameNavigation:
    case PrerenderFinalStatus::kMojoBinderPolicy:
    case PrerenderFinalStatus::kRendererProcessCrashed:
    case PrerenderFinalStatus::kRendererProcessKilled:
    case PrerenderFinalStatus::kDownload:
    case PrerenderFinalStatus::kNavigationNotCommitted:
    case PrerenderFinalStatus::kNavigationBadHttpStatus:
    case PrerenderFinalStatus::kClientCertRequested:
    case PrerenderFinalStatus::kNavigationRequestNetworkError:
    case PrerenderFinalStatus::kMaxNumOfRunningPrerendersExceeded:
    case PrerenderFinalStatus::kCancelAllHostsForTesting:
    case PrerenderFinalStatus::kDidFailLoad:
    case PrerenderFinalStatus::kStop:
    case PrerenderFinalStatus::kSslCertificateError:
    case PrerenderFinalStatus::kLoginAuthRequested:
    case PrerenderFinalStatus::kUaChangeRequiresReload:
    case PrerenderFinalStatus::kBlockedByClient:
    case PrerenderFinalStatus::kAudioOutputDeviceRequested:
    case PrerenderFinalStatus::kMixedContent:
    case PrerenderFinalStatus::kTriggerBackgrounded:
    case PrerenderFinalStatus::kEmbedderTriggeredAndCrossOriginRedirected:
    case PrerenderFinalStatus::kMemoryLimitExceeded:
    case PrerenderFinalStatus::kFailToGetMemoryUsage:
    case PrerenderFinalStatus::kDataSaverEnabled:
    case PrerenderFinalStatus::kHasEffectiveUrl:
    case PrerenderFinalStatus::kInactivePageRestriction:
    case PrerenderFinalStatus::kStartFailed:
    case PrerenderFinalStatus::kTimeoutBackgrounded:
    case PrerenderFinalStatus::kCrossSiteNavigation:
    case PrerenderFinalStatus::kCrossSiteRedirect:
    case PrerenderFinalStatus::kSameSiteCrossOriginRedirect:
    case PrerenderFinalStatus::kSameSiteCrossOriginNavigation:
    case PrerenderFinalStatus::kSameSiteCrossOriginRedirectNotOptIn:
    case PrerenderFinalStatus::kSameSiteCrossOriginNavigationNotOptIn:
    case PrerenderFinalStatus::kActivationNavigationParameterMismatch:
    case PrerenderFinalStatus::kEmbedderHostDisallowed:
      attempt_->SetFailureReason(ToPreloadingFailureReason(status));
      // We reset the attempt to ensure we don't update once we have reported it
      // as failure or accidentally use it for any other prerender attempts as
      // PrerenderHost deletion is async.
      attempt_.reset();
      return;
  }
}

bool PrerenderHost::IsUrlMatch(const GURL& url) const {
  // If the trigger defines its predicate, respect it.
  if (attributes_.url_match_predicate) {
    // Triggers are not allowed to treat a cross-origin url as a matched url. It
    // would cause security risks.
    if (!url::IsSameOriginWith(attributes_.prerendering_url, url))
      return false;
    return attributes_.url_match_predicate.value().Run(url);
  }
  return GetInitialUrl() == url;
}

void PrerenderHost::OnAcceptClientHintChanged(
    const url::Origin& origin,
    const std::vector<network::mojom::WebClientHintsType>& client_hints_type) {
  client_hints_type_[origin] = client_hints_type;
}

void PrerenderHost::GetAllowedClientHintsOnPage(
    const url::Origin& origin,
    blink::EnabledClientHints* client_hints) const {
  if (!client_hints_type_.contains(origin))
    return;
  for (const auto& hint : client_hints_type_.at(origin)) {
    client_hints->SetIsEnabled(hint, true);
  }
}

void PrerenderHost::Cancel(PrerenderFinalStatus status) {
  TRACE_EVENT("navigation", "PrerenderHost::Cancel", "final_status", status);
  // Already cancelled.
  if (final_status_)
    return;

  RenderFrameHostImpl* host = PrerenderHost::GetPrerenderedMainFrameHost();
  DCHECK(host);
  PrerenderHostRegistry* registry =
      host->delegate()->GetPrerenderHostRegistry();
  DCHECK(registry);
  registry->CancelHost(frame_tree_node_id_, status);
}

}  // namespace content