// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_PRINTING_PRINT_MEDIA_L10N_H_
#define CHROME_COMMON_PRINTING_PRINT_MEDIA_L10N_H_

#include <string>

namespace printing {

// Maps a paper vendor ID to a localized name; returns the localized
// name if any is found, else returns an empty string.
std::string LocalizePaperDisplayName(const std::string& vendor_id);

}  // namespace printing

#endif  // CHROME_COMMON_PRINTING_PRINT_MEDIA_L10N_H_
