// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/power/battery_discharge_reporter.h"

#include <memory>

#include "base/power_monitor/battery_level_provider.h"
#include "base/power_monitor/battery_state_sampler.h"
#include "base/power_monitor/sampling_event_source.h"
#include "base/strings/strcat.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/browser/metrics/power/power_metrics.h"
#include "chrome/browser/metrics/usage_scenario/usage_scenario_data_store.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

constexpr const char* kBatteryDischargeModeHistogramName =
    "Power.BatteryDischargeMode5";
constexpr const char* kBatteryDischargeRateMilliwattsHistogramName =
    "Power.BatteryDischargeRateMilliwatts5";
constexpr const char* kBatteryDischargeRateRelativeHistogramName =
    "Power.BatteryDischargeRateRelative5";

constexpr base::TimeDelta kTolerableDrift = base::Seconds(1);
constexpr int kFullBatteryChargeLevel = 10000;
constexpr int kHalfBatteryChargeLevel = 5000;

absl::optional<base::BatteryLevelProvider::BatteryState> MakeBatteryState(
    int current_capacity) {
  return base::BatteryLevelProvider::BatteryState{
      .battery_count = 1,
      .is_external_power_connected = false,
      .current_capacity = current_capacity,
      .full_charged_capacity = kFullBatteryChargeLevel,
      .charge_unit = base::BatteryLevelProvider::BatteryLevelUnit::kMWh};
}

struct HistogramSampleExpectation {
  std::string histogram_name_prefix;
  base::Histogram::Sample sample;
};

// For each histogram named after the combination of prefixes from
// `expectations` and suffixes from `suffixes`, verifies that there is a unique
// sample `expectation.sample`.
void ExpectHistogramSamples(
    base::HistogramTester* histogram_tester,
    const std::vector<const char*>& suffixes,
    const std::vector<HistogramSampleExpectation>& expectations) {
  for (const char* suffix : suffixes) {
    for (const auto& expectation : expectations) {
      std::string histogram_name =
          base::StrCat({expectation.histogram_name_prefix, suffix});
      SCOPED_TRACE(histogram_name);
      histogram_tester->ExpectUniqueSample(histogram_name, expectation.sample,
                                           1);
    }
  }
}

class NoopSamplingEventSource : public base::SamplingEventSource {
 public:
  NoopSamplingEventSource() = default;
  ~NoopSamplingEventSource() override = default;

  bool Start(SamplingEventCallback callback) override { return true; }
};

class NoopBatteryLevelProvider : public base::BatteryLevelProvider {
 public:
  NoopBatteryLevelProvider() = default;
  ~NoopBatteryLevelProvider() override = default;

  void GetBatteryState(
      base::OnceCallback<void(const absl::optional<BatteryState>&)> callback)
      override {}
};

class TestUsageScenarioDataStoreImpl : public UsageScenarioDataStoreImpl {
 public:
  TestUsageScenarioDataStoreImpl() = default;
  TestUsageScenarioDataStoreImpl(const TestUsageScenarioDataStoreImpl& rhs) =
      delete;
  TestUsageScenarioDataStoreImpl& operator=(
      const TestUsageScenarioDataStoreImpl& rhs) = delete;
  ~TestUsageScenarioDataStoreImpl() override = default;

  IntervalData ResetIntervalData() override { return fake_data_; }

 private:
  IntervalData fake_data_;
};

class BatteryDischargeReporterTest : public testing::Test {
 public:
  BatteryDischargeReporterTest() = default;
  BatteryDischargeReporterTest(const BatteryDischargeReporterTest& rhs) =
      delete;
  BatteryDischargeReporterTest& operator=(
      const BatteryDischargeReporterTest& rhs) = delete;
  ~BatteryDischargeReporterTest() override = default;

 protected:
  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  base::HistogramTester histogram_tester_;
};

}  // namespace

TEST_F(BatteryDischargeReporterTest, Simple) {
  TestUsageScenarioDataStoreImpl usage_scenario_data_store;

  base::BatteryStateSampler battery_state_sampler(
      std::make_unique<NoopSamplingEventSource>(),
      std::make_unique<NoopBatteryLevelProvider>());
  BatteryDischargeReporter battery_discharge_reporter(
      &battery_state_sampler, &usage_scenario_data_store);

  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel));
  task_environment_.FastForwardBy(base::Minutes(1));
  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel - 10));

  // 10 mWh discharge over 1 minute equals 600 mW.
  const int64_t kExpectedDischargeRate = 600;
  // 10 mWh discharge when capacity is 10000 mWh is 10 hundredth of a percent.
  const int64_t kExpectedDischargeRateRelative = 10;

  const std::vector<const char*> suffixes(
      {"", ".Initial", ".ZeroWindow", ".ZeroWindow.Initial"});
  ExpectHistogramSamples(
      &histogram_tester_, suffixes,
      {{kBatteryDischargeModeHistogramName,
        static_cast<int64_t>(BatteryDischargeMode::kDischarging)}});
  ExpectHistogramSamples(
      &histogram_tester_, suffixes,
      {{kBatteryDischargeRateMilliwattsHistogramName, kExpectedDischargeRate}});
  ExpectHistogramSamples(&histogram_tester_, suffixes,
                         {{kBatteryDischargeRateRelativeHistogramName,
                           kExpectedDischargeRateRelative}});
}

TEST_F(BatteryDischargeReporterTest, BatteryDischargeCaptureIsTooLate) {
  TestUsageScenarioDataStoreImpl usage_scenario_data_store;

  base::BatteryStateSampler battery_state_sampler(
      std::make_unique<NoopSamplingEventSource>(),
      std::make_unique<NoopBatteryLevelProvider>());
  BatteryDischargeReporter battery_discharge_reporter(
      &battery_state_sampler, &usage_scenario_data_store);

  battery_discharge_reporter.OnBatteryStateSampled(MakeBatteryState(5000));

  const base::TimeDelta kTooLate = base::Minutes(1) + kTolerableDrift;
  task_environment_.FastForwardBy(kTooLate);
  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel - 10));

  // No rate because the interval is invalid.
  histogram_tester_.ExpectUniqueSample(kBatteryDischargeModeHistogramName,
                                       BatteryDischargeMode::kInvalidInterval,
                                       1);
  histogram_tester_.ExpectTotalCount(
      kBatteryDischargeRateMilliwattsHistogramName, 0);
  histogram_tester_.ExpectTotalCount(kBatteryDischargeRateRelativeHistogramName,
                                     0);
}

TEST_F(BatteryDischargeReporterTest, BatteryDischargeCaptureIsLate) {
  TestUsageScenarioDataStoreImpl usage_scenario_data_store;

  base::BatteryStateSampler battery_state_sampler(
      std::make_unique<NoopSamplingEventSource>(),
      std::make_unique<NoopBatteryLevelProvider>());
  BatteryDischargeReporter battery_discharge_reporter(
      &battery_state_sampler, &usage_scenario_data_store);

  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel));

  const base::TimeDelta kLate =
      base::Minutes(1) + kTolerableDrift - base::Microseconds(1);
  task_environment_.FastForwardBy(kLate);
  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel - 10));

  // No rate because the interval is invalid.
  histogram_tester_.ExpectUniqueSample(kBatteryDischargeModeHistogramName,
                                       BatteryDischargeMode::kDischarging, 1);
  histogram_tester_.ExpectTotalCount(
      kBatteryDischargeRateMilliwattsHistogramName, 1);
  histogram_tester_.ExpectTotalCount(kBatteryDischargeRateRelativeHistogramName,
                                     1);
}

TEST_F(BatteryDischargeReporterTest, BatteryDischargeCaptureIsTooEarly) {
  TestUsageScenarioDataStoreImpl usage_scenario_data_store;

  base::BatteryStateSampler battery_state_sampler(
      std::make_unique<NoopSamplingEventSource>(),
      std::make_unique<NoopBatteryLevelProvider>());
  BatteryDischargeReporter battery_discharge_reporter(
      &battery_state_sampler, &usage_scenario_data_store);

  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel));

  const base::TimeDelta kTooEarly = base::Minutes(1) - kTolerableDrift;
  task_environment_.FastForwardBy(kTooEarly);
  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel - 10));

  // No rate because the interval is invalid.
  histogram_tester_.ExpectUniqueSample(kBatteryDischargeModeHistogramName,
                                       BatteryDischargeMode::kInvalidInterval,
                                       1);
  histogram_tester_.ExpectTotalCount(
      kBatteryDischargeRateMilliwattsHistogramName, 0);
  histogram_tester_.ExpectTotalCount(kBatteryDischargeRateRelativeHistogramName,
                                     0);
}

TEST_F(BatteryDischargeReporterTest, BatteryDischargeCaptureIsEarly) {
  TestUsageScenarioDataStoreImpl usage_scenario_data_store;

  base::BatteryStateSampler battery_state_sampler(
      std::make_unique<NoopSamplingEventSource>(),
      std::make_unique<NoopBatteryLevelProvider>());
  BatteryDischargeReporter battery_discharge_reporter(
      &battery_state_sampler, &usage_scenario_data_store);

  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel));

  const base::TimeDelta kEarly =
      base::Minutes(1) - kTolerableDrift + base::Microseconds(1);
  task_environment_.FastForwardBy(kEarly);
  battery_discharge_reporter.OnBatteryStateSampled(
      MakeBatteryState(kHalfBatteryChargeLevel - 10));

  // No rate because the interval is invalid.
  histogram_tester_.ExpectUniqueSample(kBatteryDischargeModeHistogramName,
                                       BatteryDischargeMode::kDischarging, 1);
  histogram_tester_.ExpectTotalCount(
      kBatteryDischargeRateMilliwattsHistogramName, 1);
  histogram_tester_.ExpectTotalCount(kBatteryDischargeRateRelativeHistogramName,
                                     1);
}
