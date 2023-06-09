// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_button_view.h"

#include "chrome/browser/ui/views/side_panel/read_anything/read_anything_constants.h"
#include "ui/base/models/image_model.h"
#include "ui/views/layout/box_layout.h"

ReadAnythingButtonView::ReadAnythingButtonView(
    views::ImageButton::PressedCallback callback,
    const gfx::ImageSkia& icon,
    const std::u16string& tooltip) {
  // Create and set a BoxLayout with insets to hold the button.
  auto button_layout_manager = std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal);
  button_layout_manager->set_inside_border_insets(
      gfx::Insets().set_left(kButtonPadding).set_right(kButtonPadding));
  button_layout_manager->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  button_layout_manager->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  SetLayoutManager(std::move(button_layout_manager));

  // Create the image button.
  auto button = std::make_unique<views::ImageButton>(std::move(callback));
  button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  button->SetImageModel(views::Button::STATE_NORMAL,
                        ui::ImageModel::FromImageSkia(icon));
  button->SetTooltipText(tooltip);

  // Add the button to the view.
  button_ = AddChildView(std::move(button));
}

void ReadAnythingButtonView::UpdateIcon(const gfx::ImageSkia& icon) {
  button_->SetImage(views::Button::STATE_NORMAL, icon);
}

ReadAnythingButtonView::~ReadAnythingButtonView() = default;
