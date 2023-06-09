// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_PUBLIC_PASSWORD_CHANGE_EMPTY_WEBSITE_LOGIN_MANAGER_IMPL_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_PUBLIC_PASSWORD_CHANGE_EMPTY_WEBSITE_LOGIN_MANAGER_IMPL_H_

#include "base/callback.h"
#include "base/time/time.h"
#include "components/autofill_assistant/browser/public/password_change/website_login_manager.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace autofill_assistant {

// Native implementation of the autofill assistant website login fetcher, which
// does nothing.
class EmptyWebsiteLoginManagerImpl : public WebsiteLoginManager {
 public:
  EmptyWebsiteLoginManagerImpl();

  EmptyWebsiteLoginManagerImpl(const EmptyWebsiteLoginManagerImpl&) = delete;
  EmptyWebsiteLoginManagerImpl& operator=(const EmptyWebsiteLoginManagerImpl&) =
      delete;

  ~EmptyWebsiteLoginManagerImpl() override;

  // From WebsiteLoginManager:
  void GetLoginsForUrl(
      const GURL& url,
      base::OnceCallback<void(std::vector<Login>)> callback) override;
  void GetPasswordForLogin(
      const Login& login,
      base::OnceCallback<void(bool, std::string)> callback) override;
  void GetGetLastTimePasswordUsed(
      const Login& login,
      base::OnceCallback<void(absl::optional<base::Time>)> callback) override;
  absl::optional<std::string> GeneratePassword(
      content::RenderFrameHost* rfh,
      autofill::FormSignature form_signature,
      autofill::FieldSignature field_signature,
      uint64_t max_length) override;
  const std::string& GetGeneratedPassword() override;
  void PresaveGeneratedPassword(const Login& login,
                                const std::string& password,
                                const autofill::FormData& form_data,
                                base::OnceCallback<void()> callback) override;
  bool ReadyToSaveGeneratedPassword() override;
  void SaveGeneratedPassword() override;
  void ResetPendingCredentials() override;
  bool ReadyToSaveSubmittedPassword() override;
  bool SubmittedPasswordIsSame() override;
  void CheckWhetherSubmittedCredentialIsLeaked(
      SavePasswordLeakDetectionDelegate::Callback callback,
      base::TimeDelta timeout) override;
  bool SaveSubmittedPassword() override;

 private:
  std::string generated_password_;
};

}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_PUBLIC_PASSWORD_CHANGE_WEBSITE_LOGIN_MANAGER_IMPL_H_
