// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_NOTIFICATION_CENTER_NOTIFICATION_CENTER_BUBBLE_H_
#define ASH_SYSTEM_NOTIFICATION_CENTER_NOTIFICATION_CENTER_BUBBLE_H_

#include "ash/system/tray/tray_bubble_wrapper.h"

namespace views {
class Widget;
}  // namespace views

namespace ash {

class NotificationCenterTray;
class NotificationCenterView;
class TrayBubbleView;

// Manages the bubble that contains NotificationCenterView.
// Shows the bubble on `ShowBubble()`, and closes the bubble on the destructor.
class NotificationCenterBubble {
 public:
  explicit NotificationCenterBubble(
      NotificationCenterTray* notification_center_tray);

  NotificationCenterBubble(const NotificationCenterBubble&) = delete;
  NotificationCenterBubble& operator=(const NotificationCenterBubble&) = delete;

  ~NotificationCenterBubble();

  TrayBubbleView* GetBubbleView();
  views::Widget* GetBubbleWidget();

 private:
  friend class NotificationCenterTestApi;

  // The owner of this class.
  NotificationCenterTray* const notification_center_tray_;

  // The main view responsible for showing all notification content in this
  // bubble. Owned by `TrayBubbleView`.
  NotificationCenterView* notification_center_view_ = nullptr;

  std::unique_ptr<TrayBubbleWrapper> bubble_wrapper_;
};

}  // namespace ash

#endif  // ASH_SYSTEM_NOTIFICATION_CENTER_NOTIFICATION_CENTER_BUBBLE_H_
