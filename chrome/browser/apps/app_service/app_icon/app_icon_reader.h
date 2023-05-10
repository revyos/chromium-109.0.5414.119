// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_APP_ICON_APP_ICON_READER_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_APP_ICON_APP_ICON_READER_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/apps/app_service/app_icon/app_icon_util.h"
#include "components/services/app_service/public/cpp/icon_types.h"

class Profile;

namespace apps {

// AppIconReader reads app icons from the icon image files in the local
// disk and provides an ImageSkia for UI code to use.
//
// TODO(crbug.com/1380608): Implement the icon reading function.
class AppIconReader {
 public:
  explicit AppIconReader(Profile* profile);
  AppIconReader(const AppIconReader&) = delete;
  AppIconReader& operator=(const AppIconReader&) = delete;
  ~AppIconReader();

  // Reads specified app icons from the local disk for an app identified by
  // `app_id`.
  void ReadIcons(const std::string& app_id,
                 int32_t size_hint_in_dip,
                 IconEffects icon_effects,
                 IconType icon_type,
                 LoadIconCallback callback);

 private:
  void OnIconRead(IconType icon_type,
                  LoadIconCallback callback,
                  std::vector<uint8_t> icon_data);

  const raw_ptr<Profile> profile_;

  base::WeakPtrFactory<AppIconReader> weak_ptr_factory_{this};
};

}  // namespace apps

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_APP_ICON_APP_ICON_READER_H_