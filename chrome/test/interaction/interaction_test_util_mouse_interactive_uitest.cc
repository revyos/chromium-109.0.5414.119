// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/interaction/interaction_test_util_mouse.h"

#include "base/test/bind.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/interaction/expect_call_in_scope.h"
#include "ui/base/interaction/interaction_sequence.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/view_utils.h"

namespace {
DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kGestureCompleteEvent);
}

class InteractionTestUtilMouseUiTest : public InProcessBrowserTest {
 public:
  InteractionTestUtilMouseUiTest() = default;
  ~InteractionTestUtilMouseUiTest() override = default;

  using Mouse = views::test::InteractionTestUtilMouse;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    mouse_ = std::make_unique<Mouse>(
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget());
  }

  void TearDownOnMainThread() override {
    mouse_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  std::unique_ptr<Mouse> mouse_;
};

IN_PROC_BROWSER_TEST_F(InteractionTestUtilMouseUiTest, MoveAndClick) {
  UNCALLED_MOCK_CALLBACK(ui::InteractionSequence::AbortedCallback, aborted);
  UNCALLED_MOCK_CALLBACK(ui::InteractionSequence::CompletedCallback, completed);

  // This will receive the result of the gesture success or failure.
  bool gesture_result = false;

  auto sequence =
      ui::InteractionSequence::Builder()
          .SetContext(browser()->window()->GetElementContext())
          .SetAbortedCallback(aborted.Get())
          .SetCompletedCallback(completed.Get())
          // Find the app menu button.
          .AddStep(
              ui::InteractionSequence::StepBuilder()
                  .SetElementID(kAppMenuButtonElementId)
                  .SetStartCallback(
                      base::BindLambdaForTesting([&](ui::TrackedElement* el) {
                        auto* const view =
                            el->AsA<views::TrackedElementViews>()->view();
                        const gfx::Point pos =
                            view->GetBoundsInScreen().CenterPoint();
                        // Perform the following gesture:
                        // - move to the center point of the app menu button
                        // - click the left mouse button
                        // When this gesture is complete, record the result, and
                        // send an event on the menu button.
                        mouse_->PerformGestures(
                            base::BindOnce(
                                [](bool* result_out, ui::TrackedElement* el,
                                   bool result) {
                                  *result_out = result;
                                  ui::ElementTracker::GetFrameworkDelegate()
                                      ->NotifyCustomEvent(
                                          el, kGestureCompleteEvent);
                                },
                                &gesture_result, el),
                            Mouse::MoveTo(pos),
                            Mouse::Click(ui_controls::LEFT));
                      })))
          // When the gesture complete event comes in, check that the gesture
          // succeeded.
          .AddStep(
              ui::InteractionSequence::StepBuilder()
                  .SetElementID(kAppMenuButtonElementId)
                  .SetType(ui::InteractionSequence::StepType::kCustomEvent,
                           kGestureCompleteEvent)
                  .SetStartCallback(base::BindLambdaForTesting(
                      [&](ui::InteractionSequence* seq, ui::TrackedElement*) {
                        if (!gesture_result)
                          seq->FailForTesting();
                      })))
          // Verify that the click opened the app menu, which should contain a
          // known menu item.
          .AddStep(ui::InteractionSequence::StepBuilder().SetElementID(
              AppMenuModel::kMoreToolsMenuItem))
          .Build();

  EXPECT_CALL_IN_SCOPE(completed, Run, sequence->RunSynchronouslyForTesting());
}

IN_PROC_BROWSER_TEST_F(InteractionTestUtilMouseUiTest, GestureAborted) {
  UNCALLED_MOCK_CALLBACK(ui::InteractionSequence::AbortedCallback, aborted);
  UNCALLED_MOCK_CALLBACK(ui::InteractionSequence::CompletedCallback, completed);

  // This will receive the result of the gesture success or failure.
  bool gesture_result = false;

  auto sequence =
      ui::InteractionSequence::Builder()
          .SetContext(browser()->window()->GetElementContext())
          .SetAbortedCallback(aborted.Get())
          .SetCompletedCallback(completed.Get())
          // Find the app menu button.
          .AddStep(
              ui::InteractionSequence::StepBuilder()
                  .SetElementID(kAppMenuButtonElementId)
                  .SetStartCallback(
                      base::BindLambdaForTesting([&](ui::TrackedElement* el) {
                        auto* const view =
                            el->AsA<views::TrackedElementViews>()->view();
                        const gfx::Point pos =
                            view->GetBoundsInScreen().CenterPoint();
                        // Perform the following gesture:
                        // - move to the center point of the app menu button
                        // - click the left mouse button
                        // When this gesture is complete, record the result, and
                        // send an event on the menu button.
                        mouse_->PerformGestures(
                            base::BindOnce(
                                [](bool* result_out, ui::TrackedElement* el,
                                   bool result) {
                                  *result_out = result;
                                  ui::ElementTracker::GetFrameworkDelegate()
                                      ->NotifyCustomEvent(
                                          el, kGestureCompleteEvent);
                                },
                                &gesture_result, el),
                            Mouse::MoveTo(pos),
                            Mouse::Click(ui_controls::LEFT));
                        mouse_->CancelAllGestures();
                      })))
          // When the gesture complete event comes in, check that the gesture
          // failed.
          .AddStep(
              ui::InteractionSequence::StepBuilder()
                  .SetElementID(kAppMenuButtonElementId)
                  .SetType(ui::InteractionSequence::StepType::kCustomEvent,
                           kGestureCompleteEvent)
                  .SetStartCallback(base::BindLambdaForTesting(
                      [&](ui::InteractionSequence* seq, ui::TrackedElement*) {
                        if (gesture_result)
                          seq->FailForTesting();
                      })))
          .Build();

  EXPECT_CALL_IN_SCOPE(completed, Run, sequence->RunSynchronouslyForTesting());
}

IN_PROC_BROWSER_TEST_F(InteractionTestUtilMouseUiTest, Drag) {
  UNCALLED_MOCK_CALLBACK(ui::InteractionSequence::AbortedCallback, aborted);
  UNCALLED_MOCK_CALLBACK(ui::InteractionSequence::CompletedCallback, completed);

  // This will receive the result of the gesture success or failure.
  bool gesture_result = false;

  const GURL first_url =
      browser()->tab_strip_model()->GetWebContentsAt(0)->GetURL();
  const GURL kSecondUrl("chrome://version");
  ASSERT_TRUE(AddTabAtIndex(-1, kSecondUrl, ui::PAGE_TRANSITION_LINK));

  auto sequence =
      ui::InteractionSequence::Builder()
          .SetContext(browser()->window()->GetElementContext())
          .SetAbortedCallback(aborted.Get())
          .SetCompletedCallback(completed.Get())
          // Find the tab strip.
          .AddStep(
              ui::InteractionSequence::StepBuilder()
                  .SetElementID(kTabStripElementId)
                  .SetStartCallback(
                      base::BindLambdaForTesting([&](ui::TrackedElement* el) {
                        auto* const tab_strip = views::AsViewClass<TabStrip>(
                            el->AsA<views::TrackedElementViews>()->view());
                        // The second tab might still be animating in, which
                        // could cause weirdness if we try to drag.
                        tab_strip->StopAnimating(/* layout =*/true);

                        const gfx::Point start = tab_strip->tab_at(0)
                                                     ->GetBoundsInScreen()
                                                     .CenterPoint();
                        const gfx::Point end = tab_strip->tab_at(1)
                                                   ->GetBoundsInScreen()
                                                   .CenterPoint();
                        // Drag the first tab into the second spot.
                        mouse_->PerformGestures(
                            base::BindOnce(
                                [](bool* result_out, ui::TrackedElement* el,
                                   bool result) {
                                  *result_out = result;
                                  ui::ElementTracker::GetFrameworkDelegate()
                                      ->NotifyCustomEvent(
                                          el, kGestureCompleteEvent);
                                },
                                &gesture_result, el),
                            Mouse::MoveTo(start), Mouse::DragAndRelease(end));
                      })))
          // When the gesture complete event comes in, check that the gesture
          // succeeded.
          .AddStep(
              ui::InteractionSequence::StepBuilder()
                  .SetElementID(kTabStripElementId)
                  .SetType(ui::InteractionSequence::StepType::kCustomEvent,
                           kGestureCompleteEvent)
                  .SetStartCallback(base::BindLambdaForTesting(
                      [&](ui::InteractionSequence* seq,
                          ui::TrackedElement* el) {
                        if (!gesture_result)
                          seq->FailForTesting();
                        // Stop any remaining animations, and verify that the
                        // tab was moved.
                        auto* const tab_strip = views::AsViewClass<TabStrip>(
                            el->AsA<views::TrackedElementViews>()->view());
                        tab_strip->StopAnimating(/* layout =*/true);

                        EXPECT_EQ(kSecondUrl, browser()
                                                  ->tab_strip_model()
                                                  ->GetWebContentsAt(0)
                                                  ->GetURL());
                        EXPECT_EQ(first_url, browser()
                                                 ->tab_strip_model()
                                                 ->GetWebContentsAt(1)
                                                 ->GetURL());
                        // Clean up any drag gestures that have not yet properly
                        // cleaned up (this is a platform implementation issue
                        // for drag event handling).
                        mouse_->CancelAllGestures();
                      })))
          .Build();

  EXPECT_CALL_IN_SCOPE(completed, Run, sequence->RunSynchronouslyForTesting());
}
