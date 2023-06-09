// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SERVICE_SERVICE_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SERVICE_SERVICE_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "components/autofill_assistant/browser/service.pb.h"
#include "components/autofill_assistant/browser/service/service_request_sender.h"
#include "components/autofill_assistant/browser/user_data.h"
#include "url/gurl.h"

namespace autofill_assistant {
class TriggerContext;

// Base interface for an autofill assistant service. Services provide methods to
// asynchronously query scripts for a particular URL, as well as actions for a
// particular script.
class Service {
 public:
  virtual ~Service() = default;

  // Get scripts for a given |url|, which should be a valid URL.
  virtual void GetScriptsForUrl(
      const GURL& url,
      const TriggerContext& trigger_context,
      ServiceRequestSender::ResponseCallback callback) = 0;

  // Get actions.
  virtual void GetActions(const std::string& script_path,
                          const GURL& url,
                          const TriggerContext& trigger_context,
                          const std::string& global_payload,
                          const std::string& script_payload,
                          ServiceRequestSender::ResponseCallback callback) = 0;

  // Get next sequence of actions according to server payloads in previous
  // response.
  virtual void GetNextActions(
      const TriggerContext& trigger_context,
      const std::string& previous_global_payload,
      const std::string& previous_script_payload,
      const std::vector<ProcessedActionProto>& processed_actions,
      const RoundtripTimingStats& timing_stats,
      const RoundtripNetworkStats& network_stats,
      ServiceRequestSender::ResponseCallback callback) = 0;

  virtual void SetScriptStoreConfig(
      const ScriptStoreConfig& script_store_config) {}

  // Get user data.
  virtual void GetUserData(const CollectUserDataOptions& options,
                           uint64_t run_id,
                           const UserData* user_data,
                           ServiceRequestSender::ResponseCallback callback) = 0;

  virtual void SetDisableRpcSigning(bool disable_rpc_signing) {}

  virtual void UpdateAnnotateDomModelContext(int64_t model_version) {}

  virtual void UpdateJsFlowLibraryLoaded(bool js_flow_library_loaded) {}

  virtual void ReportProgress(
      const std::string& token,
      const std::string& payload,
      ServiceRequestSender::ResponseCallback callback) = 0;

 protected:
  Service() = default;
};

}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SERVICE_SERVICE_H_
