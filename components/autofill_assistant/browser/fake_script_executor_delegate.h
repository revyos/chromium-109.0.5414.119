// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_FAKE_SCRIPT_EXECUTOR_DELEGATE_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_FAKE_SCRIPT_EXECUTOR_DELEGATE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "components/autofill_assistant/browser/client_settings.h"
#include "components/autofill_assistant/browser/script_executor_delegate.h"
#include "components/autofill_assistant/browser/trigger_context.h"

namespace password_manager {
class PasswordChangeSuccessTracker;
}

namespace autofill_assistant {

// Implementation of ScriptExecutorDelegate that's convenient to use in
// unittests.
class FakeScriptExecutorDelegate : public ScriptExecutorDelegate {
 public:
  FakeScriptExecutorDelegate();

  FakeScriptExecutorDelegate(const FakeScriptExecutorDelegate&) = delete;
  FakeScriptExecutorDelegate& operator=(const FakeScriptExecutorDelegate&) =
      delete;

  ~FakeScriptExecutorDelegate() override;

  const ClientSettings& GetSettings() override;
  const GURL& GetCurrentURL() override;
  const GURL& GetDeeplinkURL() override;
  const GURL& GetScriptURL() override;
  WebController* GetWebController() override;
  TriggerContext* GetTriggerContext() override;
  autofill::PersonalDataManager* GetPersonalDataManager() override;
  WebsiteLoginManager* GetWebsiteLoginManager() override;
  password_manager::PasswordChangeSuccessTracker*
  GetPasswordChangeSuccessTracker() override;
  content::WebContents* GetWebContents() override;
  const std::string GetLocale() override;
  void SetJsFlowLibrary(const std::string& js_flow_library) override;
  JsFlowDevtoolsWrapper* GetJsFlowDevtoolsWrapper() override;
  std::string GetEmailAddressForAccessTokenAccount() override;
  ukm::UkmRecorder* GetUkmRecorder() override;
  bool EnterState(AutofillAssistantState state) override;
  AutofillAssistantState GetState() const override;
  void SetTouchableElementArea(const ElementAreaProto& element) override;
  void WriteUserData(
      base::OnceCallback<void(UserData*, UserDataFieldChange*)>) override;
  void SetViewportMode(ViewportMode mode) override;
  ViewportMode GetViewportMode() override;
  void SetClientSettings(const ClientSettingsProto& client_settings) override;
  UserModel* GetUserModel() override;
  UserData* GetUserData() override;
  void ExpectNavigation() override;
  bool HasNavigationError() override;
  bool IsNavigatingToNewDocument() override;
  void RequireUI() override;
  void AddNavigationListener(
      ScriptExecutorDelegate::NavigationListener* listener) override;
  void RemoveNavigationListener(
      ScriptExecutorDelegate::NavigationListener* listener) override;
  void SetBrowseDomainsAllowlist(std::vector<std::string> domains) override;
  void SetOverlayBehavior(
      ConfigureUiStateProto::OverlayBehavior overlay_behavior) override;
  void SetBrowseModeInvisible(bool invisible) override;
  ProcessedActionStatusDetailsProto& GetLogInfo() override;
  bool MustUseBackendData() const override;
  bool IsXmlSigned(const std::string& xml_string) const override;
  const std::vector<std::string> ExtractValuesFromSingleTagXml(
      const std::string& xml_string,
      const std::vector<std::string>& keys) const override;
  void OnActionsResponseReceived(
      const RoundtripNetworkStats& network_stats) override;

  bool ShouldShowWarning() override;

  ClientSettings* GetMutableSettings() { return &client_settings_; }

  void SetCurrentURL(const GURL& url) { current_url_ = url; }

  void SetWebController(WebController* web_controller) {
    web_controller_ = web_controller;
  }

  void SetWebContents(content::WebContents* web_contents) {
    web_contents_ = web_contents;
  }

  void SetTriggerContext(std::unique_ptr<TriggerContext> trigger_context) {
    trigger_context_ = std::move(trigger_context);
  }

  void SetUserModel(UserModel* user_model) { user_model_ = user_model; }
  std::vector<AutofillAssistantState> GetStateHistory() {
    return state_history_;
  }
  std::vector<ElementAreaProto> GetTouchableElementAreaHistory() {
    return touchable_element_area_history_;
  }

  void UpdateNavigationState(bool navigating, bool error) {
    navigating_to_new_document_ = navigating;
    navigation_error_ = error;

    for (auto* listener : navigation_listeners_) {
      listener->OnNavigationStateChanged();
    }
  }

  bool HasNavigationListeners() { return !navigation_listeners_.empty(); }

  bool IsUIRequired() { return require_ui_; }

  std::vector<std::string>* GetCurrentBrowseDomainsList();

  void SetMustUseBackendData(bool must_use_backend_data) {
    must_use_backend_data_ = must_use_backend_data;
  }

  RoundtripNetworkStats GetRoundtripNetworkStats() const {
    return roundtrip_network_stats_;
  }

 private:
  ClientSettings client_settings_;
  GURL current_url_;
  raw_ptr<WebController> web_controller_ = nullptr;
  raw_ptr<content::WebContents, DanglingUntriaged> web_contents_ = nullptr;
  std::unique_ptr<JsFlowDevtoolsWrapper> js_flow_devtools_wrapper_;
  std::string js_flow_library_;
  std::unique_ptr<TriggerContext> trigger_context_;
  std::vector<AutofillAssistantState> state_history_;
  std::vector<ElementAreaProto> touchable_element_area_history_;
  std::unique_ptr<UserData> payment_request_info_;
  bool navigating_to_new_document_ = false;
  bool navigation_error_ = false;
  base::flat_set<ScriptExecutorDelegate::NavigationListener*>
      navigation_listeners_;
  ViewportMode viewport_mode_ = ViewportMode::NO_RESIZE;
  std::vector<std::string> browse_domains_;
  raw_ptr<UserModel> user_model_ = nullptr;
  raw_ptr<UserData> user_data_ = nullptr;
  ProcessedActionStatusDetailsProto log_info_;
  bool must_use_backend_data_ = false;
  RoundtripNetworkStats roundtrip_network_stats_;

  bool require_ui_ = false;
};

}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_FAKE_SCRIPT_EXECUTOR_DELEGATE_H_
