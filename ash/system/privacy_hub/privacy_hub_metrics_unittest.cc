// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/privacy_hub/privacy_hub_metrics.h"

#include "base/test/metrics/histogram_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ash::privacy_hub_metrics {

TEST(PrivacyHubMetricsTest, EnableFromNotification) {
  const base::HistogramTester histogram_tester;

  for (const bool enabled : {true, false}) {
    histogram_tester.ExpectBucketCount(
        kPrivacyHubCameraEnabledFromNotificationHistogram, enabled, 0);
    LogCameraEnabledFromNotification(enabled);
    histogram_tester.ExpectBucketCount(
        kPrivacyHubCameraEnabledFromNotificationHistogram, enabled, 1);

    histogram_tester.ExpectBucketCount(
        kPrivacyHubMicrophoneEnabledFromNotificationHistogram, enabled, 0);
    LogMicrophoneEnabledFromNotification(enabled);
    histogram_tester.ExpectBucketCount(
        kPrivacyHubMicrophoneEnabledFromNotificationHistogram, enabled, 1);
  }
}

TEST(PrivacyHubMetricsTest, EnableFromSettings) {
  const base::HistogramTester histogram_tester;

  for (const bool enabled : {true, false}) {
    histogram_tester.ExpectBucketCount(
        kPrivacyHubCameraEnabledFromSettingsHistogram, enabled, 0);
    LogCameraEnabledFromSettings(enabled);
    histogram_tester.ExpectBucketCount(
        kPrivacyHubCameraEnabledFromSettingsHistogram, enabled, 1);

    histogram_tester.ExpectBucketCount(
        kPrivacyHubMicrophoneEnabledFromSettingsHistogram, enabled, 0);
    LogMicrophoneEnabledFromSettings(enabled);
    histogram_tester.ExpectBucketCount(
        kPrivacyHubMicrophoneEnabledFromSettingsHistogram, enabled, 1);
  }
}

}  // namespace ash::privacy_hub_metrics
