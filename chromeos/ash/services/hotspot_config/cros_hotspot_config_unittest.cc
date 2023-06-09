// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ash/services/hotspot_config/cros_hotspot_config.h"

#include "ash/constants/ash_features.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "chromeos/ash/components/network/network_handler.h"
#include "chromeos/ash/components/network/network_handler_test_helper.h"
#include "chromeos/ash/services/hotspot_config/public/cpp/cros_hotspot_config_test_observer.h"
#include "chromeos/login/login_state/login_state.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/shill/dbus-constants.h"

namespace ash::hotspot_config {

namespace {

const char kHotspotConfigSSID[] = "hotspot_SSID";
const char kHotspotConfigPassphrase[] = "hotspot_passphrase";
const char kCellularServicePath[] = "/service/cellular0";
const char kCellularServiceGuid[] = "cellular_guid0";
const char kCellularServiceName[] = "cellular_name0";

mojom::HotspotConfigPtr GenerateTestConfig() {
  auto mojom_config = mojom::HotspotConfig::New();
  mojom_config->auto_disable = false;
  mojom_config->band = mojom::WiFiBand::k5GHz;
  mojom_config->security = mojom::WiFiSecurityMode::kWpa2;
  mojom_config->ssid = kHotspotConfigSSID;
  mojom_config->passphrase = kHotspotConfigPassphrase;
  return mojom_config;
}

}  // namespace

class CrosHotspotConfigTest : public testing::Test {
 public:
  CrosHotspotConfigTest() = default;
  CrosHotspotConfigTest(const CrosHotspotConfigTest&) = delete;
  CrosHotspotConfigTest& operator=(const CrosHotspotConfigTest&) = delete;
  ~CrosHotspotConfigTest() override = default;

  // testing::Test:
  void SetUp() override {
    feature_list_.InitAndEnableFeature(features::kHotspot);
    LoginState::Initialize();
    LoginState::Get()->set_always_logged_in(false);

    network_handler_test_helper_ = std::make_unique<NetworkHandlerTestHelper>();
    network_handler_test_helper_->AddDefaultProfiles();
    network_handler_test_helper_->ResetDevicesAndServices();
    NetworkHandler* network_handler = NetworkHandler::Get();
    // Use absl::WrapUnique(new CrosHotspotConfig(...)) instead of
    // std::make_unique<CrosHotspotConfig> to access a private constructor.
    cros_hotspot_config_ = absl::WrapUnique(
        new CrosHotspotConfig(network_handler->hotspot_state_handler(),
                              network_handler->hotspot_controller()));
    base::RunLoop().RunUntilIdle();
  }

  void TearDown() override {
    cros_hotspot_config_.reset();
    network_handler_test_helper_.reset();
    LoginState::Shutdown();
  }

  void SetupObserver() {
    observer_ = std::make_unique<CrosHotspotConfigTestObserver>();
    cros_hotspot_config_->AddObserver(observer_->GenerateRemote());
  }

  void SetValidHotspotCapabilities() {
    base::Value capabilities_dict(base::Value::Type::DICTIONARY);
    base::Value upstream_list(base::Value::Type::LIST);
    upstream_list.Append(base::Value(shill::kTypeCellular));
    capabilities_dict.GetDict().Set(shill::kTetheringCapUpstreamProperty,
                                    std::move(upstream_list));
    // Add WiFi to the downstream technology list in Shill
    base::Value downstream_list(base::Value::Type::LIST);
    downstream_list.Append(base::Value(shill::kTypeWifi));
    capabilities_dict.GetDict().Set(shill::kTetheringCapDownstreamProperty,
                                    std::move(downstream_list));
    // Add allowed WiFi security mode in Shill
    base::Value security_list(base::Value::Type::LIST);
    security_list.Append(base::Value(shill::kSecurityWpa2));
    security_list.Append(base::Value(shill::kSecurityWpa3));
    capabilities_dict.GetDict().Set(shill::kTetheringCapSecurityProperty,
                                    std::move(security_list));
    network_handler_test_helper_->manager_test()->SetManagerProperty(
        shill::kTetheringCapabilitiesProperty, capabilities_dict);
    base::RunLoop().RunUntilIdle();
  }

  void AddActiveCellularService() {
    network_handler_test_helper_->service_test()->AddService(
        kCellularServicePath, kCellularServiceGuid, kCellularServiceName,
        shill::kTypeCellular, shill::kStateOnline, /*visible=*/true);
    base::RunLoop().RunUntilIdle();
  }

  void SetHotspotStateInShill(const std::string& state) {
    // Update tethering status to active in Shill.
    base::Value status_dict(base::Value::Type::DICTIONARY);
    status_dict.GetDict().Set(shill::kTetheringStatusStateProperty,
                              base::Value(state));
    network_handler_test_helper_->manager_test()->SetManagerProperty(
        shill::kTetheringStatusProperty, status_dict);
    base::RunLoop().RunUntilIdle();
  }

  void SetReadinessCheckResultReady() {
    network_handler_test_helper_->manager_test()
        ->SetSimulateCheckTetheringReadinessResult(
            FakeShillSimulatedResult::kSuccess,
            shill::kTetheringReadinessReady);
    base::RunLoop().RunUntilIdle();
  }

  mojom::HotspotInfoPtr GetHotspotInfo() {
    mojom::HotspotInfoPtr out_result;
    base::RunLoop run_loop;
    cros_hotspot_config_->GetHotspotInfo(
        base::BindLambdaForTesting([&](mojom::HotspotInfoPtr result) {
          out_result = std::move(result);
          run_loop.QuitClosure();
        }));
    run_loop.RunUntilIdle();
    return out_result;
  }

  mojom::SetHotspotConfigResult SetHotspotConfig(
      mojom::HotspotConfigPtr mojom_config) {
    base::RunLoop run_loop;
    mojom::SetHotspotConfigResult out_result;
    cros_hotspot_config_->SetHotspotConfig(
        std::move(mojom_config),
        base::BindLambdaForTesting([&](mojom::SetHotspotConfigResult result) {
          out_result = result;
          run_loop.QuitClosure();
        }));
    run_loop.RunUntilIdle();
    return out_result;
  }

  mojom::HotspotControlResult EnableHotspot() {
    base::RunLoop run_loop;
    mojom::HotspotControlResult out_result;
    cros_hotspot_config_->EnableHotspot(
        base::BindLambdaForTesting([&](mojom::HotspotControlResult result) {
          out_result = result;
          run_loop.QuitClosure();
        }));
    run_loop.RunUntilIdle();
    return out_result;
  }

  mojom::HotspotControlResult DisableHotspot() {
    base::RunLoop run_loop;
    mojom::HotspotControlResult out_result;
    cros_hotspot_config_->DisableHotspot(
        base::BindLambdaForTesting([&](mojom::HotspotControlResult result) {
          out_result = result;
          run_loop.QuitClosure();
        }));
    run_loop.RunUntilIdle();
    return out_result;
  }

  void LoginToRegularUser() {
    LoginState::Get()->SetLoggedInState(LoginState::LOGGED_IN_ACTIVE,
                                        LoginState::LOGGED_IN_USER_REGULAR);
    task_environment_.RunUntilIdle();
  }

  NetworkHandlerTestHelper* helper() {
    return network_handler_test_helper_.get();
  }

  CrosHotspotConfigTestObserver* observer() { return observer_.get(); }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  base::test::ScopedFeatureList feature_list_;
  std::unique_ptr<NetworkHandlerTestHelper> network_handler_test_helper_;
  std::unique_ptr<CrosHotspotConfig> cros_hotspot_config_;
  std::unique_ptr<CrosHotspotConfigTestObserver> observer_;
};

TEST_F(CrosHotspotConfigTest, GetHotspotInfo) {
  SetupObserver();
  auto hotspot_info = GetHotspotInfo();
  EXPECT_EQ(hotspot_info->state, mojom::HotspotState::kDisabled);
  EXPECT_EQ(hotspot_info->client_count, 0u);
  EXPECT_EQ(hotspot_info->allow_status,
            mojom::HotspotAllowStatus::kDisallowedNoCellularUpstream);
  EXPECT_EQ(hotspot_info->allowed_wifi_security_modes.size(), 0u);
  EXPECT_FALSE(hotspot_info->config);

  SetReadinessCheckResultReady();
  SetValidHotspotCapabilities();
  base::RunLoop().RunUntilIdle();
  hotspot_info = GetHotspotInfo();
  EXPECT_EQ(hotspot_info->state, mojom::HotspotState::kDisabled);
  EXPECT_EQ(hotspot_info->client_count, 0u);
  EXPECT_EQ(hotspot_info->allow_status,
            mojom::HotspotAllowStatus::kDisallowedNoMobileData);
  EXPECT_EQ(hotspot_info->allowed_wifi_security_modes.size(), 2u);
  EXPECT_FALSE(hotspot_info->config);
  EXPECT_EQ(observer()->hotspot_info_changed_count(), 1u);

  AddActiveCellularService();
  base::RunLoop().RunUntilIdle();
  hotspot_info = GetHotspotInfo();
  EXPECT_EQ(hotspot_info->allow_status, mojom::HotspotAllowStatus::kAllowed);
  EXPECT_EQ(observer()->hotspot_info_changed_count(), 2u);

  SetHotspotStateInShill(shill::kTetheringStateActive);
  EXPECT_EQ(GetHotspotInfo()->state, mojom::HotspotState::kEnabled);
  EXPECT_EQ(observer()->hotspot_info_changed_count(), 3u);

  SetHotspotStateInShill(shill::kTetheringStateIdle);
  EXPECT_EQ(GetHotspotInfo()->state, mojom::HotspotState::kDisabled);
  EXPECT_EQ(observer()->hotspot_info_changed_count(), 4u);

  // Simulate user starting tethering and failed.
  SetHotspotStateInShill(shill::kTetheringStateStarting);
  EXPECT_EQ(observer()->hotspot_info_changed_count(), 5u);
  // Update tethering status to failure in Shill.
  base::Value status_dict(base::Value::Type::DICTIONARY);
  status_dict.GetDict().Set(shill::kTetheringStatusStateProperty,
                            base::Value(shill::kTetheringStateFailure));
  status_dict.GetDict().Set(
      shill::kTetheringStatusErrorProperty,
      base::Value(shill::kTetheringErrorUpstreamNotReady));
  helper()->manager_test()->SetManagerProperty(shill::kTetheringStatusProperty,
                                               status_dict);
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(GetHotspotInfo()->state, mojom::HotspotState::kDisabled);
  EXPECT_EQ(observer()->hotspot_info_changed_count(), 6u);
  EXPECT_EQ(observer()->hotspot_state_failed_count(), 1u);
  EXPECT_EQ(shill::kTetheringErrorUpstreamNotReady,
            observer()->last_hotspot_failed_error());
}

TEST_F(CrosHotspotConfigTest, SetHotspotConfig) {
  SetupObserver();
  // Vrerifies that return failed when the user is not login.
  EXPECT_EQ(mojom::SetHotspotConfigResult::kFailedNotLogin,
            SetHotspotConfig(GenerateTestConfig()));
  EXPECT_FALSE(GetHotspotInfo()->config);

  LoginToRegularUser();
  EXPECT_EQ(mojom::SetHotspotConfigResult::kSuccess,
            SetHotspotConfig(GenerateTestConfig()));
  auto hotspot_info = GetHotspotInfo();
  EXPECT_TRUE(hotspot_info->config);
  EXPECT_FALSE(hotspot_info->config->auto_disable);
  EXPECT_EQ(hotspot_info->config->band, mojom::WiFiBand::k5GHz);
  EXPECT_EQ(hotspot_info->config->security, mojom::WiFiSecurityMode::kWpa2);
  EXPECT_EQ(hotspot_info->config->ssid, kHotspotConfigSSID);
  EXPECT_EQ(hotspot_info->config->passphrase, kHotspotConfigPassphrase);
  EXPECT_EQ(observer()->hotspot_info_changed_count(), 1u);
}

TEST_F(CrosHotspotConfigTest, EnableHotspot) {
  EXPECT_EQ(mojom::HotspotControlResult::kNotAllowed, EnableHotspot());

  SetReadinessCheckResultReady();
  SetValidHotspotCapabilities();
  AddActiveCellularService();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(mojom::HotspotControlResult::kSuccess, EnableHotspot());

  // Simulate check tethering readiness operation fail.
  helper()->manager_test()->SetSimulateCheckTetheringReadinessResult(
      FakeShillSimulatedResult::kFailure,
      /*readiness_status=*/std::string());
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(mojom::HotspotControlResult::kReadinessCheckFailed,
            EnableHotspot());
}

TEST_F(CrosHotspotConfigTest, DisableHotspot) {
  EXPECT_EQ(mojom::HotspotControlResult::kSuccess, DisableHotspot());
}

}  // namespace ash::hotspot_config
