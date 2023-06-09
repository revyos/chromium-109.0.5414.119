// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ash/components/network/managed_cellular_pref_handler.h"

#include "base/test/task_environment.h"
#include "chromeos/ash/components/network/network_state_test_helper.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash {

namespace {

const char kIccid[] = "1234567890";
const char kSmdpAddress[] = "LPA:1$SmdpAddress$ActivationCode";

class FakeObserver : public ManagedCellularPrefHandler::Observer {
 public:
  FakeObserver() = default;
  ~FakeObserver() override = default;

  int change_count() const { return change_count_; }

  // ManagedCellularPref::Observer:
  void OnManagedCellularPrefChanged() override { ++change_count_; }

 private:
  int change_count_ = 0u;
};

}  // namespace

class ManagedCellularPrefHandlerTest : public testing::Test {
 protected:
  ManagedCellularPrefHandlerTest() = default;
  ~ManagedCellularPrefHandlerTest() override = default;

  // testing::Test:
  void SetUp() override {
    ManagedCellularPrefHandler::RegisterLocalStatePrefs(
        device_prefs_.registry());
  }

  void TearDown() override {
    managed_cellular_pref_handler_->RemoveObserver(&observer_);
    managed_cellular_pref_handler_.reset();
  }

  void Init() {
    if (managed_cellular_pref_handler_ &&
        managed_cellular_pref_handler_->HasObserver(&observer_)) {
      managed_cellular_pref_handler_->RemoveObserver(&observer_);
    }
    managed_cellular_pref_handler_ =
        std::make_unique<ManagedCellularPrefHandler>();
    managed_cellular_pref_handler_->AddObserver(&observer_);
    managed_cellular_pref_handler_->Init(helper_.network_state_handler());
  }

  void SetDevicePrefs(bool set_to_null = false) {
    managed_cellular_pref_handler_->SetDevicePrefs(
        set_to_null ? nullptr : &device_prefs_);
  }

  void AddIccidSmdpPair(const std::string& iccid,
                        const std::string& smdp_address) {
    managed_cellular_pref_handler_->AddIccidSmdpPair(iccid, smdp_address);
  }

  void RemovePairForIccid(const std::string& iccid) {
    managed_cellular_pref_handler_->RemovePairWithIccid(iccid);
  }

  const std::string* GetSmdpAddressFromIccid(const std::string& iccid) {
    return managed_cellular_pref_handler_->GetSmdpAddressFromIccid(iccid);
  }

  int NumObserverEvents() { return observer_.change_count(); }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  NetworkStateTestHelper helper_{/*use_default_devices_and_services=*/false};
  TestingPrefServiceSimple device_prefs_;
  FakeObserver observer_;

  std::unique_ptr<ManagedCellularPrefHandler> managed_cellular_pref_handler_;
};

TEST_F(ManagedCellularPrefHandlerTest, AddRemoveIccidSmdpPair) {
  Init();
  SetDevicePrefs();

  // Add a pair of ICCID - SMDP address pair to pref and verify that the correct
  // value can be retrieved.
  AddIccidSmdpPair(kIccid, kSmdpAddress);
  EXPECT_EQ(1, NumObserverEvents());
  const std::string* smdp_address = GetSmdpAddressFromIccid(kIccid);
  EXPECT_TRUE(smdp_address);
  EXPECT_EQ(kSmdpAddress, *smdp_address);
  EXPECT_FALSE(GetSmdpAddressFromIccid("00000000000"));
  RemovePairForIccid(kIccid);
  EXPECT_EQ(2, NumObserverEvents());
  smdp_address = GetSmdpAddressFromIccid(kIccid);
  EXPECT_FALSE(smdp_address);
}

TEST_F(ManagedCellularPrefHandlerTest, NoDevicePrefSet) {
  Init();
  SetDevicePrefs(/*set_to_null=*/true);

  // Verify that when there's no device prefs, no SMDP address can be
  // retrieved.
  const std::string* smdp_address = GetSmdpAddressFromIccid(kIccid);
  EXPECT_FALSE(smdp_address);
  AddIccidSmdpPair(kIccid, kSmdpAddress);
  EXPECT_EQ(0, NumObserverEvents());
  smdp_address = GetSmdpAddressFromIccid(kIccid);
  EXPECT_FALSE(smdp_address);
}

}  // namespace ash
