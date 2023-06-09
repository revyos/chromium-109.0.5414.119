// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/capture_mode/capture_mode_menu_toggle_button.h"

#include "ash/capture_mode/capture_mode_constants.h"
#include "ash/capture_mode/capture_mode_util.h"
#include "ash/style/ash_color_id.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace ash {

namespace {

constexpr gfx::Size kToggleButtonSize{40, 20};

}  // namespace

CaptureModeMenuToggleButton::CaptureModeMenuToggleButton(
    const gfx::VectorIcon& icon,
    const std::u16string& label_text,
    bool enabled,
    views::ToggleButton::PressedCallback callback)
    : icon_view_(AddChildView(std::make_unique<views::ImageView>())),
      label_view_(AddChildView(std::make_unique<views::Label>(label_text))),
      toggle_button_(AddChildView(
          std::make_unique<views::ToggleButton>(std::move(callback)))) {
  toggle_button_->SetAccessibleName(label_text);
  icon_view_->SetImageSize(capture_mode::kSettingsIconSize);
  icon_view_->SetPreferredSize(capture_mode::kSettingsIconSize);
  icon_view_->SetImage(
      ui::ImageModel::FromVectorIcon(icon, kColorAshButtonIconColor));
  toggle_button_->SetPreferredSize(kToggleButtonSize);
  toggle_button_->SetIsOn(enabled);

  SetBorder(views::CreateEmptyBorder(capture_mode::kSettingsMenuBorderSize));
  capture_mode_util::ConfigLabelView(label_view_);
  auto* box_layout = capture_mode_util::CreateAndInitBoxLayoutForView(this);
  box_layout->SetFlexForView(label_view_, 1);
}

CaptureModeMenuToggleButton::~CaptureModeMenuToggleButton() = default;

views::View* CaptureModeMenuToggleButton::GetView() {
  return this;
}

BEGIN_METADATA(CaptureModeMenuToggleButton, views::View)
END_METADATA

}  // namespace ash