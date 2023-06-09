// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PROFILES_PROFILE_MANAGEMENT_FLOW_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PROFILES_PROFILE_MANAGEMENT_FLOW_CONTROLLER_H_

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/profiles/profile_management_utils.h"
#include "chrome/browser/ui/views/profiles/profile_picker_web_contents_host.h"
#include "components/signin/public/base/signin_buildflags.h"

class Profile;
class ProfileManagementStepController;
class ProfilePickerWebContentsHost;

// Represents an abstract user facing flow related to profile management.
//
// A profile management flow is made of a series of steps, implemented as
// `ProfileManagementStepController`s and owned by this object.
//
// Typical usage starts with calling `Init()` on the instantiated flow, which
// will switch to the `initial_step()`. Then as the user interacts with the
// flow, this controller will handle instantiating and navigating between the
// steps.
class ProfileManagementFlowController {
 public:
  // TODO(https://crbug.com/1358843): Split the steps more granularly across
  // logical steps instead of according to implementation details.
  enum class Step {
    kUnknown,
    // Renders the `chrome://profile-picker` app, covering the profile picker,
    // the profile type choice at the beginning of the profile creation
    // flow and the account selection on Lacros.
    kProfilePicker,
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    // Renders the sign in screen on Dice platforms.
    // TODO(https://crbug.com/1360773): Support the `kAccountSelection` step on
    // Lacros. Picking an account during the `kLacrosSelectAvailableAccount`
    // flow and the profile creation should be implemented as a standalone step.
    kAccountSelection,
    // Moves the rest of the flow to a browser tab so that the user can complete
    // the SAML sign in they started at the previous step.
    kFinishSamlSignin,
#endif
    // Renders all post-sign in screens: enterprise management consent, profile
    // switch, sync opt-in, etc.
    kPostSignInFlow,

    // Renders the beginning of the First Run Experience.
    kIntro,
  };

  // Creates a flow controller that will advance to `initial_step` when it is
  // `Init()`-ed.
  // `clear_host_callback` will be called if `host` needs to be closed.
  explicit ProfileManagementFlowController(ProfilePickerWebContentsHost* host,
                                           ClearHostClosure clear_host_callback,
                                           Step initial_step);
  virtual ~ProfileManagementFlowController();

  // Switches to the `initial_step()`.
  // If `initial_step_switch_finished_callback` is provided, it will be called
  // with `true` when the navigation to the initial step succeeded, or with
  // `false` otherwise.
  virtual void Init(
      base::OnceCallback<void(bool)> initial_step_switch_finished_callback =
          base::OnceCallback<void(bool success)>());

  // Instructs a step registered as `step` to be shown.
  // If `step_switch_finished_callback` is provided, it will be called
  // with `true` when the navigation to `step` succeeded, or with
  // `false` otherwise.
  // Also see `ProfileManagementStepController::Show()`.
  void SwitchToStep(
      Step step,
      bool reset_state,
      base::OnceClosure pop_step_callback = base::OnceClosure(),
      base::OnceCallback<void(bool)> step_switch_finished_callback =
          base::OnceCallback<void(bool success)>());

  void OnNavigateBackRequested();

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  void OnReloadRequested();
#endif

 protected:
  void RegisterStep(Step step,
                    std::unique_ptr<ProfileManagementStepController>);

  void UnregisterStep(Step step);

  bool IsStepInitialized(Step step) const;

  // Clears the flow, causing the `host()` to be deleted.
  void ExitFlow();

  // Opens a browser window for `profile`, closes the flow and then runs
  // `callback`.
  //
  // Since the flow and its host will be destroyed by the time `callback` runs,
  // it should be longer lived than these.
  void FinishFlowAndRunInBrowser(Profile* profile,
                                 PostHostClearedCallback callback);

  Step current_step() const { return current_step_; }

  Step initial_step() const { return initial_step_; }

  ProfilePickerWebContentsHost* host() { return host_; }

 private:
  Step current_step_ = Step::kUnknown;

  Step initial_step_;

  raw_ptr<ProfilePickerWebContentsHost> host_;
  ClearHostClosure clear_host_callback_;

  base::flat_map<Step, std::unique_ptr<ProfileManagementStepController>>
      initialized_steps_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_PROFILES_PROFILE_MANAGEMENT_FLOW_CONTROLLER_H_
