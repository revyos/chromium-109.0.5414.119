// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_ASSISTANT_PASSWORD_CHANGE_PASSWORD_CHANGE_RUN_CONTROLLER_H_
#define CHROME_BROWSER_UI_AUTOFILL_ASSISTANT_PASSWORD_CHANGE_PASSWORD_CHANGE_RUN_CONTROLLER_H_

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "components/autofill_assistant/browser/public/password_change/proto/actions.pb.h"

class PasswordChangeRunDisplay;
class GURL;

// Abstract interface for a controller of an `PasswordChangeRunDisplay`.
class PasswordChangeRunController {
 public:
  // Defines the current UI state to restore pre-interrupt UI.
  // Interrupts are not triggered during prompts, therefore
  // there is no need to keep their state.
  struct Model {
    Model();
    Model(Model& other);
    Model(Model&& other);
    Model& operator=(const Model& other);
    Model& operator=(Model&& other);

    ~Model();

    std::u16string title;
    std::u16string accessibility_title;
    autofill_assistant::password_change::TopIcon top_icon;
    std::u16string description;
    autofill_assistant::password_change::ProgressStep progress_step;
    // Whether the icon animation on the progress bar is paused.
    bool progress_bar_animation_is_paused = false;
    // The flow type (i.e. password change vs. password reset). It is used to
    // decide which completion screen to display at the end.
    autofill_assistant::password_change::FlowType flow_type =
        autofill_assistant::password_change::FlowType::FLOW_TYPE_UNSPECIFIED;
  };

  // Factory function to create the controller.
  static std::unique_ptr<PasswordChangeRunController> Create();

  virtual ~PasswordChangeRunController() = default;

  // Shows the `PasswordChangeRunDisplay`.
  virtual void Show(
      base::WeakPtr<PasswordChangeRunDisplay> password_change_run_display) = 0;

  // The below methods are used to set UI.
  // They all persist state to a model owned by the controller and call the
  // sibling view methods.
  virtual void SetTopIcon(
      autofill_assistant::password_change::TopIcon top_icon) = 0;
  // Sets the title shown in the side panel. If `accessibility_title` is not set
  // defaults to `title`.
  virtual void SetTitle(const std::u16string& title,
                        const std::u16string& accessibility_title) = 0;
  virtual void SetDescription(const std::u16string& description) = 0;
  virtual void SetProgressBarStep(
      autofill_assistant::password_change::ProgressStep progress_step) = 0;

  // Shows a base type prompt and receives the response from the view.
  virtual void ShowBasePrompt(
      const autofill_assistant::password_change::BasePromptSpecification&
          base_prompt) = 0;
  virtual void OnBasePromptChoiceSelected(size_t choice_index) = 0;

  // Shows a generated password prompt and receives the response from the view.
  virtual void ShowUseGeneratedPasswordPrompt(
      const autofill_assistant::password_change::
          UseGeneratedPasswordPromptSpecification& password_prompt,
      const std::u16string& suggested_password) = 0;
  // Called on user interaction with the prompt. `selected` indicates whether
  // the automatically generated password was selected or not.
  virtual void OnGeneratedPasswordSelected(bool selected) = 0;

  // Shows the intermediate starting screen until first actions are received
  // from the script controller.
  virtual void ShowStartingScreen(const GURL& url) = 0;

  // Shows the ending screen that is displayed after script completion.
  virtual void ShowCompletionScreen(
      base::RepeatingClosure done_button_callback) = 0;

  // Opens Chrome's password manager.
  virtual void OpenPasswordManager() = 0;

  // Shows the error screen.
  virtual void ShowErrorScreen() = 0;

  // Returns whether a password change run has resulted in a successfully
  // changed password.
  virtual bool PasswordWasSuccessfullyChanged() = 0;

  // Pauses and resumes that icon animation in the progress bar.
  virtual void PauseProgressBarAnimation() = 0;
  virtual void ResumeProgressBarAnimation() = 0;

  // Returns a weak pointer to this controller.
  virtual base::WeakPtr<PasswordChangeRunController> GetWeakPtr() = 0;

 protected:
  PasswordChangeRunController() = default;
};

#endif  // CHROME_BROWSER_UI_AUTOFILL_ASSISTANT_PASSWORD_CHANGE_PASSWORD_CHANGE_RUN_CONTROLLER_H_
