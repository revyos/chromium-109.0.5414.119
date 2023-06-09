// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/display_features.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"

namespace display {
namespace features {

#if BUILDFLAG(IS_CHROMEOS_ASH)

// Enables the rounded corners for the internal display.
BASE_FEATURE(kRoundedDisplay,
             "RoundedDisplay",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsRoundedDisplayEnabled() {
  return base::FeatureList::IsEnabled(kRoundedDisplay);
}

// Enables using HDR transfer function if the monitor says it supports it.
BASE_FEATURE(kUseHDRTransferFunction,
             "UseHDRTransferFunction",
// TODO(b/168843009): Temporarily disable on ARM while investigating.
#if defined(ARCH_CPU_ARM_FAMILY)
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             base::FEATURE_ENABLED_BY_DEFAULT
#endif
);

#endif

// This features allows listing all display modes of external displays in the
// display settings and setting any one of them exactly as requested, which can
// be very useful for debugging and development purposes.
BASE_FEATURE(kListAllDisplayModes,
             "ListAllDisplayModes",
             base::FEATURE_ENABLED_BY_DEFAULT);

bool IsListAllDisplayModesEnabled() {
  return base::FeatureList::IsEnabled(kListAllDisplayModes);
}

// A temporary flag to control hardware mirroring until it is decided whether to
// permanently remove hardware mirroring support. See crbug.com/1161556 for
// details.
BASE_FEATURE(kEnableHardwareMirrorMode,
             "EnableHardwareMirrorMode",
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsHardwareMirrorModeEnabled() {
  return base::FeatureList::IsEnabled(kEnableHardwareMirrorMode);
}

// A temporary flag to require Content Protection to use provisioned key as the
// kernel doesn't expose that it requires this yet.(b/112172923)
BASE_FEATURE(kRequireHdcpKeyProvisioning,
             "RequireHdcpKeyProvisioning",
             base::FEATURE_DISABLED_BY_DEFAULT);
bool IsHdcpKeyProvisioningRequired() {
  return base::FeatureList::IsEnabled(kRequireHdcpKeyProvisioning);
}

}  // namespace features
}  // namespace display
