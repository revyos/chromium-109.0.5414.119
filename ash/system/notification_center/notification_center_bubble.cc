// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/notification_center/notification_center_bubble.h"

#include <memory>

#include "ash/public/cpp/shelf_types.h"
#include "ash/shelf/shelf.h"
#include "ash/system/notification_center/notification_center_tray.h"
#include "ash/system/notification_center/notification_center_view.h"
#include "ash/system/tray/tray_constants.h"
#include "ash/system/tray/tray_utils.h"
#include "ui/views/widget/widget.h"

namespace ash {

NotificationCenterBubble::NotificationCenterBubble(
    NotificationCenterTray* notification_center_tray)
    : notification_center_tray_(notification_center_tray) {
  TrayBubbleView::InitParams init_params;
  init_params.delegate = notification_center_tray_->GetWeakPtr();
  init_params.parent_window =
      notification_center_tray_->GetBubbleWindowContainer();
  init_params.anchor_view = nullptr;
  init_params.anchor_mode = TrayBubbleView::AnchorMode::kRect;
  init_params.anchor_rect =
      notification_center_tray_->shelf()->GetSystemTrayAnchorRect();
  init_params.insets = GetTrayBubbleInsets();
  init_params.shelf_alignment = notification_center_tray_->shelf()->alignment();
  init_params.preferred_width = kTrayMenuWidth;
  init_params.close_on_deactivate = true;
  init_params.reroute_event_handler = true;
  init_params.translucent = true;

  // Create and customize bubble view.
  TrayBubbleView* bubble_view = new TrayBubbleView(init_params);

  notification_center_view_ =
      bubble_view->AddChildView(std::make_unique<NotificationCenterView>());
  notification_center_view_->Init();

  // Show the bubble.
  bubble_wrapper_ = std::make_unique<TrayBubbleWrapper>(
      notification_center_tray_, bubble_view);
}

NotificationCenterBubble::~NotificationCenterBubble() {
  bubble_wrapper_->bubble_view()->ResetDelegate();
}

TrayBubbleView* NotificationCenterBubble::GetBubbleView() {
  return bubble_wrapper_->bubble_view();
}

views::Widget* NotificationCenterBubble::GetBubbleWidget() {
  return bubble_wrapper_->GetBubbleWidget();
}

}  // namespace ash
