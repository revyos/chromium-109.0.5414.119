// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ash/in_session_password_change/lock_screen_start_reauth_ui.h"

#include <memory>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/ui/webui/ash/in_session_password_change/lock_screen_reauth_handler.h"
#include "chrome/browser/ui/webui/metrics_handler.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/gaia_auth_host_resources_map.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/oobe_unconditional_resources_map.h"
#include "chrome/grit/password_change_resources.h"
#include "chrome/grit/password_change_resources_map.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/chromeos/devicetype_utils.h"

namespace ash {

bool LockScreenStartReauthUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return ash::ProfileHelper::IsLockScreenProfile(
      Profile::FromBrowserContext(browser_context));
}

LockScreenStartReauthUI::LockScreenStartReauthUI(content::WebUI* web_ui)
    : ui::WebDialogUI(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  const user_manager::User* user =
      user_manager::UserManager::Get()->GetActiveUser();
  std::string email;
  if (user) {
    email = user->GetDisplayEmail();
  }

  content::WebUIDataSource* source = content::WebUIDataSource::Create(
      chrome::kChromeUILockScreenStartReauthHost);

  auto main_handler = std::make_unique<LockScreenReauthHandler>(email);
  main_handler_ = main_handler.get();
  web_ui->AddMessageHandler(std::move(main_handler));
  web_ui->AddMessageHandler(std::make_unique<MetricsHandler>());

  // TODO(crbug.com/1098690): Trusted Type Polymer
  source->DisableTrustedTypesCSP();

  source->EnableReplaceI18nInJS();
  source->UseStringsJs();

  source->AddString("lockScreenReauthSubtitile",
                    l10n_util::GetStringFUTF16(IDS_LOCK_SCREEN_REAUTH_SUBTITLE,
                                               base::UTF8ToUTF16(email)));
  source->AddString(
      "lockScreenReauthSubtitile1WithError",
      l10n_util::GetStringUTF16(IDS_LOCK_SCREEN_WRONG_USER_SUBTITLE1));
  source->AddString("lockScreenReauthSubtitile2WithError",
                    l10n_util::GetStringFUTF16(IDS_LOCK_SCREEN_WRONG_USER_SUBTITLE2,
                                               base::UTF8ToUTF16(email)));

  source->AddString("lockScreenVerifyButton",
                    l10n_util::GetStringUTF16(IDS_LOCK_SCREEN_VERIFY_BUTTON));
  source->AddString(
      "lockScreenVerifyAgainButton",
      l10n_util::GetStringUTF16(IDS_LOCK_SCREEN_VERIFY_AGAIN_BUTTON));
  source->AddString("lockScreenCancelButton",
                    l10n_util::GetStringUTF16(IDS_LOCK_SCREEN_CANCEL_BUTTON));
  source->AddString("lockScreenCloseButton",
                    l10n_util::GetStringUTF16(IDS_LOCK_SCREEN_CLOSE_BUTTON));
  source->AddString(
      "lockScreenNextButton",
      l10n_util::GetStringUTF16(IDS_LOGIN_SAML_INTERSTITIAL_NEXT_BUTTON_TEXT));
  source->AddString(
      "confirmPasswordLabel",
      l10n_util::GetStringUTF16(IDS_LOGIN_CONFIRM_PASSWORD_LABEL));
  source->AddString(
      "manualPasswordInputLabel",
      l10n_util::GetStringUTF16(IDS_LOGIN_MANUAL_PASSWORD_INPUT_LABEL));
  source->AddString("passwordChangedIncorrectOldPassword",
                    l10n_util::GetStringUTF16(
                        IDS_LOGIN_PASSWORD_CHANGED_INCORRECT_OLD_PASSWORD));
  source->AddString(
      "manualPasswordMismatch",
      l10n_util::GetStringUTF16(IDS_LOGIN_MANUAL_PASSWORD_MISMATCH));
  source->AddString("loginWelcomeMessage",
                    l10n_util::GetStringUTF16(IDS_LOCK_SCREEN_VERIFY_ACCOUNT));
  source->AddString(
      "loginWelcomeMessageWithError",
      l10n_util::GetStringUTF16(IDS_LOCK_SCREEN_VERIFICATION_FAILED));
  source->AddString(
      "manualPasswordSubtitle",
      l10n_util::GetStringUTF16(IDS_LOCK_MANUAL_PASSWORD_SUBTITLE));
  source->AddString("confirmPasswordSubtitle",
                    l10n_util::GetStringFUTF16(IDS_LOGIN_CONFIRM_PASSWORD_TITLE,
                                               ui::GetChromeOSDeviceName()));
  source->AddString("samlNotice",
                    l10n_util::GetStringUTF16(IDS_LOCK_SAML_NOTICE));
  source->AddString("passwordChangedTitle",
                    l10n_util::GetStringUTF16(IDS_LOCK_PASSWORD_CHANGED_TITLE));
  source->AddString(
      "passwordChangedSubtitle",
      l10n_util::GetStringFUTF16(IDS_LOCK_PASSWORD_CHANGED_SUBTITLE,
                                 ui::GetChromeOSDeviceName()));
  source->AddString(
      "passwordChangedOldPasswordHint",
      l10n_util::GetStringUTF16(IDS_LOCK_PASSWORD_CHANGED_OLD_PASSWORD_HINT));

  source->AddResourcePaths(
      base::make_span(kPasswordChangeResources, kPasswordChangeResourcesSize));
  source->SetDefaultResource(IDR_PASSWORD_CHANGE_LOCK_SCREEN_REAUTH_APP_HTML);

  // Add Gaia Authenticator resources
  source->AddResourcePaths(
      base::make_span(kGaiaAuthHostResources, kGaiaAuthHostResourcesSize));

  // Add OOBE resources
  source->AddResourcePaths(base::make_span(kOobeUnconditionalResources,
                                           kOobeUnconditionalResourcesSize));

  content::WebUIDataSource::Add(profile, source);
}

LockScreenStartReauthUI::~LockScreenStartReauthUI() = default;

}  // namespace ash
