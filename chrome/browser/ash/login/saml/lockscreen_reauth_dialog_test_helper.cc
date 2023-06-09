// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/saml/lockscreen_reauth_dialog_test_helper.h"

#include "base/run_loop.h"
#include "chrome/browser/ash/login/login_pref_names.h"
#include "chrome/browser/ash/login/saml/in_session_password_sync_manager.h"
#include "chrome/browser/ash/login/saml/in_session_password_sync_manager_factory.h"
#include "chrome/browser/ash/login/test/js_checker.h"
#include "chrome/browser/ash/login/test/test_condition_waiter.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/webui/ash/in_session_password_change/lock_screen_captive_portal_dialog.h"
#include "chrome/browser/ui/webui/ash/in_session_password_change/lock_screen_network_dialog.h"
#include "chrome/browser/ui/webui/ash/in_session_password_change/lock_screen_network_ui.h"
#include "chrome/browser/ui/webui/ash/in_session_password_change/lock_screen_reauth_dialogs.h"
#include "chrome/browser/ui/webui/ash/in_session_password_change/lock_screen_start_reauth_ui.h"
#include "chrome/browser/ui/webui/signin/signin_utils.h"
#include "components/session_manager/core/session_manager.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {

namespace {
// Main dialog
const test::UIPath kSamlContainer = {"main-element", "body"};
const test::UIPath kMainVerifyButton = {"main-element",
                                        "nextButtonVerifyScreen"};
const test::UIPath kMainCancelButton = {"main-element",
                                        "cancelButtonVerifyScreen"};
const test::UIPath kErrorCancelButton = {"main-element",
                                         "cancelButtonErrorScreen"};
const test::UIPath kSamlCancelButton = {"main-element", "saml-close-button"};
const test::UIPath kMainScreen = {"main-element", "verifyAccountScreen"};
const test::UIPath kErrorScreen = {"main-element", "errorScreen"};
const test::UIPath kSamlConfirmPasswordScreen = {"main-element",
                                                 "samlConfirmPasswordScreen"};
const test::UIPath kPasswordInput = {"main-element", "passwordInput"};
const test::UIPath kPasswordConfirmInput = {"main-element",
                                            "confirmPasswordInput"};
const test::UIPath kPasswordSubmit = {"main-element",
                                      "nextButtonSamlConfirmPassword"};
const char kSigninFrame[] = "signin-frame";

// Network dialog
const test::UIPath kNetworkDialog = {"network-ui", "dialog"};
const test::UIPath kNetworkCancelButton = {"network-ui", "cancelButton"};
}  // namespace

LockScreenReauthDialogTestHelper::LockScreenReauthDialogTestHelper() = default;
LockScreenReauthDialogTestHelper::~LockScreenReauthDialogTestHelper() = default;

LockScreenReauthDialogTestHelper::LockScreenReauthDialogTestHelper(
    LockScreenReauthDialogTestHelper&& other) = default;

LockScreenReauthDialogTestHelper& LockScreenReauthDialogTestHelper::operator=(
    LockScreenReauthDialogTestHelper&& other) = default;

// static
absl::optional<LockScreenReauthDialogTestHelper>
LockScreenReauthDialogTestHelper::ShowDialogAndWait() {
  LockScreenReauthDialogTestHelper dialog_test_helper;
  if (!dialog_test_helper.ShowDialogAndWaitImpl())
    return absl::nullopt;
  return dialog_test_helper;
}

// static
absl::optional<LockScreenReauthDialogTestHelper>
LockScreenReauthDialogTestHelper::StartSamlAndWaitForIdpPageLoad() {
  absl::optional<LockScreenReauthDialogTestHelper> reauth_dialog_helper =
      LockScreenReauthDialogTestHelper::ShowDialogAndWait();
  if (!reauth_dialog_helper.has_value()) {
    return absl::nullopt;
  }

  reauth_dialog_helper->ForceSamlRedirect();

  // Expect the 'Verify Account' screen (the first screen the dialog shows) to
  // be visible and proceed to the SAML page.
  reauth_dialog_helper->WaitForVerifyAccountScreen();
  reauth_dialog_helper->ClickVerifyButton();

  reauth_dialog_helper->WaitForSamlScreen();
  reauth_dialog_helper->ExpectVerifyAccountScreenHidden();

  reauth_dialog_helper->WaitForIdpPageLoad();
  return reauth_dialog_helper;
}

bool LockScreenReauthDialogTestHelper::ShowDialogAndWaitImpl() {
  // Check precondition: Screen is locked.
  if (!session_manager::SessionManager::Get()->IsScreenLocked()) {
    ADD_FAILURE() << "Screen must be locked";
    return false;
  }

  // The screen can only be locked if there is an active user session, so
  // ProfileManager::GetActiveUserProfile() must return a non-null Profile.
  Profile* profile = ProfileManager::GetActiveUserProfile();
  CHECK(profile);
  password_sync_manager_ =
      InSessionPasswordSyncManagerFactory::GetForProfile(profile);
  if (!password_sync_manager_) {
    ADD_FAILURE() << "Could not retrieve InSessionPasswordSyncManager";
    return false;
  }
  ProfileManager::GetActiveUserProfile()->GetPrefs()->SetBoolean(
      prefs::kLockScreenReauthenticationEnabled, true);
  password_sync_manager_->CreateAndShowDialog();

  WaitForReauthDialogToLoad();

  // Fetch the dialog, WebUi controller and main message handler.
  reauth_dialog_ = password_sync_manager_->get_reauth_dialog_for_testing();
  if (!reauth_dialog_ || !reauth_dialog_->GetWebUIForTest()) {
    ADD_FAILURE() << "Could not retrieve LockScreenStartReauthDialog";
    return false;
  }
  reauth_webui_controller_ = static_cast<LockScreenStartReauthUI*>(
      reauth_dialog_->GetWebUIForTest()->GetController());
  if (!reauth_webui_controller_) {
    ADD_FAILURE() << "Could not retrieve LockScreenStartReauthUI";
    return false;
  }
  main_handler_ = reauth_webui_controller_->GetMainHandler();
  if (!main_handler_) {
    ADD_FAILURE() << "Could not retrieve LockScreenReauthHandler";
    return false;
  }
  return true;
}

void LockScreenReauthDialogTestHelper::ForceSamlRedirect() {
  main_handler_->force_saml_redirect_for_testing();
}

void LockScreenReauthDialogTestHelper::WaitForVerifyAccountScreen() {
  test::JSChecker js_checker = DialogJS();
  js_checker.CreateVisibilityWaiter(true, kMainScreen)->Wait();
  js_checker.ExpectVisiblePath(kMainScreen);
}

void LockScreenReauthDialogTestHelper::ClickVerifyButton() {
  ExpectVerifyAccountScreenVisible();

  // Expect the main screen to be visible and proceed to the SAML page.
  DialogJS().TapOnPath(kMainVerifyButton);
}

void LockScreenReauthDialogTestHelper::ClickCancelButtonOnVerifyScreen() {
  ExpectVerifyAccountScreenVisible();
  DialogJS().TapOnPath(kMainCancelButton);
}

void LockScreenReauthDialogTestHelper::ClickCancelButtonOnErrorScreen() {
  ExpectErrorScreenVisible();
  DialogJS().TapOnPath(kErrorCancelButton);
}

void LockScreenReauthDialogTestHelper::ClickCancelButtonOnSamlScreen() {
  ExpectSamlScreenVisible();
  DialogJS().TapOnPath(kSamlCancelButton);
}

void LockScreenReauthDialogTestHelper::WaitForSamlScreen() {
  WaitForAuthenticatorToLoad();
  DialogJS().CreateVisibilityWaiter(true, kSamlContainer)->Wait();
  DialogJS().ExpectVisiblePath(kSamlContainer);
}

void LockScreenReauthDialogTestHelper::ExpectVerifyAccountScreenVisible() {
  DialogJS().ExpectVisiblePath(kMainScreen);
}

void LockScreenReauthDialogTestHelper::ExpectVerifyAccountScreenHidden() {
  DialogJS().ExpectHiddenPath(kMainScreen);
}

void LockScreenReauthDialogTestHelper::ExpectErrorScreenVisible() {
  DialogJS().CreateVisibilityWaiter(true, kErrorScreen)->Wait();
  DialogJS().ExpectVisiblePath(kErrorScreen);
}

void LockScreenReauthDialogTestHelper::ExpectSamlScreenVisible() {
  DialogJS().ExpectVisiblePath(kSamlContainer);
}

void LockScreenReauthDialogTestHelper::ExpectSamlScreenHidden() {
  DialogJS().ExpectHiddenPath(kSamlContainer);
}

void LockScreenReauthDialogTestHelper::ExpectSamlConfirmPasswordVisible() {
  DialogJS().CreateVisibilityWaiter(true, kSamlConfirmPasswordScreen)->Wait();
  DialogJS().ExpectVisiblePath(kSamlConfirmPasswordScreen);
}

void LockScreenReauthDialogTestHelper::ExpectPasswordConfirmInputHidden() {
  DialogJS().CreateVisibilityWaiter(false, kPasswordConfirmInput)->Wait();
  DialogJS().ExpectHiddenPath(kPasswordConfirmInput);
}

void LockScreenReauthDialogTestHelper::ExpectPasswordConfirmInputVisible() {
  DialogJS().CreateVisibilityWaiter(true, kPasswordConfirmInput)->Wait();
  DialogJS().ExpectVisiblePath(kPasswordConfirmInput);
}

void LockScreenReauthDialogTestHelper::SendConfirmPassword(
    const std::string& password_to_confirm) {
  DialogJS().TypeIntoPath(password_to_confirm, kPasswordInput);
  DialogJS().TapOnPath(kPasswordSubmit);
}

void LockScreenReauthDialogTestHelper::SetManualPasswords(
    const std::string& password,
    const std::string& confirm_password) {
  DialogJS().TypeIntoPath(password, kPasswordInput);
  DialogJS().TypeIntoPath(confirm_password, kPasswordConfirmInput);
  DialogJS().TapOnPath(kPasswordSubmit);
}

void LockScreenReauthDialogTestHelper::WaitForIdpPageLoad() {
  content::DOMMessageQueue message_queue(DialogWebContents());
  content::ExecuteScriptAsync(
      DialogWebContents(),
      R"($('main-element').authenticator_.addEventListener('authFlowChange',
            function f() {
              $('main-element').authenticator_.removeEventListener(
                  'authFlowChange', f);
              window.domAutomationController.send('Loaded');
            });)");
  std::string message;
  do {
    ASSERT_TRUE(message_queue.WaitForMessage(&message));
  } while (message != "\"Loaded\"");
}

content::WebContents* LockScreenReauthDialogTestHelper::DialogWebContents() {
  CHECK(reauth_dialog_);
  return reauth_dialog_->GetWebUIForTest()->GetWebContents();
}

test::JSChecker LockScreenReauthDialogTestHelper::DialogJS() {
  return test::JSChecker(DialogWebContents());
}

test::JSChecker LockScreenReauthDialogTestHelper::NetworkJS() {
  CHECK(network_dialog_);
  return test::JSChecker(network_dialog_->GetWebUIForTest()->GetWebContents());
}

test::JSChecker LockScreenReauthDialogTestHelper::SigninFrameJS() {
  content::RenderFrameHost* frame =
      signin::GetAuthFrame(DialogWebContents(), kSigninFrame);
  CHECK(frame);
  CHECK(frame->IsDOMContentLoaded());
  return test::JSChecker(frame);
}

void LockScreenReauthDialogTestHelper::WaitForAuthenticatorToLoad() {
  base::RunLoop run_loop;
  if (!main_handler_->IsAuthenticatorLoaded(run_loop.QuitClosure())) {
    run_loop.Run();
  }
}

void LockScreenReauthDialogTestHelper::WaitForReauthDialogToClose() {
  base::RunLoop run_loop;
  if (!password_sync_manager_->IsReauthDialogClosedForTesting(
          run_loop.QuitClosure())) {
    run_loop.Run();
  }
}

void LockScreenReauthDialogTestHelper::WaitForReauthDialogToLoad() {
  base::RunLoop run_loop;
  if (!password_sync_manager_->IsReauthDialogLoadedForTesting(
          run_loop.QuitClosure())) {
    run_loop.Run();
  }
}

void LockScreenReauthDialogTestHelper::WaitForNetworkDialogToLoad() {
  CHECK(reauth_dialog_);
  base::RunLoop run_loop;
  if (!reauth_dialog_->IsNetworkDialogLoadedForTesting(
          run_loop.QuitClosure())) {
    run_loop.Run();
  }
}

void LockScreenReauthDialogTestHelper::WaitForCaptivePortalDialogToLoad() {
  base::RunLoop run_loop;
  if (!reauth_dialog_->IsCaptivePortalDialogLoadedForTesting(
          run_loop.QuitClosure())) {
    run_loop.Run();
  }

  captive_portal_dialog_ =
      reauth_dialog_->get_captive_portal_dialog_for_testing();
}

void LockScreenReauthDialogTestHelper::WaitForCaptivePortalDialogToShow() {
  base::RunLoop run_loop;
  if (!captive_portal_dialog_->IsDialogShownForTesting(
          run_loop.QuitClosure())) {
    run_loop.Run();
  }
}

void LockScreenReauthDialogTestHelper::WaitForCaptivePortalDialogToClose() {
  base::RunLoop run_loop;
  if (!captive_portal_dialog_->IsDialogClosedForTesting(
          run_loop.QuitClosure())) {
    run_loop.Run();
  }
}

void LockScreenReauthDialogTestHelper::WaitForNetworkDialogAndSetHandlers() {
  WaitForNetworkDialogToLoad();

  network_dialog_ = reauth_dialog_->get_network_dialog_for_testing();
  if (!network_dialog_ || !network_dialog_->GetWebUIForTest()) {
    ADD_FAILURE() << "Could not retrieve LockScreenNetworkDialog";
  }
  network_webui_controller_ = static_cast<LockScreenNetworkUI*>(
      network_dialog_->GetWebUIForTest()->GetController());
  if (!network_webui_controller_) {
    ADD_FAILURE() << "Could not retrieve LockScreenNetworkUI";
  }
  network_handler_ = network_webui_controller_->GetMainHandlerForTests();
  if (!network_handler_) {
    ADD_FAILURE() << "Could not retrieve LockScreenNetworkHandler";
  }
}

// Makes the main dialog show its inner 'network' dialog and fetches
// pointers to the Dialog, WebUI Controller and Message Handler.
void LockScreenReauthDialogTestHelper::ShowNetworkScreenAndWait() {
  reauth_dialog_->ShowLockScreenNetworkDialog();
  WaitForNetworkDialogAndSetHandlers();
}

void LockScreenReauthDialogTestHelper::CloseNetworkScreen() {
  reauth_dialog_->DismissLockScreenNetworkDialog();
}

void LockScreenReauthDialogTestHelper::ExpectNetworkDialogVisible() {
  NetworkJS().CreateVisibilityWaiter(true, kNetworkDialog)->Wait();
  NetworkJS().ExpectVisiblePath(kNetworkDialog);
}

void LockScreenReauthDialogTestHelper::ExpectNetworkDialogHidden() {
  EXPECT_FALSE(reauth_dialog_->is_network_dialog_visible_for_testing());
}

void LockScreenReauthDialogTestHelper::ClickCloseNetworkButton() {
  NetworkJS().TapOnPath(kNetworkCancelButton);
}

void LockScreenReauthDialogTestHelper::ExpectCaptivePortalDialogVisible() {
  EXPECT_TRUE(captive_portal_dialog_->IsRunning());
}

void LockScreenReauthDialogTestHelper::ExpectCaptivePortalDialogHidden() {
  EXPECT_FALSE(captive_portal_dialog_->IsRunning());
}

void LockScreenReauthDialogTestHelper::CloseCaptivePortalDialogAndWait() {
  captive_portal_dialog_->Close();
  WaitForCaptivePortalDialogToClose();
}

}  // namespace ash
