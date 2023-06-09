// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/actions/navigate_action.h"

#include <memory>
#include <utility>

#include "base/callback.h"
#include "components/autofill_assistant/browser/actions/action_delegate.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace autofill_assistant {

NavigateAction::NavigateAction(ActionDelegate* delegate,
                               const ActionProto& proto)
    : Action(delegate, proto) {
  DCHECK(proto_.has_navigate());
}

NavigateAction::~NavigateAction() {}

void NavigateAction::InternalProcessAction(ProcessActionCallback callback) {
  // We know to expect navigation to happen, since we're about to cause it. This
  // allows scripts to put wait_for_navigation just after navigate, if needed,
  // without having to add an expect_navigation first.
  // The navigations are not declared as renderer initiated, they can cause
  // failures. Setting the navigation to expected will prevent this from
  // happening.

  auto& proto = proto_.navigate();
  if (!proto.url().empty()) {
    delegate_->ExpectNavigation();
    GURL url(proto_.navigate().url());
    delegate_->LoadURL(url);
    UpdateProcessedAction(ACTION_APPLIED);
  } else if (proto.go_backward()) {
    auto& controller = delegate_->GetWebContents()->GetController();
    if (controller.CanGoBack()) {
      delegate_->ExpectNavigation();
      controller.GoBack();
      UpdateProcessedAction(ACTION_APPLIED);
    } else {
      UpdateProcessedAction(PRECONDITION_FAILED);
    }
  } else if (proto.go_forward()) {
    auto& controller = delegate_->GetWebContents()->GetController();
    if (controller.CanGoForward()) {
      delegate_->ExpectNavigation();
      controller.GoForward();
      UpdateProcessedAction(ACTION_APPLIED);
    } else {
      UpdateProcessedAction(PRECONDITION_FAILED);
    }
  } else {
    UpdateProcessedAction(UNSUPPORTED);
  }

  std::move(callback).Run(std::move(processed_action_proto_));
}

}  // namespace autofill_assistant
