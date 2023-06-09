// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_ASH_OS_SETTINGS_FEATURES_UTIL_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_ASH_OS_SETTINGS_FEATURES_UTIL_H_

class Profile;

namespace ash::settings::features {

bool IsGuestModeActive();

// Determines whether the Parental Controls section of People settings should be
// shown for |profile|.
bool ShouldShowParentalControlSettings(const Profile* profile);

// Determines whether the External Storage section of Device settings should be
// shown for |profile|.
bool ShouldShowExternalStorageSettings(const Profile* profile);

}  // namespace ash::settings::features

// TODO(https://crbug.com/1164001): remove when the migration is finished.
namespace chromeos::settings::features {
using ::ash::settings::features::IsGuestModeActive;
using ::ash::settings::features::ShouldShowExternalStorageSettings;
using ::ash::settings::features::ShouldShowParentalControlSettings;
}  // namespace chromeos::settings::features

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_ASH_OS_SETTINGS_FEATURES_UTIL_H_
