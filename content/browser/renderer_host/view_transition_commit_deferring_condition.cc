// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/view_transition_commit_deferring_condition.h"

#include "base/memory/ptr_util.h"
#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/public/common/content_features.h"
#include "third_party/blink/public/common/features_generated.h"
#include "third_party/blink/public/common/frame/view_transition_state.h"

namespace content {
namespace {

void OnSnapshotAck(base::OnceClosure closure,
                   NavigationRequest* navigation_request,
                   const blink::ViewTransitionState& view_transition_state) {
  if (!view_transition_state.elements.empty()) {
    navigation_request->SetViewTransitionState(
        std::move(view_transition_state));
  }
  std::move(closure).Run();
}

}  // namespace

// static
std::unique_ptr<CommitDeferringCondition>
ViewTransitionCommitDeferringCondition::MaybeCreate(
    NavigationRequest& navigation_request) {
  // TODO(khushalsagar): This shouldn't be done for every navigation. We'll need
  // a meta tag (or another way) in the API to know whether this Document is
  // interested in enabling transitions for same-origin navigations.
  if (!base::FeatureList::IsEnabled(
          blink::features::kViewTransitionOnNavigation)) {
    return nullptr;
  }

  if (!navigation_request.IsInPrimaryMainFrame())
    return nullptr;

  const bool is_same_origin =
      navigation_request.frame_tree_node()
          ->current_frame_host()
          ->GetLastCommittedOrigin() == navigation_request.GetOriginToCommit();
  if (!is_same_origin)
    return nullptr;

  return base::WrapUnique(
      new ViewTransitionCommitDeferringCondition(navigation_request));
}

ViewTransitionCommitDeferringCondition::ViewTransitionCommitDeferringCondition(
    NavigationRequest& navigation_request)
    : CommitDeferringCondition(navigation_request) {}

ViewTransitionCommitDeferringCondition::
    ~ViewTransitionCommitDeferringCondition() = default;

CommitDeferringCondition::Result
ViewTransitionCommitDeferringCondition::WillCommitNavigation(
    base::OnceClosure resume) {
  auto* navigation_request = NavigationRequest::From(&GetNavigationHandle());
  auto* render_frame_host =
      navigation_request->frame_tree_node()->current_frame_host();

  // TODO(crbug.com/1372584):  Implement a timeout, to avoid blocking the
  // navigation for too long.
  render_frame_host->SnapshotDocumentForViewTransition(
      base::BindOnce(&OnSnapshotAck, std::move(resume), navigation_request));
  return Result::kDefer;
}

}  // namespace content
