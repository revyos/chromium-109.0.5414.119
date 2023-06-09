// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEBID_FEDERATED_IDENTITY_ACTIVE_SESSION_PERMISSION_CONTEXT_H_
#define CHROME_BROWSER_WEBID_FEDERATED_IDENTITY_ACTIVE_SESSION_PERMISSION_CONTEXT_H_

#include <string>

#include "chrome/browser/webid/federated_identity_account_keyed_permission_context.h"
#include "content/public/browser/federated_identity_active_session_permission_context_delegate.h"

namespace content {
class BrowserContext;
}

// Context for storing permission grants that are associated with having an
// federated sign-in session between a Relying Party and and Identity
// Provider.
class FederatedIdentityActiveSessionPermissionContext
    : public content::FederatedIdentityActiveSessionPermissionContextDelegate,
      public FederatedIdentityAccountKeyedPermissionContext {
 public:
  explicit FederatedIdentityActiveSessionPermissionContext(
      content::BrowserContext* browser_context);

  ~FederatedIdentityActiveSessionPermissionContext() override;

  FederatedIdentityActiveSessionPermissionContext(
      const FederatedIdentityActiveSessionPermissionContext&) = delete;
  FederatedIdentityActiveSessionPermissionContext& operator=(
      const FederatedIdentityActiveSessionPermissionContext&) = delete;

  // content::FederatedIdentityActiveSessionPermissionContextDelegate:
  bool HasActiveSession(const url::Origin& relying_party_requester,
                        const url::Origin& identity_provider,
                        const std::string& account_identifier) override;
  void GrantActiveSession(const url::Origin& relying_party_requester,
                          const url::Origin& identity_provider,
                          const std::string& account_identifier) override;
  void RevokeActiveSession(const url::Origin& relying_party_requester,
                           const url::Origin& identity_provider,
                           const std::string& account_identifier) override;
};

#endif  // CHROME_BROWSER_WEBID_FEDERATED_IDENTITY_ACTIVE_SESSION_PERMISSION_CONTEXT_H_
