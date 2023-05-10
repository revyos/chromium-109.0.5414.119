// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/profile_management_flow_controller.h"

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/profiles/profile_management_step_controller.h"
#include "chrome/browser/ui/views/profiles/profile_management_utils.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"

ProfileManagementFlowController::ProfileManagementFlowController(
    ProfilePickerWebContentsHost* host,
    ClearHostClosure clear_host_callback,
    Step initial_step)
    : initial_step_(initial_step),
      host_(host),
      clear_host_callback_(std::move(clear_host_callback)) {}

ProfileManagementFlowController::~ProfileManagementFlowController() = default;

void ProfileManagementFlowController::Init(
    base::OnceCallback<void(bool)> initial_step_switch_finished_callback) {
  DCHECK(clear_host_callback_.value());
  SwitchToStep(initial_step(), /*reset_state=*/true,
               /*pop_step_callback=*/base::OnceClosure(),
               std::move(initial_step_switch_finished_callback));
}

void ProfileManagementFlowController::SwitchToStep(
    Step step,
    bool reset_state,
    base::OnceClosure pop_step_callback,
    base::OnceCallback<void(bool)> step_switch_finished_callback) {
  DCHECK_NE(Step::kUnknown, step);
  DCHECK_NE(current_step_, step);

  auto* new_step_controller = initialized_steps_.at(step).get();
  DCHECK(new_step_controller);
  new_step_controller->set_pop_step_callback(std::move(pop_step_callback));
  new_step_controller->Show(std::move(step_switch_finished_callback),
                            reset_state);

  if (initialized_steps_.contains(current_step_)) {
    initialized_steps_.at(current_step_)->OnHidden();
  }

  current_step_ = step;
}

void ProfileManagementFlowController::OnNavigateBackRequested() {
  DCHECK(initialized_steps_.contains(current_step_));
  initialized_steps_.at(current_step_)->OnNavigateBackRequested();
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
void ProfileManagementFlowController::OnReloadRequested() {
  DCHECK(initialized_steps_.contains(current_step_));
  initialized_steps_.at(current_step_)->OnReloadRequested();
}
#endif

void ProfileManagementFlowController::RegisterStep(
    Step step,
    std::unique_ptr<ProfileManagementStepController> step_controller) {
  initialized_steps_[step] = std::move(step_controller);
}

void ProfileManagementFlowController::UnregisterStep(Step step) {
  initialized_steps_.erase(step);
}

bool ProfileManagementFlowController::IsStepInitialized(Step step) const {
  return initialized_steps_.contains(step) && initialized_steps_.at(step);
}

void ProfileManagementFlowController::ExitFlow() {
  DCHECK(clear_host_callback_.value());
  std::move(clear_host_callback_.value()).Run();
}

void ProfileManagementFlowController::FinishFlowAndRunInBrowser(
    Profile* profile,
    PostHostClearedCallback post_host_cleared_callback) {
  DCHECK(clear_host_callback_.value());  // The host shouldn't be cleared yet.

  // TODO(crbug.com/1370485): Update when we have a more elegant way to ignore
  // the incoming argument.
  base::OnceCallback<void(Profile*)> post_browser_open_callback =
      base::BindOnce([](Profile* absorb_arg) {
      }).Then(std::move(clear_host_callback_.value()));

  if (!post_host_cleared_callback->is_null()) {
    post_browser_open_callback =
        std::move(post_browser_open_callback)
            .Then(base::BindOnce(&chrome::FindLastActiveWithProfile,
                                 // Unretained ok: we'll have a browser window
                                 // open for `profile`.
                                 base::Unretained(profile)))
            .Then(std::move(post_host_cleared_callback.value()));
  }

  // Start by opening the browser window, to ensure that we have another
  // KeepAlive for `profile` by the time we clear the flow and its host.
  // TODO(crbug.com/1374315): Make sure we do something or log an error if
  // opening a browser window was not possible.
  profiles::OpenBrowserWindowForProfile(
      std::move(post_browser_open_callback),
      /*always_create=*/false,   // Don't create a window if one already exists.
      /*is_new_profile=*/false,  // Don't create a first run window.
      /*unblock_extensions=*/false,  // There is no need to unblock all
                                     // extensions because we only open browser
                                     // window if the Profile is not locked.
                                     // Hence there is no extension blocked.
      profile);
}