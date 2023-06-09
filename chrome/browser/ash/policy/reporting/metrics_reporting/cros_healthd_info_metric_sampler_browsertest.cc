// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include "base/functional/bind.h"
#include "chrome/browser/ash/policy/core/device_policy_cros_browser_test.h"
#include "chrome/browser/ash/policy/reporting/metrics_reporting/cros_healthd_info_metric_sampler_test_utils.h"
#include "chrome/browser/ash/settings/scoped_testing_cros_settings.h"
#include "chrome/browser/ash/settings/stub_cros_settings_provider.h"
#include "chrome/browser/chromeos/reporting/metric_default_utils.h"
#include "chromeos/ash/components/settings/cros_settings_names.h"
#include "chromeos/ash/services/cros_healthd/public/cpp/fake_cros_healthd.h"
#include "chromeos/dbus/missive/missive_client_test_observer.h"
#include "components/reporting/proto/synced/metric_data.pb.h"
#include "components/reporting/proto/synced/record.pb.h"
#include "components/reporting/proto/synced/record_constants.pb.h"
#include "content/public/test/browser_test.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace ash::reporting {

// Helper for setting up CrosHealthdInfoMetrics tests.
class CrosHealthdInfoMetricsHelper {
 public:
  CrosHealthdInfoMetricsHelper() {
    // Don't allow delay in initialization. We don't use
    // |ScopedMockTimeMessageLoopTaskRunner| here because we are not able to
    // make it work with mojom.
    ::reporting::metrics::InitDelayParam::SetForTesting(base::Seconds(0));
  }
};

namespace {

namespace cros_healthd = ::ash::cros_healthd::mojom;

using ::chromeos::MissiveClientTestObserver;
using ::reporting::Destination;
using ::reporting::MetricData;
using ::reporting::Priority;
using ::reporting::Record;
using ::testing::Eq;
using ::testing::StrEq;

// Is the given record about info metric? If yes, return the underlying
// MetricData object.
absl::optional<MetricData> IsRecordInfo(const Record& record) {
  if (record.destination() != Destination::INFO_METRIC) {
    return absl::nullopt;
  }

  MetricData record_data;
  EXPECT_TRUE(record_data.ParseFromString(record.data()));
  EXPECT_TRUE(record_data.has_info_data());
  return record_data;
}

// Assert info in a record and returns the underlying MetricData object.
MetricData AssertInfo(Priority priority, const Record& record) {
  EXPECT_THAT(priority, Eq(Priority::SLOW_BATCH));
  EXPECT_THAT(record.destination(), Eq(Destination::INFO_METRIC));
  MetricData record_data;
  EXPECT_TRUE(record_data.ParseFromString(record.data()));
  EXPECT_TRUE(record_data.has_timestamp_ms());
  EXPECT_TRUE(record_data.has_info_data());
  return record_data;
}

}  // namespace

// ---- Bus ----

class BusInfoSamplerBrowserTest : public policy::DevicePolicyCrosBrowserTest {
 public:
  BusInfoSamplerBrowserTest(const BusInfoSamplerBrowserTest&) = delete;
  BusInfoSamplerBrowserTest& operator=(const BusInfoSamplerBrowserTest&) =
      delete;

 protected:
  BusInfoSamplerBrowserTest() = default;
  ~BusInfoSamplerBrowserTest() override = default;

  void SetUpOnMainThread() override {
    policy::DevicePolicyCrosBrowserTest::SetUpOnMainThread();
    scoped_testing_cros_settings_.device_settings()->SetBoolean(
        kReportDeviceSecurityStatus, true);
  }

  // Is the given record about Bus info metric?
  static bool IsRecordBusInfo(const Record& record) {
    auto record_data = IsRecordInfo(record);
    return record_data.has_value() &&
           record_data.value().info_data().has_bus_device_info();
  }

 private:
  CrosHealthdInfoMetricsHelper cros_healthd_info_metrics_helper_;
  ScopedTestingCrosSettings scoped_testing_cros_settings_;
};

IN_PROC_BROWSER_TEST_F(BusInfoSamplerBrowserTest, Thunderbolt) {
  static constexpr std::array<::reporting::ThunderboltSecurityLevel, 2>
      kErpSecurityLevels = {::reporting::ThunderboltSecurityLevel::
                                THUNDERBOLT_SECURITY_NONE_LEVEL,
                            ::reporting::ThunderboltSecurityLevel::
                                THUNDERBOLT_SECURITY_SECURE_LEVEL};
  const std::vector<cros_healthd::ThunderboltSecurityLevel>
      kHealthdSecurityLevels = {
          cros_healthd::ThunderboltSecurityLevel::kNone,
          cros_healthd::ThunderboltSecurityLevel::kSecureLevel};
  auto thunderbolt_bus_result =
      ::reporting::test::CreateThunderboltBusResult(kHealthdSecurityLevels);
  ash::cros_healthd::FakeCrosHealthd::Get()
      ->SetProbeTelemetryInfoResponseForTesting(thunderbolt_bus_result);
  MissiveClientTestObserver observer(base::BindRepeating(&IsRecordBusInfo));
  auto [priority, record] = observer.GetNextEnqueuedRecord();
  auto info_data = AssertInfo(priority, record).info_data();
  ASSERT_THAT(
      static_cast<size_t>(info_data.bus_device_info().thunderbolt_info_size()),
      Eq(kErpSecurityLevels.size()));
  for (size_t i = 0; i < kErpSecurityLevels.size(); ++i) {
    EXPECT_THAT(
        info_data.bus_device_info().thunderbolt_info(i).security_level(),
        Eq(kErpSecurityLevels[i]));
  }
}

// ---- CPU ----

class CpuInfoSamplerBrowserTest : public policy::DevicePolicyCrosBrowserTest {
 public:
  CpuInfoSamplerBrowserTest(const CpuInfoSamplerBrowserTest&) = delete;
  CpuInfoSamplerBrowserTest& operator=(const CpuInfoSamplerBrowserTest&) =
      delete;

 protected:
  CpuInfoSamplerBrowserTest() = default;
  ~CpuInfoSamplerBrowserTest() override = default;

  void SetUpOnMainThread() override {
    policy::DevicePolicyCrosBrowserTest::SetUpOnMainThread();
    scoped_testing_cros_settings_.device_settings()->SetBoolean(
        kReportDeviceCpuInfo, true);
  }

  // Is the given record about CPU info metric?
  static bool IsRecordCpuInfo(const Record& record) {
    auto record_data = IsRecordInfo(record);
    return record_data.has_value() &&
           record_data.value().info_data().has_cpu_info();
  }

 private:
  CrosHealthdInfoMetricsHelper cros_healthd_info_metrics_helper_;
  ScopedTestingCrosSettings scoped_testing_cros_settings_;
};

IN_PROC_BROWSER_TEST_F(CpuInfoSamplerBrowserTest, KeylockerUnsupported) {
  auto cpu_result = ::reporting::test::CreateCpuResult(nullptr);
  ash::cros_healthd::FakeCrosHealthd::Get()
      ->SetProbeTelemetryInfoResponseForTesting(cpu_result);
  MissiveClientTestObserver observer(base::BindRepeating(&IsRecordCpuInfo));
  auto [priority, record] = observer.GetNextEnqueuedRecord();
  auto info_data = AssertInfo(priority, record).info_data();
  ASSERT_TRUE(info_data.cpu_info().has_keylocker_info());
  EXPECT_FALSE(info_data.cpu_info().keylocker_info().configured());
  EXPECT_FALSE(info_data.cpu_info().keylocker_info().supported());
}

IN_PROC_BROWSER_TEST_F(CpuInfoSamplerBrowserTest, KeylockerConfigured) {
  auto cpu_result = ::reporting::test::CreateCpuResult(
      ::reporting::test::CreateKeylockerInfo(true));
  ash::cros_healthd::FakeCrosHealthd::Get()
      ->SetProbeTelemetryInfoResponseForTesting(cpu_result);
  MissiveClientTestObserver observer(base::BindRepeating(&IsRecordCpuInfo));
  auto [priority, record] = observer.GetNextEnqueuedRecord();
  auto info_data = AssertInfo(priority, record).info_data();
  ASSERT_TRUE(info_data.cpu_info().has_keylocker_info());
  EXPECT_TRUE(info_data.cpu_info().keylocker_info().configured());
  EXPECT_TRUE(info_data.cpu_info().keylocker_info().supported());
}

// ---- Memory ----

// Memory constants.
static constexpr int64_t kTmeMaxKeys = 2;
static constexpr int64_t kTmeKeysLength = 4;

class MemoryInfoSamplerBrowserTest
    : public policy::DevicePolicyCrosBrowserTest,
      public testing::WithParamInterface<
          ::reporting::test::MemoryInfoTestCase> {
 public:
  MemoryInfoSamplerBrowserTest(const MemoryInfoSamplerBrowserTest&) = delete;
  MemoryInfoSamplerBrowserTest& operator=(const MemoryInfoSamplerBrowserTest&) =
      delete;

 protected:
  MemoryInfoSamplerBrowserTest() = default;
  ~MemoryInfoSamplerBrowserTest() override = default;

  void SetUpOnMainThread() override {
    policy::DevicePolicyCrosBrowserTest::SetUpOnMainThread();
    scoped_testing_cros_settings_.device_settings()->SetBoolean(
        kReportDeviceMemoryInfo, true);
  }

  // Is the given record about memory info metric?
  static bool IsRecordMemoryInfo(const Record& record) {
    auto record_data = IsRecordInfo(record);
    return record_data.has_value() &&
           record_data.value().info_data().has_memory_info();
  }

  static void AssertMemoryInfo(MissiveClientTestObserver* observer) {
    auto [priority, record] = observer->GetNextEnqueuedRecord();
    MetricData record_data = AssertInfo(priority, record);
    ::reporting::test::AssertMemoryInfo(record_data, GetParam());
  }

 private:
  CrosHealthdInfoMetricsHelper cros_healthd_info_metrics_helper_;
  ScopedTestingCrosSettings scoped_testing_cros_settings_;
};

IN_PROC_BROWSER_TEST_P(MemoryInfoSamplerBrowserTest, ReportMemoryInfo) {
  const auto& test_case = GetParam();
  auto memory_result = ::reporting::test::CreateMemoryResult(
      ::reporting::test::CreateMemoryEncryptionInfo(
          test_case.healthd_encryption_state, test_case.max_keys,
          test_case.key_length, test_case.healthd_encryption_algorithm));

  ash::cros_healthd::FakeCrosHealthd::Get()
      ->SetProbeTelemetryInfoResponseForTesting(memory_result);
  MissiveClientTestObserver observer(base::BindRepeating(&IsRecordMemoryInfo));
  AssertMemoryInfo(&observer);
}

INSTANTIATE_TEST_SUITE_P(
    MemoryInfoSamplerBrowserTests,
    MemoryInfoSamplerBrowserTest,
    testing::ValuesIn<::reporting::test::MemoryInfoTestCase>({
        {"UnknownEncryptionState", cros_healthd::EncryptionState::kUnknown,
         ::reporting::MEMORY_ENCRYPTION_STATE_UNKNOWN,
         cros_healthd::CryptoAlgorithm::kUnknown,
         ::reporting::MEMORY_ENCRYPTION_ALGORITHM_UNKNOWN, 0, 0},
        {"KeyValuesSet", cros_healthd::EncryptionState::kUnknown,
         ::reporting::MEMORY_ENCRYPTION_STATE_UNKNOWN,
         cros_healthd::CryptoAlgorithm::kUnknown,
         ::reporting::MEMORY_ENCRYPTION_ALGORITHM_UNKNOWN, kTmeMaxKeys,
         kTmeKeysLength},
    }),
    [](const testing::TestParamInfo<MemoryInfoSamplerBrowserTest::ParamType>&
           info) { return info.param.test_name; });

// ---- Input ----

class InputInfoSamplerBrowserTest : public policy::DevicePolicyCrosBrowserTest {
 public:
  InputInfoSamplerBrowserTest(const InputInfoSamplerBrowserTest&) = delete;
  InputInfoSamplerBrowserTest& operator=(const InputInfoSamplerBrowserTest&) =
      delete;

 protected:
  InputInfoSamplerBrowserTest() = default;
  ~InputInfoSamplerBrowserTest() override = default;

  void SetUpOnMainThread() override {
    policy::DevicePolicyCrosBrowserTest::SetUpOnMainThread();
    scoped_testing_cros_settings_.device_settings()->SetBoolean(
        kReportDeviceGraphicsStatus, true);
  }

  // Is the given record about CPU info metric?
  static bool IsRecordTouchScreenInfo(const Record& record) {
    auto record_data = IsRecordInfo(record);
    return record_data.has_value() &&
           record_data.value().info_data().has_touch_screen_info();
  }

 private:
  CrosHealthdInfoMetricsHelper cros_healthd_info_metrics_helper_;
  ScopedTestingCrosSettings scoped_testing_cros_settings_;
};

IN_PROC_BROWSER_TEST_F(InputInfoSamplerBrowserTest, TouchScreenSingleInternal) {
  static constexpr char kSampleLibrary[] = "SampleLibrary";
  static constexpr char kSampleDevice[] = "SampleDevice";
  static constexpr int kTouchPoints = 10;

  auto input_device = cros_healthd::TouchscreenDevice::New(
      cros_healthd::InputDevice::New(
          kSampleDevice, cros_healthd::InputDevice_ConnectionType::kInternal,
          /*physical_location*/ "", /*is_enabled*/ true),
      kTouchPoints, /*has_stylus*/ true,
      /*has_stylus_garage_switch*/ false);
  std::vector<cros_healthd::TouchscreenDevicePtr> touchscreen_devices;
  touchscreen_devices.push_back(std::move(input_device));

  auto input_result = ::reporting::test::CreateInputResult(
      kSampleLibrary, std::move(touchscreen_devices));
  ash::cros_healthd::FakeCrosHealthd::Get()
      ->SetProbeTelemetryInfoResponseForTesting(input_result);
  MissiveClientTestObserver observer(
      base::BindRepeating(&IsRecordTouchScreenInfo));
  auto [priority, record] = observer.GetNextEnqueuedRecord();
  auto info_data = AssertInfo(priority, record).info_data();
  ASSERT_TRUE(info_data.has_touch_screen_info());
  ASSERT_TRUE(info_data.touch_screen_info().has_library_name());
  EXPECT_THAT(info_data.touch_screen_info().library_name(),
              StrEq(kSampleLibrary));
  ASSERT_EQ(info_data.touch_screen_info().touch_screen_devices().size(), 1);
  EXPECT_THAT(
      info_data.touch_screen_info().touch_screen_devices(0).display_name(),
      StrEq(kSampleDevice));
  EXPECT_THAT(
      info_data.touch_screen_info().touch_screen_devices(0).touch_points(),
      Eq(kTouchPoints));
  EXPECT_TRUE(
      info_data.touch_screen_info().touch_screen_devices(0).has_stylus());
}

}  // namespace ash::reporting
