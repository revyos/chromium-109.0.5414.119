// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_STORAGE_MANAGED_VALUE_STORE_CACHE_H_
#define CHROME_BROWSER_EXTENSIONS_API_STORAGE_MANAGED_VALUE_STORE_CACHE_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "components/policy/core/common/policy_namespace.h"
#include "components/policy/core/common/policy_service.h"
#include "extensions/browser/api/storage/settings_observer.h"
#include "extensions/browser/api/storage/value_store_cache.h"

class Profile;

namespace content {
class BrowserContext;
}

namespace policy {
class PolicyMap;
}

namespace value_store {
class ValueStoreFactory;
}

namespace extensions {

class PolicyValueStore;

// A ValueStoreCache that manages a PolicyValueStore for each extension that
// uses the storage.managed namespace. This class observes policy changes and
// which extensions listen for storage.onChanged(), and sends the appropriate
// updates to the corresponding PolicyValueStore on the FILE thread.
class ManagedValueStoreCache : public ValueStoreCache,
                               public policy::PolicyService::Observer {
 public:
  // |factory| is used to create databases for the PolicyValueStores.
  // |observer| is invoked/notified when a ValueStore changes.
  ManagedValueStoreCache(content::BrowserContext* context,
                         scoped_refptr<value_store::ValueStoreFactory> factory,
                         SettingsChangedCallback observer);

  ManagedValueStoreCache(const ManagedValueStoreCache&) = delete;
  ManagedValueStoreCache& operator=(const ManagedValueStoreCache&) = delete;

  ~ManagedValueStoreCache() override;

  policy::PolicyDomain policy_domain() const;

  // ValueStoreCache implementation:
  void ShutdownOnUI() override;
  void RunWithValueStoreForExtension(
      StorageCallback callback,
      scoped_refptr<const Extension> extension) override;
  void DeleteStorageSoon(const std::string& extension_id) override;

 private:
  class ExtensionTracker;

  // PolicyService::Observer implementation:
  void OnPolicyServiceInitialized(policy::PolicyDomain domain) override;
  void OnPolicyUpdated(const policy::PolicyNamespace& ns,
                       const policy::PolicyMap& previous,
                       const policy::PolicyMap& current) override;

  // Returns the policy domain that should be used for the specified profile.
  static policy::PolicyDomain GetPolicyDomain(Profile* profile);

  // Posted by OnPolicyUpdated() to update a PolicyValueStore on the backend
  // sequence.
  void UpdatePolicyOnBackend(const std::string& extension_id,
                             const policy::PolicyMap& current_policy)
      VALID_CONTEXT_REQUIRED(backend_sequence_checker_);

  // Returns an existing PolicyValueStore for |extension_id|, or NULL.
  PolicyValueStore* GetOrCreateStore(const std::string& extension_id)
      VALID_CONTEXT_REQUIRED(backend_sequence_checker_);

  // Returns true if a backing store has been created for |extension_id|.
  bool HasStore(const std::string& extension_id) const
      VALID_CONTEXT_REQUIRED(backend_sequence_checker_);

  // The profile that owns the extension system being used. This is used to
  // get the PolicyService, the EventRouter and the ExtensionService.
  raw_ptr<Profile, DanglingUntriaged> profile_
      GUARDED_BY_CONTEXT(ui_sequence_checker_);

  // The policy domain. This is used for both updating the schema registry with
  // the list of extensions and for observing the policy updates.
  policy::PolicyDomain policy_domain_ GUARDED_BY_CONTEXT(ui_sequence_checker_);

  // The |profile_|'s PolicyService.
  raw_ptr<policy::PolicyService, DanglingUntriaged> policy_service_
      GUARDED_BY_CONTEXT(ui_sequence_checker_);

  // Observes extension loading and unloading, and keeps the Profile's
  // PolicyService aware of the current list of extensions.
  std::unique_ptr<ExtensionTracker> extension_tracker_
      GUARDED_BY_CONTEXT(ui_sequence_checker_);

  scoped_refptr<value_store::ValueStoreFactory> storage_factory_
      GUARDED_BY_CONTEXT(backend_sequence_checker_);
  SequenceBoundSettingsChangedCallback observer_
      GUARDED_BY_CONTEXT(backend_sequence_checker_);

  // All the PolicyValueStores live on the FILE/backend thread, and |store_map_|
  // can be accessed only on this thread as well.
  std::map<std::string, std::unique_ptr<PolicyValueStore>> store_map_
      GUARDED_BY_CONTEXT(backend_sequence_checker_);

  SEQUENCE_CHECKER(ui_sequence_checker_);
  SEQUENCE_CHECKER(backend_sequence_checker_);

  base::WeakPtrFactory<ManagedValueStoreCache> weak_ptr_factory_{this};
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_STORAGE_MANAGED_VALUE_STORE_CACHE_H_
