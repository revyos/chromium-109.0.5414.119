// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/public/password_change/website_login_manager.h"

namespace autofill_assistant {

WebsiteLoginManager::Login::Login(const GURL& _origin,
                                  const std::string& _username)
    : origin(_origin), username(_username) {}

WebsiteLoginManager::Login::Login(const Login& other) = default;
WebsiteLoginManager::Login::~Login() = default;

}  // namespace autofill_assistant
