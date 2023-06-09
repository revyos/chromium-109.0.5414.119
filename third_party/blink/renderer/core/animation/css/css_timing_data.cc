// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/animation/css/css_timing_data.h"

namespace blink {

CSSTimingData::CSSTimingData() {
  delay_list_.push_back(InitialDelay());
  delay_start_list_.push_back(InitialDelayStart());
  delay_end_list_.push_back(InitialDelayEnd());
  duration_list_.push_back(InitialDuration());
  timing_function_list_.push_back(InitialTimingFunction());
}

CSSTimingData::CSSTimingData(const CSSTimingData& other) = default;

Timing CSSTimingData::ConvertToTiming(size_t index) const {
  Timing timing;
  timing.start_delay = Timing::Delay(
      ANIMATION_TIME_DELTA_FROM_SECONDS(GetRepeated(delay_list_, index)));
  double duration = GetRepeated(duration_list_, index);
  timing.iteration_duration =
      std::isnan(duration)
          ? absl::nullopt
          : absl::make_optional(ANIMATION_TIME_DELTA_FROM_SECONDS(duration));
  timing.timing_function = GetRepeated(timing_function_list_, index);
  timing.AssertValid();
  return timing;
}

bool CSSTimingData::TimingMatchForStyleRecalc(
    const CSSTimingData& other) const {
  if (delay_list_ != other.delay_list_)
    return false;
  if (delay_start_list_ != other.delay_start_list_)
    return false;
  if (delay_end_list_ != other.delay_end_list_)
    return false;
  if (duration_list_ != other.duration_list_)
    return false;
  if (timing_function_list_.size() != other.timing_function_list_.size())
    return false;

  for (wtf_size_t i = 0; i < timing_function_list_.size(); i++) {
    if (!ValuesEquivalent(timing_function_list_.at(i),
                          other.timing_function_list_.at(i))) {
      return false;
    }
  }
  return true;
}

}  // namespace blink
