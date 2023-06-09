// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/service/service_impl.h"

#include "base/memory/raw_ptr.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "components/autofill_assistant/browser/features.h"
#include "components/autofill_assistant/browser/mock_client.h"
#include "components/autofill_assistant/browser/mock_client_context.h"
#include "components/autofill_assistant/browser/protocol_utils.h"
#include "components/autofill_assistant/browser/service/mock_service_request_sender.h"
#include "components/autofill_assistant/browser/service/service.h"
#include "net/http/http_status_code.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill_assistant {

using ::base::test::RunOnceCallback;
using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;

namespace {

const char kScriptServerUrl[] = "https://www.fake.backend.com/script_server";
const char kActionServerUrl[] = "https://www.fake.backend.com/action_server";
const char kUserDataServerUrl[] =
    "https://www.fake.backend.com/user_data_server";
const char kReportProgressServerUrl[] =
    "https://www.fake.backend.com/report_progress";
const char kFakeUrl[] = "https://www.example.com";

std::string ExpectedGetUserDataRequestBody(uint64_t run_id,
                                           const std::string& client_token,
                                           bool request_payment_methods) {
  return ProtocolUtils::CreateGetUserDataRequest(
      run_id, /* request_name= */ false, /* request_email= */ false,
      /* request_phone= */ false,
      /* request_shipping= */ false,
      /* preexisting_address_ids= */ std::vector<std::string>(),
      request_payment_methods,
      /* supported_card_networks= */ std::vector<std::string>(),
      /* preexisting_payment_instrument_ids= */ std::vector<std::string>(),
      client_token);
}

class ServiceImplTest : public testing::Test {
 public:
  ServiceImplTest() {
    auto mock_client_context = std::make_unique<NiceMock<MockClientContext>>();
    mock_client_context_ = mock_client_context.get();

    auto mock_request_sender =
        std::make_unique<NiceMock<MockServiceRequestSender>>();
    mock_request_sender_ = mock_request_sender.get();

    service_ = std::make_unique<ServiceImpl>(
        &mock_client_, std::move(mock_request_sender), GURL(kScriptServerUrl),
        GURL(kActionServerUrl), GURL(kUserDataServerUrl),
        GURL(kReportProgressServerUrl), std::move(mock_client_context));
  }
  ~ServiceImplTest() override = default;

 protected:
  base::MockCallback<ServiceRequestSender::ResponseCallback>
      mock_response_callback_;
  NiceMock<MockClient> mock_client_;
  raw_ptr<NiceMock<MockClientContext>> mock_client_context_;
  raw_ptr<NiceMock<MockServiceRequestSender>> mock_request_sender_;
  std::unique_ptr<ServiceImpl> service_;
};

TEST_F(ServiceImplTest, GetScriptsForUrl) {
  EXPECT_CALL(*mock_client_context_, Update);
  EXPECT_CALL(*mock_request_sender_,
              OnSendRequest(GURL(kScriptServerUrl),
                            ProtocolUtils::CreateGetScriptsRequest(
                                GURL(kFakeUrl), mock_client_context_->AsProto(),
                                ScriptParameters()),
                            _, RpcType::SUPPORTS_SCRIPT))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, std::string("response"),
                                   ServiceRequestSender::ResponseInfo{}));
  EXPECT_CALL(mock_response_callback_,
              Run(net::HTTP_OK, std::string("response"), _));

  service_->GetScriptsForUrl(GURL(kFakeUrl), TriggerContext(),
                             mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, GetActions) {
  const std::string script_path = "fake_script_path";
  const std::string global_payload = "fake_global_payload";
  const std::string script_payload = "fake_script_payload";

  EXPECT_CALL(*mock_client_context_, Update);
  EXPECT_CALL(*mock_request_sender_,
              OnSendRequest(GURL(kActionServerUrl),
                            ProtocolUtils::CreateInitialScriptActionsRequest(
                                script_path, GURL(kFakeUrl), global_payload,
                                script_payload, mock_client_context_->AsProto(),
                                ScriptParameters(),
                                /* script_store_config= */ absl::nullopt),
                            _, RpcType::GET_ACTIONS))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, std::string("response"),
                                   ServiceRequestSender::ResponseInfo{}));
  EXPECT_CALL(mock_response_callback_,
              Run(net::HTTP_OK, std::string("response"), _));

  service_->GetActions(script_path, GURL(kFakeUrl), TriggerContext(),
                       global_payload, script_payload,
                       mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, GetActionsForwardsScriptStoreConfig) {
  const std::string script_path = "fake_script_path";
  const std::string global_payload = "fake_global_payload";
  const std::string script_payload = "fake_script_payload";

  EXPECT_CALL(*mock_client_context_, Update);

  ScriptActionRequestProto expected_get_actions;
  ScriptStoreConfig* config = expected_get_actions.mutable_initial_request()
                                  ->mutable_script_store_config();
  config->set_bundle_path("bundle/path");
  config->set_bundle_version(12);

  ScriptStoreConfig set_config;
  set_config.set_bundle_path("bundle/path");
  set_config.set_bundle_version(12);

  std::string get_actions_request;
  EXPECT_CALL(*mock_request_sender_,
              OnSendRequest(GURL(kActionServerUrl),
                            ProtocolUtils::CreateInitialScriptActionsRequest(
                                script_path, GURL(kFakeUrl), global_payload,
                                script_payload, mock_client_context_->AsProto(),
                                ScriptParameters(), set_config),
                            _, RpcType::GET_ACTIONS))
      .WillOnce(SaveArg<1>(&get_actions_request));
  service_->SetScriptStoreConfig(set_config);

  service_->GetActions(script_path, GURL(kFakeUrl), TriggerContext(),
                       global_payload, script_payload,
                       mock_response_callback_.Get());

  ScriptActionRequestProto get_actions_request_proto;
  EXPECT_TRUE(get_actions_request_proto.ParseFromString(get_actions_request));
  EXPECT_EQ("bundle/path", get_actions_request_proto.initial_request()
                               .script_store_config()
                               .bundle_path());
  EXPECT_EQ(12, get_actions_request_proto.initial_request()
                    .script_store_config()
                    .bundle_version());
}

TEST_F(ServiceImplTest, GetActionsWithoutClientToken) {
  const std::string script_path = "fake_script_path";
  const std::string global_payload = "fake_global_payload";
  const std::string script_payload = "fake_script_payload";

  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndDisableFeature(
      features::kAutofillAssistantGetPaymentsClientToken);

  EXPECT_CALL(*mock_client_context_, Update);
  EXPECT_CALL(*mock_request_sender_,
              OnSendRequest(GURL(kActionServerUrl),
                            ProtocolUtils::CreateInitialScriptActionsRequest(
                                script_path, GURL(kFakeUrl), global_payload,
                                script_payload, mock_client_context_->AsProto(),
                                ScriptParameters(),
                                /* script_store_config= */ absl::nullopt),
                            _, RpcType::GET_ACTIONS))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, std::string("response"),
                                   ServiceRequestSender::ResponseInfo{}));
  EXPECT_CALL(mock_response_callback_,
              Run(net::HTTP_OK, std::string("response"), _));

  service_->GetActions(script_path, GURL(kFakeUrl), TriggerContext(),
                       global_payload, script_payload,
                       mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, GetNextActions) {
  const std::string previous_global_payload = "fake_previous_global_payload";
  const std::string previous_script_payload = "fake_previous_script_payload";

  EXPECT_CALL(*mock_client_context_, Update);
  EXPECT_CALL(*mock_request_sender_,
              OnSendRequest(
                  GURL(kActionServerUrl),
                  ProtocolUtils::CreateNextScriptActionsRequest(
                      previous_global_payload, previous_script_payload,
                      /* processed_actions = */ {}, RoundtripTimingStats(),
                      RoundtripNetworkStats(), mock_client_context_->AsProto()),
                  _, RpcType::GET_ACTIONS))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, std::string("response"),
                                   ServiceRequestSender::ResponseInfo{}));
  EXPECT_CALL(mock_response_callback_,
              Run(net::HTTP_OK, std::string("response"), _));

  service_->GetNextActions(
      TriggerContext(), std::string("fake_previous_global_payload"),
      std::string("fake_previous_script_payload"), /* processed_actions = */ {},
      /* timing_stats = */ RoundtripTimingStats(), RoundtripNetworkStats(),
      mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, GetUserDataWithPayments) {
  const uint64_t run_id = 1;
  const std::string client_token = "token";

  CollectUserDataOptions options;
  options.request_payment_method = true;

  EXPECT_CALL(mock_client_, FetchPaymentsClientToken)
      .WillOnce(RunOnceCallback<0>(client_token));
  EXPECT_CALL(
      *mock_request_sender_,
      OnSendRequest(GURL(kUserDataServerUrl),
                    ExpectedGetUserDataRequestBody(
                        run_id, client_token, options.request_payment_method),
                    _, RpcType::GET_USER_DATA))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, std::string("response"),
                                   ServiceRequestSender::ResponseInfo{}));
  EXPECT_CALL(mock_response_callback_,
              Run(net::HTTP_OK, std::string("response"), _));

  service_->GetUserData(options, run_id, /* user_data= */ nullptr,
                        mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, GetUserDataWithoutPayments) {
  const uint64_t run_id = 1;
  CollectUserDataOptions options;

  EXPECT_CALL(mock_client_, FetchPaymentsClientToken).Times(0);
  EXPECT_CALL(*mock_request_sender_,
              OnSendRequest(GURL(kUserDataServerUrl),
                            ExpectedGetUserDataRequestBody(
                                run_id, /* client_token= */ std::string(),
                                options.request_payment_method),
                            _, RpcType::GET_USER_DATA))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, std::string("response"),
                                   ServiceRequestSender::ResponseInfo{}));
  EXPECT_CALL(mock_response_callback_,
              Run(net::HTTP_OK, std::string("response"), _));

  service_->GetUserData(options, run_id, /* user_data= */ nullptr,
                        mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, UpdateAnnotateDomModelService) {
  EXPECT_CALL(*mock_client_context_, UpdateAnnotateDomModelContext(123456));
  service_->UpdateAnnotateDomModelContext(123456);
}

TEST_F(ServiceImplTest, UpdateJsFlowLibraryLoaded) {
  EXPECT_CALL(*mock_client_context_, UpdateJsFlowLibraryLoaded(true));
  service_->UpdateJsFlowLibraryLoaded(true);
}

TEST_F(ServiceImplTest, ReportProgress) {
  const std::string token = "token";
  const std::string payload = "payload";

  EXPECT_CALL(mock_client_, GetMakeSearchesAndBrowsingBetterEnabled)
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(mock_client_, GetMetricsReportingEnabled)
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(
      *mock_request_sender_,
      OnSendRequest(GURL(kReportProgressServerUrl),
                    ProtocolUtils::CreateReportProgressRequest(token, payload),
                    _, RpcType::REPORT_PROGRESS))
      .WillOnce(RunOnceCallback<2>(net::HTTP_OK, std::string(""),
                                   ServiceRequestSender::ResponseInfo{}));
  EXPECT_CALL(mock_response_callback_, Run(net::HTTP_OK, std::string(""), _));

  service_->ReportProgress("token", "payload", mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, ReportProgressMSBBDisabled) {
  const std::string token = "token";
  const std::string payload = "payload";

  EXPECT_CALL(mock_client_, GetMakeSearchesAndBrowsingBetterEnabled)
      .Times(1)
      .WillOnce(Return(false));
  EXPECT_CALL(mock_client_, GetMetricsReportingEnabled).Times(0);

  EXPECT_CALL(
      *mock_request_sender_,
      OnSendRequest(GURL(kReportProgressServerUrl),
                    ProtocolUtils::CreateReportProgressRequest(token, payload),
                    _, RpcType::REPORT_PROGRESS))
      .Times(0);

  service_->ReportProgress("token", "payload", mock_response_callback_.Get());
}

TEST_F(ServiceImplTest, ReportProgressMetricsDisabled) {
  const std::string token = "token";
  const std::string payload = "payload";

  EXPECT_CALL(mock_client_, GetMakeSearchesAndBrowsingBetterEnabled)
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(mock_client_, GetMetricsReportingEnabled)
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(
      *mock_request_sender_,
      OnSendRequest(GURL(kReportProgressServerUrl),
                    ProtocolUtils::CreateReportProgressRequest(token, payload),
                    _, RpcType::REPORT_PROGRESS))
      .Times(0);

  service_->ReportProgress("token", "payload", mock_response_callback_.Get());
}

}  // namespace
}  // namespace autofill_assistant
