// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REPORTING_METRICS_METRIC_EVENT_OBSERVER_MANAGER_H_
#define COMPONENTS_REPORTING_METRICS_METRIC_EVENT_OBSERVER_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace reporting {

class EventDrivenTelemetrySamplerPool;
class MetricData;
class MetricEventObserver;
class MetricReportingController;
class MetricReportQueue;
class ReportingSettings;

// Class to manage report
class MetricEventObserverManager {
 public:
  MetricEventObserverManager(
      std::unique_ptr<MetricEventObserver> event_observer,
      MetricReportQueue* metric_report_queue,
      ReportingSettings* reporting_settings,
      const std::string& enable_setting_path,
      bool setting_enabled_default_value,
      EventDrivenTelemetrySamplerPool* sampler_pool);

  MetricEventObserverManager(const MetricEventObserverManager& other) = delete;
  MetricEventObserverManager& operator=(
      const MetricEventObserverManager& other) = delete;

  virtual ~MetricEventObserverManager();

 private:
  void SetReportingEnabled(bool is_enabled);

  void OnEventObserved(MetricData metric_data);

  void MergeAndReport(MetricData event_metric_data,
                      absl::optional<MetricData> telemetry_metric_data);

  const std::unique_ptr<MetricEventObserver> event_observer_;

  const raw_ptr<MetricReportQueue> metric_report_queue_;

  const raw_ptr<EventDrivenTelemetrySamplerPool> sampler_pool_;

  std::unique_ptr<MetricReportingController> reporting_controller_;

  bool is_reporting_enabled_ = false;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<MetricEventObserverManager> weak_ptr_factory_{this};
};
}  // namespace reporting

#endif  // COMPONENTS_REPORTING_METRICS_METRIC_EVENT_OBSERVER_MANAGER_H_
