// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SCRIPT_PARAMETERS_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SCRIPT_PARAMETERS_H_

#include <string>

#include "base/containers/flat_map.h"
#include "components/autofill_assistant/browser/service.pb.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace autofill_assistant {
class UserData;

// Stores script parameters and provides access to the subset of client-relevant
// parameters.
class ScriptParameters {
 public:
  // TODO(arbesser): Expect properly typed parameters instead.
  ScriptParameters(const base::flat_map<std::string, std::string>& parameters);
  ScriptParameters();
  ~ScriptParameters();
  ScriptParameters(const ScriptParameters&) = delete;
  ScriptParameters& operator=(const ScriptParameters&) = delete;
  ScriptParameters& operator=(ScriptParameters&&);

  // Merges |another| into this. Does not overwrite existing values.
  void MergeWith(const ScriptParameters& another);

  // Returns whether there is a script parameter that satisfies |proto|.
  bool Matches(const ScriptParameterMatchProto& proto) const;

  // Returns a proto representation of this class. If
  // |only_non_sensitive_allowlisted| is set to true, this will only return the
  // list of non sensitive script parameters that client requests are allowed
  // to send to the backend.
  google::protobuf::RepeatedPtrField<ScriptParameterProto> ToProto(
      bool only_non_sensitive_allowlisted = false) const;

  // Update the device only parameters. New parameters always take precedence.
  void UpdateDeviceOnlyParameters(
      const base::flat_map<std::string, std::string>& parameters);

  // Write parameters and device only parameters to |UserData|, by adding them
  // to the additional values with a "param:" prefix.
  void WriteToUserData(UserData* user_data) const;

  // Returns whether |experiment_id| is contained in the experiments parameter.
  bool HasExperimentId(const std::string& experiment_id) const;

  // Getters for specific parameters.
  absl::optional<std::string> GetOverlayColors() const;
  absl::optional<std::string> GetPasswordChangeUsername() const;
  bool GetRequestsTriggerScript() const;
  // Returns true if the parameter is set and valid (either "true" or "false").
  bool HasStartImmediately() const;
  bool GetStartImmediately() const;
  bool GetEnabled() const;
  absl::optional<std::string> GetOriginalDeeplink() const;
  bool GetTriggerScriptExperiment() const;
  absl::optional<std::string> GetIntent() const;
  absl::optional<std::string> GetCallerEmail() const;
  bool GetEnableTts() const;
  bool GetDisableScrollbarFading() const;
  bool GetEnableObserverWaitForDom() const;
  absl::optional<int> GetCaller() const;
  absl::optional<int> GetSource() const;
  std::vector<std::string> GetExperiments() const;
  bool GetDisableRpcSigning() const;
  bool GetSendAnnotateDomModelVersion() const;
  bool GetRunHeadless() const;
  absl::optional<std::string> GetFieldTrialGroup(
      const int field_trial_slot) const;
  absl::optional<bool> GetIsNoRoundtrip() const;

  // Details parameters.
  bool GetDetailsShowInitial() const;
  // Returns true if the parameter is set and valid (either "true" or "false").
  bool HasDetailsShowInitial() const;
  absl::optional<std::string> GetDetailsTitle() const;
  absl::optional<std::string> GetDetailsDescriptionLine1() const;
  absl::optional<std::string> GetDetailsDescriptionLine2() const;
  absl::optional<std::string> GetDetailsDescriptionLine3() const;
  absl::optional<std::string> GetDetailsImageUrl() const;
  absl::optional<std::string> GetDetailsImageAccessibilityHint() const;
  absl::optional<std::string> GetDetailsImageClickthroughUrl() const;
  absl::optional<std::string> GetDetailsTotalPriceLabel() const;
  absl::optional<std::string> GetDetailsTotalPrice() const;

 private:
  absl::optional<std::string> GetParameter(const std::string& name) const;

  base::flat_map<std::string, ValueProto> parameters_;
};

}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_SCRIPT_PARAMETERS_H_
