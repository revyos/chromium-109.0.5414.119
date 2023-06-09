// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_INTERACTION_INTERACTION_TEST_UTIL_H_
#define UI_BASE_INTERACTION_INTERACTION_TEST_UTIL_H_

#include <memory>

#include "ui/base/interaction/element_tracker.h"

namespace ui::test {

// Platform- and framework-independent utility for delegating specific common
// actions to framework-specific handlers. Use so you can write your
// interaction tests without having to worry about framework specifics.
//
// Example usage:
//
// class MyTest {
//   void SetUp() override {
//     test_util_.AddSimulator(
//         std::make_unique<InteractionTestUtilSimuatorViews>());
// #if BUILDFLAG(IS_MAC)
//     test_util_.AddSimulator(
//         std::make_unique<InteractionTestUtilSimuatorMac>());
// #endif
//     ...
//   }
//   InteractionTestUtil test_util_;
// };
//
// TEST_F(MyTest, TestClickButton) {
//   ...
//   step.SetStartCallback(base::BindLambdaForTesting([&] (
//       InteractionSequence* seq, TrackedElement* element) {
//     test_util_.PressButton(element);
//   }))
//   ...
// }
//
class InteractionTestUtil {
 public:
  // Indicates the type of input we want to apply to an element. Default in most
  // cases is `kDontCare` which will use the most reliable form of input (or may
  // even call code that directly simulates e.g. a button press).
  //
  // Only use values other than `kDontCare` if you REALLY want to test a
  // specific mode of input, as not all inputs will be supported for all
  // frameworks or platforms.
  enum class InputType {
    // Simulate the input in the most reliable way, which could be through
    // sending an input event or calling code that directly simulates the
    // interaction.
    kDontCare,
    // Simulate the input explicitly via mouse events.
    kMouse,
    // Simulate the input explicitly via kayboard events.
    kKeyboard,
    // Simulate the input explicitly via touch events.
    kTouch
  };

  // Provides framework-agnostic ways to send common input to the UI, such as
  // clicking buttons, typing text, etc.
  //
  // Framework-specific implementations will need to be provided to each
  // InteractionTestUtil instance you are using for testing.
  class Simulator {
   public:
    Simulator() = default;
    virtual ~Simulator() = default;
    Simulator(const Simulator&) = delete;
    void operator=(const Simulator&) = delete;

    using InputType = InteractionTestUtil::InputType;

    // Tries to press `element` as if it is a button. Returns false if `element`
    // is an unsupported type or if `input_type` is not supported.
    [[nodiscard]] virtual bool PressButton(TrackedElement* element,
                                           InputType input_type);

    // Tries to select `element` as if it is a menu item. Returns false if
    // `element` is an unsupported type or if `input_type` is not supported.
    [[nodiscard]] virtual bool SelectMenuItem(TrackedElement* element,
                                              InputType input_type);

    // Triggers the default action of the target element, which is typically
    // whatever happens when the user clicks/taps it. If `element` is a button
    // or menu item, prefer PressButton() or SelectMenuItem() instead.
    [[nodiscard]] virtual bool DoDefaultAction(TrackedElement* element,
                                               InputType input_type);

    // Tries to select tab `index` in `tab_collection`. The collection could be
    // a tabbed pane, browser/tabstrip, or similar. Note that `index` is
    // zero-indexed.
    [[nodiscard]] virtual bool SelectTab(TrackedElement* tab_collection,
                                         size_t index,
                                         InputType input_type);

    // Tries to select item `index` in `dropdown`. The collection could be
    // a listbox, combobox, or similar. Note that `index` is zero-indexed.
    [[nodiscard]] virtual bool SelectDropdownItem(TrackedElement* dropdown,
                                                  size_t index,
                                                  InputType input_type);
  };

  InteractionTestUtil();
  virtual ~InteractionTestUtil();
  InteractionTestUtil(const InteractionTestUtil&) = delete;
  void operator=(const InteractionTestUtil&) = delete;

  // Adds an input simulator for a specific framework.
  template <class T>
  T* AddSimulator(std::unique_ptr<T> simulator) {
    T* const result = simulator.get();
    simulators_.emplace_back(std::move(simulator));
    return result;
  }

  // Simulate a button press on `element`. Will fail if `element` is not a
  // button or if `input_type` is not supported.
  void PressButton(TrackedElement* element,
                   InputType input_type = InputType::kDontCare);

  // Simulate the menu item `element` being selected by the user. Will fail if
  // `element` is not a menu item or if `input_type` is not supported.
  void SelectMenuItem(TrackedElement* element,
                      InputType input_type = InputType::kDontCare);

  // Simulate selecting the `index`-th tab (zero-indexed) of `tab_collection`.
  // Will fail if the target object is not a supported type, if `index` is out
  // of bounds, or if `input_type` is not supported.
  void SelectTab(TrackedElement* tab_collection,
                 size_t index,
                 InputType input_type = InputType::kDontCare);

  // Simulate selecting item `index` in `dropdown`. The collection could be
  // a listbox, combobox, or similar. Will fail if the target object is not a
  // supported type, if `index` is out of bounds, or if `input_type` is not
  // supported.
  //
  // Note that if `input_type` is kDontCare, the approach with the broadest
  // possible compatibility will be used, possibly bypassing the dropdown menu
  // associated with the element. This is because dropdown menus vary in
  // implementation across platforms and can be a source of flakiness. Options
  // other than kDontCare may not be supported on all platforms for this reason;
  // if they are not, an error message will be printed and the test will fail.
  void SelectDropdownItem(TrackedElement* dropdown,
                          size_t index,
                          InputType input_type = InputType::kDontCare);

  // Simulate the default action for `element` - typically whatever happens when
  // the user clicks or taps on it. Will fail if `input_type` is not supported.
  // Prefer PressButton() for buttons and SelectMenuItem() for menu items.
  void DoDefaultAction(TrackedElement* element,
                       InputType input_type = InputType::kDontCare);

 private:
  // The list of known simulators.
  std::vector<std::unique_ptr<Simulator>> simulators_;
};

}  // namespace ui::test

#endif  // UI_BASE_INTERACTION_INTERACTION_TEST_UTIL_H_
