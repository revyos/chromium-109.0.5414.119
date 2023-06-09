// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_COMPONENTS_ATTESTATION_ATTESTATION_FLOW_UTILS_H_
#define CHROMEOS_ASH_COMPONENTS_ATTESTATION_ATTESTATION_FLOW_UTILS_H_

#include <string>

#include "base/component_export.h"
#include "chromeos/ash/components/dbus/constants/attestation_constants.h"

namespace ash {
namespace attestation {

// Returns the name of the key for a given certificate profile. The
// |request_origin| parameter is for PROFILE_CONTENT_PROTECTION_CERTIFICATE
// profiles and is ignored for other profiles.
//
// Parameters
//   certificate_profile - Specifies what kind of certificate the key is for.
//   request_origin - For content protection profiles, certificate requests
//                    are origin-specific.  This string must uniquely identify
//                    the origin of the request.
COMPONENT_EXPORT(CHROMEOS_ASH_COMPONENTS_ATTESTATION)
std::string GetKeyNameForProfile(
    AttestationCertificateProfile certificate_profile,
    const std::string& request_origin);

}  // namespace attestation
}  // namespace ash

#endif  // CHROMEOS_ASH_COMPONENTS_ATTESTATION_ATTESTATION_FLOW_UTILS_H_
