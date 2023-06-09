// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_INTENT_HELPER_MAC_INTENT_PICKER_HELPERS_H_
#define CHROME_BROWSER_APPS_INTENT_HELPER_MAC_INTENT_PICKER_HELPERS_H_

#include <string>

#include "chrome/browser/apps/intent_helper/apps_navigation_types.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "url/gurl.h"

namespace apps {

// Returns a native Mac app, if any, registered to own the given `url`.
absl::optional<IntentPickerAppInfo> FindMacAppForUrl(const GURL& url);

// Launches a native Mac app, specified by the `launch_name` (the path) returned
// by `FindMacAppForUrl` above, for the given `url`.
void LaunchMacApp(const GURL& url, const std::string& launch_name);

}  // namespace apps

#endif  // CHROME_BROWSER_APPS_INTENT_HELPER_MAC_INTENT_PICKER_HELPERS_H_
