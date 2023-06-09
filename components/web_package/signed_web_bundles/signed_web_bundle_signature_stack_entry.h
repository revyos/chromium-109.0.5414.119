// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_WEB_PACKAGE_SIGNED_WEB_BUNDLES_SIGNED_WEB_BUNDLE_SIGNATURE_STACK_ENTRY_H_
#define COMPONENTS_WEB_PACKAGE_SIGNED_WEB_BUNDLES_SIGNED_WEB_BUNDLE_SIGNATURE_STACK_ENTRY_H_

#include "base/types/expected.h"
#include "components/web_package/mojom/web_bundle_parser.mojom-forward.h"
#include "components/web_package/signed_web_bundles/ed25519_public_key.h"
#include "components/web_package/signed_web_bundles/ed25519_signature.h"

namespace web_package {

// This class represents an entry on the signature stack of the integrity block
// of a Signed Web Bundle. See the documentation of
// `SignedWebBundleIntegrityBlock` for more details of how this class is used.
class SignedWebBundleSignatureStackEntry {
 public:
  // Attempt to convert the provided Mojo signature stack entry into an instance
  // of this class, returning a string describing the error on failure.
  static base::expected<SignedWebBundleSignatureStackEntry, std::string> Create(
      const mojom::BundleIntegrityBlockSignatureStackEntryPtr entry);

  SignedWebBundleSignatureStackEntry(const SignedWebBundleSignatureStackEntry&);

  ~SignedWebBundleSignatureStackEntry();

  const Ed25519PublicKey& public_key() const { return public_key_; }
  const Ed25519Signature& signature() const { return signature_; }

  const std::vector<uint8_t>& complete_entry_cbor() const {
    return complete_entry_cbor_;
  }
  const std::vector<uint8_t>& attributes_cbor() const {
    return attributes_cbor_;
  }

 private:
  SignedWebBundleSignatureStackEntry(
      const std::vector<uint8_t>& complete_entry_cbor,
      const std::vector<uint8_t>& attributes_cbor,
      const Ed25519PublicKey& public_key,
      const Ed25519Signature& signature);

  const std::vector<uint8_t> complete_entry_cbor_;
  const std::vector<uint8_t> attributes_cbor_;

  const Ed25519PublicKey public_key_;
  const Ed25519Signature signature_;
};

}  // namespace web_package

#endif  // COMPONENTS_WEB_PACKAGE_SIGNED_WEB_BUNDLES_SIGNED_WEB_BUNDLE_SIGNATURE_STACK_ENTRY_H_
