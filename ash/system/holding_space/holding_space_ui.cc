// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/holding_space/holding_space_ui.h"

#include "ash/bubble/bubble_utils.h"
#include "ash/constants/ash_features.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash::holding_space_ui {

views::Builder<views::Label> CreateTopLevelBubbleHeaderLabel(int message_id) {
  return views::Builder<views::Label>(
      bubble_utils::CreateLabel(bubble_utils::TypographyStyle::kTitle1,
                                l10n_util::GetStringUTF16(message_id)));
}

views::Builder<views::Label> CreateSectionHeaderLabel(int message_id) {
  return views::Builder<views::Label>(
      bubble_utils::CreateLabel(features::IsHoldingSpaceRefreshEnabled()
                                    ? bubble_utils::TypographyStyle::kButton1
                                    : bubble_utils::TypographyStyle::kTitle1,
                                l10n_util::GetStringUTF16(message_id)));
}

views::Builder<views::Label> CreateSuggestionsSectionHeaderLabel(
    int message_id) {
  return views::Builder<views::Label>(bubble_utils::CreateLabel(
      bubble_utils::TypographyStyle::kButton2,
      l10n_util::GetStringUTF16(message_id),
      AshColorProvider::ContentLayerType::kTextColorSecondary));
}

views::Builder<views::Label> CreateBubblePlaceholderLabel(int message_id) {
  return views::Builder<views::Label>(bubble_utils::CreateLabel(
      bubble_utils::TypographyStyle::kTitle1,
      l10n_util::GetStringUTF16(message_id),
      AshColorProvider::ContentLayerType::kTextColorSecondary));
}

views::Builder<views::Label> CreateSectionPlaceholderLabel(
    const std::u16string& text) {
  return views::Builder<views::Label>(bubble_utils::CreateLabel(
      bubble_utils::TypographyStyle::kBody1, text,
      features::IsHoldingSpaceSuggestionsEnabled()
          ? AshColorProvider::ContentLayerType::kTextColorSecondary
          : AshColorProvider::ContentLayerType::kTextColorPrimary));
}

}  // namespace ash::holding_space_ui
