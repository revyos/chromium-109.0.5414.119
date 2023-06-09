// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/actions/expect_navigation_action.h"

#include <utility>

#include "base/callback.h"
#include "components/autofill_assistant/browser/actions/action_delegate.h"

namespace autofill_assistant {

ExpectNavigationAction::ExpectNavigationAction(ActionDelegate* delegate,
                                               const ActionProto& proto)
    : Action(delegate, proto) {
  DCHECK(proto_.has_expect_navigation());
}

ExpectNavigationAction::~ExpectNavigationAction() {}

void ExpectNavigationAction::InternalProcessAction(
    ProcessActionCallback callback) {
  delegate_->ExpectNavigation();
  UpdateProcessedAction(ACTION_APPLIED);
  std::move(callback).Run(std::move(processed_action_proto_));
}

}  // namespace autofill_assistant
