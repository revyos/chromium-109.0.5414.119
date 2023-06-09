// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_CHILD_ACCOUNTS_CHILD_ACCOUNT_TEST_UTILS_H_
#define CHROME_BROWSER_ASH_CHILD_ACCOUNTS_CHILD_ACCOUNT_TEST_UTILS_H_

#include <string>

namespace ash {
namespace test {

// Returns a base64-encoded dummy token for child log-in.
std::string GetChildAccountOAuthIdToken();

}  // namespace test
}  // namespace ash

// TODO(https://crbug.com/1164001): remove when ChromOS code migration is done.
namespace chromeos {
namespace test {
using ::ash::test::GetChildAccountOAuthIdToken;
}  // namespace test
}  // namespace chromeos

#endif  // CHROME_BROWSER_ASH_CHILD_ACCOUNTS_CHILD_ACCOUNT_TEST_UTILS_H_
