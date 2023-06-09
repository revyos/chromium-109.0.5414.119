// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MIRRORING_SERVICE_MIRRORING_FEATURES_H_
#define COMPONENTS_MIRRORING_SERVICE_MIRRORING_FEATURES_H_

#include "base/component_export.h"
#include "base/feature_list.h"
#include "build/build_config.h"

namespace mirroring {
namespace features {

COMPONENT_EXPORT(MIRRORING_SERVICE) BASE_DECLARE_FEATURE(kCastStreamingAv1);

COMPONENT_EXPORT(MIRRORING_SERVICE) BASE_DECLARE_FEATURE(kCastStreamingVp9);

// TODO(crbug.com/1363512): Remove support for sender side letterboxing.
COMPONENT_EXPORT(MIRRORING_SERVICE)
BASE_DECLARE_FEATURE(kCastDisableLetterboxing);

// TODO(crbug.com/1198616): Remove model name checks for querying receiver
// capabilities.
COMPONENT_EXPORT(MIRRORING_SERVICE)
BASE_DECLARE_FEATURE(kCastDisableModelNameCheck);

bool IsCastStreamingAV1Enabled();

}  // namespace features
}  // namespace mirroring

#endif  // COMPONENTS_MIRRORING_SERVICE_MIRRORING_FEATURES_H_
