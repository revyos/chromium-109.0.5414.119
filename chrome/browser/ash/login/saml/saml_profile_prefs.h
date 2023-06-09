// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_LOGIN_SAML_SAML_PROFILE_PREFS_H_
#define CHROME_BROWSER_ASH_LOGIN_SAML_SAML_PROFILE_PREFS_H_

class PrefRegistrySimple;

namespace ash {

// Registers all Saml-related profile prefs.
void RegisterSamlProfilePrefs(PrefRegistrySimple* registry);

}  // namespace ash

// TODO(https://crbug.com/1164001): remove after the //chrome/browser/chromeos
// source migration is finished.
namespace chromeos {
using ::ash::RegisterSamlProfilePrefs;
}

#endif  // CHROME_BROWSER_ASH_LOGIN_SAML_SAML_PROFILE_PREFS_H_
