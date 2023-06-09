// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_INTERACTION_INTERACTIVE_TEST_INTERNAL_H_
#define UI_BASE_INTERACTION_INTERACTIVE_TEST_INTERNAL_H_

#include <memory>

#include "base/logging.h"
#include "base/strings/string_piece_forward.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/interaction/interaction_sequence.h"
#include "ui/base/interaction/interaction_test_util.h"

namespace ui::test {

class InteractiveTestApi;

namespace internal {

// Element that is present during interactive tests that actions can bounce
// events off of.
DECLARE_ELEMENT_IDENTIFIER_VALUE(kInteractiveTestPivotElementId);

// Class that implements functionality for InteractiveTest* that should be
// hidden from tests that inherit the API.
class InteractiveTestPrivate {
 public:
  explicit InteractiveTestPrivate(
      std::unique_ptr<InteractionTestUtil> test_util);
  virtual ~InteractiveTestPrivate();
  InteractiveTestPrivate(const InteractiveTestPrivate&) = delete;
  void operator=(const InteractiveTestPrivate&) = delete;

  InteractionTestUtil& test_util() { return *test_util_; }
  TrackedElement* pivot_element() { return pivot_element_.get(); }

  // Call this method during test SetUp(), or SetUpOnMainThread() for browser
  // tests.
  virtual void DoTestSetUp();

  // Call this method during test TearDown(), or TearDownOnMainThread() for
  // browser tests.
  virtual void DoTestTearDown();

  // Called when the sequence ends, but before we break out of the run loop
  // in RunTestSequenceImpl().
  virtual void OnSequenceComplete();
  virtual void OnSequenceAborted(
      int active_step,
      TrackedElement* last_element,
      ElementIdentifier last_id,
      InteractionSequence::StepType last_step_type,
      InteractionSequence::AbortedReason aborted_reason);

  // Sets a callback that is called if the test sequence fails instead of
  // failing the current test. Should only be called in tests that are testing
  // InteractiveTestApi or descendant classes.
  void set_aborted_callback_for_testing(
      InteractionSequence::AbortedCallback aborted_callback_for_testing) {
    aborted_callback_for_testing_ = std::move(aborted_callback_for_testing);
  }

 private:
  friend class ui::test::InteractiveTestApi;

  // Tracks whether a sequence succeeded or failed.
  bool success_ = false;

  // Used to simulate input to UI elements.
  std::unique_ptr<InteractionTestUtil> test_util_;

  // Always present during a test sequence; used to relay events to trigger
  // follow-up steps.
  std::unique_ptr<TrackedElement> pivot_element_;

  // Overrides the default test failure behavior to test the API itself.
  InteractionSequence::AbortedCallback aborted_callback_for_testing_;
};

// Specifies an element either by ID or by name.
using ElementSpecifier = absl::variant<ElementIdentifier, base::StringPiece>;

// Applies `matcher` to `value` and returns the result; on failure a useful
// error message is printed using `test_name`, `value`, and `matcher`.
//
// Steps which use this method will fail if it returns false, printing out the
// details of the step in the usual way.
template <typename T>
bool MatchAndExplain(const base::StringPiece& test_name,
                     testing::Matcher<T>& matcher,
                     T&& value) {
  if (matcher.Matches(value))
    return true;
  std::ostringstream oss;
  oss << test_name << " failed.\nExpected: ";
  matcher.DescribeTo(&oss);
  oss << "\nActual: " << testing::PrintToString(value);
  LOG(ERROR) << oss.str();
  return false;
}

// Converts an ElementSpecifier to an element ID or name and sets it onto
// `builder`.
void SpecifyElement(ui::InteractionSequence::StepBuilder& builder,
                    ElementSpecifier element);

}  // namespace internal

}  // namespace ui::test

#endif  // UI_BASE_INTERACTION_INTERACTIVE_TEST_INTERNAL_H_
