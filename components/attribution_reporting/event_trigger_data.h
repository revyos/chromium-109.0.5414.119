// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ATTRIBUTION_REPORTING_EVENT_TRIGGER_DATA_H_
#define COMPONENTS_ATTRIBUTION_REPORTING_EVENT_TRIGGER_DATA_H_

#include <stdint.h>

#include "base/component_export.h"
#include "components/attribution_reporting/filters.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace attribution_reporting {

struct COMPONENT_EXPORT(ATTRIBUTION_REPORTING) EventTriggerData {
  // Data associated with trigger.
  // Will be sanitized to a lower entropy by the `AttributionStorageDelegate`
  // before storage.
  uint64_t data;

  // Priority specified in conversion redirect. Used to prioritize which
  // reports to send among multiple different reports for the same attribution
  // source. Defaults to 0 if not provided.
  int64_t priority;

  // Key specified in conversion redirect for deduplication against existing
  // conversions with the same source. If absent, no deduplication is
  // performed.
  absl::optional<uint64_t> dedup_key;

  // The filters used to determine whether this `EventTriggerData'`s fields
  // are used.
  Filters filters;

  // The negated filters used to determine whether this `EventTriggerData'`s
  // fields are used.
  Filters not_filters;

  EventTriggerData(uint64_t data,
                   int64_t priority,
                   absl::optional<uint64_t> dedup_key,
                   Filters filters,
                   Filters not_filters);
};

}  // namespace attribution_reporting

#endif  // COMPONENTS_ATTRIBUTION_REPORTING_EVENT_TRIGGER_DATA_H_