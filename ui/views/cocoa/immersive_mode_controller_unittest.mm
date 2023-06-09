// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "components/remote_cocoa/app_shim/immersive_mode_controller.h"

#import <Cocoa/Cocoa.h>

#include <memory>
#include "base/bind.h"
#include "base/callback_helpers.h"
#import "base/mac/scoped_nsobject.h"
#include "components/remote_cocoa/app_shim/bridged_content_view.h"
#include "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#import "ui/base/cocoa/window_size_constants.h"
#import "ui/base/test/cocoa_helper.h"

namespace {
const double kBrowserHeight = 200;
const double kBrowserWidth = 400;
const double kOverlayViewHeight = 100;
const double kOverlayViewWidth = kBrowserWidth;
const double kTitlebarHeight = 28;
}

namespace remote_cocoa {

class CocoaImmersiveModeControllerTest : public ui::CocoaTest {
 public:
  CocoaImmersiveModeControllerTest() = default;

  CocoaImmersiveModeControllerTest(const CocoaImmersiveModeControllerTest&) =
      delete;
  CocoaImmersiveModeControllerTest& operator=(
      const CocoaImmersiveModeControllerTest&) = delete;

  void SetUp() override {
    ui::CocoaTest::SetUp();

    // Create a blank browser window.
    browser_.reset([[NSWindow alloc]
        initWithContentRect:ui::kWindowSizeDeterminedLater
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO]);
    [browser_ setFrame:NSMakeRect(0, 0, kBrowserWidth, kBrowserHeight)
               display:YES];
    [browser_ orderBack:nil];
    browser_.get().releasedWhenClosed = NO;

    // Create a blank overlay window.
    overlay_.reset([[NativeWidgetMacNSWindow alloc]
        initWithContentRect:ui::kWindowSizeDeterminedLater
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO]);
    overlay_.get().releasedWhenClosed = NO;
    [overlay_ setFrame:NSMakeRect(0, 0, kBrowserWidth, kOverlayViewHeight)
               display:YES];
    overlay_.get().contentView =
        [[[BridgedContentView alloc] initWithBridge:nullptr
                                             bounds:gfx::Rect()] autorelease];
    [overlay_.get().contentView
        setFrame:NSMakeRect(0, 0, kBrowserWidth, kOverlayViewHeight)];

    [browser_ addChildWindow:overlay_ ordered:NSWindowAbove];
    EXPECT_EQ(overlay_.get().isVisible, YES);
  }

  void TearDown() override {
    EXPECT_EQ(browser_.get().titlebarAccessoryViewControllers.count, 0u);

    [overlay_ close];
    overlay_.reset();
    [browser_ close];
    browser_.reset();

    ui::CocoaTest::TearDown();
  }

  NSWindow* browser() { return browser_; }
  NSWindow* overlay() { return overlay_; }

 private:
  base::scoped_nsobject<NSWindow> browser_;
  base::scoped_nsobject<NSWindow> overlay_;
};

// Test ImmersiveModeController construction and destruction.
TEST_F(CocoaImmersiveModeControllerTest, ImmersiveModeController) {
  bool view_will_appear_ran = false;
  // Controller under test.
  auto immersive_mode_controller = std::make_unique<ImmersiveModeController>(
      browser(), overlay(),
      base::BindOnce(
          [](bool* view_will_appear_ran) { *view_will_appear_ran = true; },
          &view_will_appear_ran));
  immersive_mode_controller->Enable();
  EXPECT_TRUE(view_will_appear_ran);
  EXPECT_EQ(browser().titlebarAccessoryViewControllers.count, 1u);
}

// Test that child windows in immersive mode properly balance the revealed lock
// count.
TEST_F(CocoaImmersiveModeControllerTest, ChildWindowRevealLock) {
  // Controller under test.
  auto immersive_mode_controller = std::make_unique<ImmersiveModeController>(
      browser(), overlay(), base::DoNothing());
  immersive_mode_controller->Enable();

  // Create a popup.
  CocoaTestHelperWindow* popup = [[CocoaTestHelperWindow alloc] init];
  EXPECT_EQ(popup.isVisible, NO);
  EXPECT_EQ(immersive_mode_controller->revealed_lock_count(), 0);

  // Add the popup as a child of overlay.
  [overlay() addChildWindow:popup ordered:NSWindowAbove];
  EXPECT_EQ(popup.isVisible, YES);
  EXPECT_EQ(immersive_mode_controller->revealed_lock_count(), 1);

  // Make sure that closing the popup results in the reveal lock count
  // decrementing.
  [popup close];
  EXPECT_EQ(immersive_mode_controller->revealed_lock_count(), 0);
}

// Test that child windows in immersive mode properly balance the revealed lock
// count.
TEST_F(CocoaImmersiveModeControllerTest,
       HiddenTitleBarAccessoryViewController) {
  // Controller under test.
  auto immersive_mode_controller = std::make_unique<ImmersiveModeController>(
      browser(), overlay(), base::DoNothing());
  immersive_mode_controller->Enable();

  EXPECT_EQ(immersive_mode_controller->revealed_lock_count(), 0);
  immersive_mode_controller->RevealLock();
  immersive_mode_controller->RevealLock();
  immersive_mode_controller->RevealLock();
  EXPECT_EQ(immersive_mode_controller->revealed_lock_count(), 3);

  // One controller for Top Chrome and another hidden controller that pins the
  // Title Bar.
  EXPECT_EQ(browser().titlebarAccessoryViewControllers.count, 2u);

  // Ensure the clear controller's view covers the browser view.
  NSTitlebarAccessoryViewController* clear_controller =
      browser().titlebarAccessoryViewControllers.lastObject;
  EXPECT_EQ(clear_controller.view.frame.size.height,
            browser().contentView.frame.size.height);
  EXPECT_EQ(clear_controller.view.frame.size.width,
            browser().contentView.frame.size.width);

  // There is still an outstanding lock, make sure we still have the hidden
  // controller.
  immersive_mode_controller->RevealUnlock();
  immersive_mode_controller->RevealUnlock();
  EXPECT_EQ(immersive_mode_controller->revealed_lock_count(), 1);
  EXPECT_EQ(browser().titlebarAccessoryViewControllers.count, 2u);

  immersive_mode_controller->RevealUnlock();
  EXPECT_EQ(immersive_mode_controller->revealed_lock_count(), 0);
  EXPECT_EQ(browser().titlebarAccessoryViewControllers.count, 1u);
}

// Test ImmersiveModeController construction and destruction.
TEST_F(CocoaImmersiveModeControllerTest, TitlebarObserver) {
  // Create a fake NSToolbarFullScreenWindow and associated views.
  base::scoped_nsobject<NSView> titlebar_container_view([[NSView alloc]
      initWithFrame:NSMakeRect(0, kOverlayViewHeight, kOverlayViewWidth,
                               kOverlayViewHeight)]);
  base::scoped_nsobject<NSView> overlay_view([[NSView alloc]
      initWithFrame:NSMakeRect(0, 0, kOverlayViewWidth, kOverlayViewHeight)]);
  [titlebar_container_view addSubview:overlay_view];
  base::scoped_nsobject<NSWindow> fullscreen_window([[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, kOverlayViewWidth, kBrowserHeight)
                styleMask:NSWindowStyleMaskBorderless
                  backing:NSBackingStoreBuffered
                    defer:NO]);
  fullscreen_window.get().releasedWhenClosed = NO;
  [fullscreen_window.get().contentView addSubview:titlebar_container_view];
  [fullscreen_window orderBack:nil];

  // Create a titlebar observer. This is the class under test.
  base::scoped_nsobject<ImmersiveModeTitlebarObserver> titlebar_observer(
      [[ImmersiveModeTitlebarObserver alloc]
          initWithOverlayWindow:overlay()
                    overlayView:overlay_view]);

  // Observer the fake fake titlebar container view.
  [titlebar_container_view addObserver:titlebar_observer
                            forKeyPath:@"frame"
                               options:NSKeyValueObservingOptionInitial |
                                       NSKeyValueObservingOptionNew
                               context:nullptr];

  // Make sure that the overlay view moves along the y axis as the titlebar
  // container view moves. This simulates the titlebar reveal when top chrome is
  // always visible. Down.
  for (int i = 0; i < kTitlebarHeight + 1; ++i) {
    [titlebar_container_view
        setFrame:NSMakeRect(0, kOverlayViewHeight - i, kOverlayViewWidth,
                            kOverlayViewHeight)];
    EXPECT_EQ(overlay().frame.origin.y, 100 - i);
  }

  // And back up.
  for (int i = 1; i <= kTitlebarHeight; ++i) {
    [titlebar_container_view
        setFrame:NSMakeRect(0, (kOverlayViewHeight - kTitlebarHeight) + i,
                            kOverlayViewWidth, kOverlayViewHeight)];
    EXPECT_EQ(overlay().frame.origin.y,
              (kOverlayViewHeight - kTitlebarHeight) + i);
  }

  // Clip the overlay view and make sure the overlay window moves off screen.
  // This simulates top chrome auto hiding.
  [titlebar_container_view
      setFrame:NSMakeRect(0, kOverlayViewHeight + 1, kOverlayViewWidth,
                          kOverlayViewHeight)];
  EXPECT_EQ(overlay().frame.origin.y, -kOverlayViewHeight);

  // Remove the clip and make sure the overlay window moves back.
  [titlebar_container_view
      setFrame:NSMakeRect(0, kOverlayViewHeight, kOverlayViewWidth,
                          kOverlayViewHeight)];
  EXPECT_EQ(overlay().frame.origin.y, kOverlayViewHeight);

  [titlebar_container_view removeObserver:titlebar_observer
                               forKeyPath:@"frame"];

  [fullscreen_window close];
  fullscreen_window.reset();
}

}  // namespace remote_cocoa
