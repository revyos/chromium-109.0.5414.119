// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/reporting/encryption/encryption_module_interface.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/feature_list.h"
#include "base/strings/string_piece.h"
#include "base/time/time.h"
#include "components/reporting/proto/synced/record.pb.h"
#include "components/reporting/util/status.h"
#include "components/reporting/util/statusor.h"

namespace reporting {

// Temporary: enable/disable encryption.
BASE_FEATURE(kEncryptedReportingFeature,
             "EncryptedReporting",
             base::FEATURE_ENABLED_BY_DEFAULT);

// static
bool EncryptionModuleInterface::is_enabled() {
  return base::FeatureList::IsEnabled(kEncryptedReportingFeature);
}

EncryptionModuleInterface::EncryptionModuleInterface(
    base::TimeDelta renew_encryption_key_period,
    const base::TickClock* clock)
    : renew_encryption_key_period_(renew_encryption_key_period),
      clock_(clock) {}

EncryptionModuleInterface::~EncryptionModuleInterface() = default;

void EncryptionModuleInterface::EncryptRecord(
    base::StringPiece record,
    base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb) const {
  if (!is_enabled()) {
    // Encryptor disabled.
    EncryptedRecord encrypted_record;
    encrypted_record.mutable_encrypted_wrapped_record()->assign(record.begin(),
                                                                record.end());
    // encryption_info is not set.
    std::move(cb).Run(std::move(encrypted_record));
    return;
  }

  // Encryptor enabled: start encryption of the record as a whole.
  if (!has_encryption_key()) {
    // Encryption key is not available.
    std::move(cb).Run(
        Status(error::NOT_FOUND, "Cannot encrypt record - no key"));
    return;
  }
  // Encryption key is available, encrypt.
  EncryptRecordImpl(record, std::move(cb));
}

void EncryptionModuleInterface::UpdateAsymmetricKey(
    base::StringPiece new_public_key,
    PublicKeyId new_public_key_id,
    base::OnceCallback<void(Status)> response_cb) {
  UpdateAsymmetricKeyImpl(
      new_public_key, new_public_key_id,
      base::BindOnce(
          [](EncryptionModuleInterface* encryption_module_interface,
             base::OnceCallback<void(Status)> response_cb, Status status) {
            if (status.ok()) {
              encryption_module_interface->last_encryption_key_update_.store(
                  encryption_module_interface->clock_->NowTicks());
            }
            std::move(response_cb).Run(status);
          },
          base::Unretained(this), std::move(response_cb)));
}

bool EncryptionModuleInterface::has_encryption_key() const {
  return !last_encryption_key_update_.load().is_null();
}

bool EncryptionModuleInterface::need_encryption_key() const {
  return !has_encryption_key() ||
         last_encryption_key_update_.load() + renew_encryption_key_period_ <
             clock_->NowTicks();
}

}  // namespace reporting
