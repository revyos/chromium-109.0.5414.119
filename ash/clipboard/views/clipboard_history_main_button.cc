// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/clipboard/views/clipboard_history_main_button.h"

#include "ash/clipboard/clipboard_history_util.h"
#include "ash/clipboard/views/clipboard_history_item_view.h"
#include "ash/constants/ash_features.h"
#include "ash/public/cpp/style/scoped_light_mode_as_default.h"
#include "ash/style/ash_color_id.h"
#include "ash/style/style_util.h"
#include "base/bind.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/focus_ring.h"

namespace ash {

ClipboardHistoryMainButton::ClipboardHistoryMainButton(
    ClipboardHistoryItemView* container)
    : Button(base::BindRepeating(
          [](ClipboardHistoryItemView* item, const ui::Event& event) {
            item->HandleMainButtonPressEvent(event);
          },
          base::Unretained(container))),
      container_(container) {
  SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
  SetID(clipboard_history_util::kMainButtonViewID);

  // Let the parent handle accessibility features.
  GetViewAccessibility().OverrideIsIgnored(/*value=*/true);

  // TODO(crbug.com/1205227): Revisit if this comment makes sense still. It was
  // attached to CreateInkDrop() but sounds more about talking about a null
  // CreateInkDropHighlight(), but at the time of writing the class inherited
  // from Button and had no other InkDrop-related override than CreateInkDrop().
  // This may need an upstream fix in InkDrop.
  //
  // We do not use the ripple highlight due to the following reasons:
  // (1) Events may be intercepted by the menu controller. As a result, the
  // ripple highlight may not update properly.
  // (2) The animation to fade in/out highlight does not look good when the menu
  // selection is advanced by the up/down arrow key.
  // Hence, highlighted background is implemented by customizing in
  // `PaintButtonContents()`.
  views::InkDrop::UseInkDropForFloodFillRipple(
      views::InkDrop::Get(this), /*highlight_on_hover=*/false,
      /*highlight_on_focus=*/!views::FocusRing::Get(this));
}

ClipboardHistoryMainButton::~ClipboardHistoryMainButton() = default;

void ClipboardHistoryMainButton::OnHostPseudoFocusUpdated() {
  SetShouldHighlight(container_->ShouldHighlight());
}

void ClipboardHistoryMainButton::SetShouldHighlight(bool should_highlight) {
  if (should_highlight_ == should_highlight)
    return;

  should_highlight_ = should_highlight;
  SchedulePaint();
}

const char* ClipboardHistoryMainButton::GetClassName() const {
  return "ClipboardHistoryMainButton";
}

void ClipboardHistoryMainButton::OnClickCanceled(const ui::Event& event) {
  DCHECK(event.IsMouseEvent());

  container_->OnMouseClickOnDescendantCanceled();
  views::Button::OnClickCanceled(event);
}

void ClipboardHistoryMainButton::OnThemeChanged() {
  views::Button::OnThemeChanged();

  // Use the light mode as default because the light mode is the default mode
  // of the native theme which decides the context menu's background color.
  // TODO(andrewxu): remove this line after https://crbug.com/1143009 is
  // fixed.
  ScopedLightModeAsDefault scoped_light_mode_as_default;
  StyleUtil::ConfigureInkDropAttributes(
      this, StyleUtil::kBaseColor | StyleUtil::kInkDropOpacity);
}

void ClipboardHistoryMainButton::OnGestureEvent(ui::GestureEvent* event) {
  // Give `container_` a chance to handle `event`.
  container_->MaybeHandleGestureEventFromMainButton(event);
  if (event->handled())
    return;

  views::Button::OnGestureEvent(event);

  // Prevent the menu controller from handling gesture events. The menu
  // controller may bring side-effects such as canceling the item selection.
  event->SetHandled();
}

void ClipboardHistoryMainButton::PaintButtonContents(gfx::Canvas* canvas) {
  if (!should_highlight_)
    return;

  // Highlight the background when the menu item is selected or pressed.
  cc::PaintFlags flags;
  flags.setAntiAlias(true);

  // Use the color in light mode when dark/light mode is not enabled. As the
  // background color of the context menu is from NativeTheme when the feature
  // is not enabled, and light mode is the default color of NativeTheme. If
  // dark/light mode is enabled, the background color of the context menus
  // inside SystemUI will be overridden to align with current system color mode.
  const SkColor color =
      features::IsDarkLightModeEnabled()
          ? GetColorProvider()->GetColor(kColorAshInkDrop)
          : SkColorSetA(SK_ColorBLACK,
                        StyleUtil::kLightInkDropOpacity * SK_AlphaOPAQUE);
  flags.setColor(color);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRect(GetLocalBounds(), flags);
}

}  // namespace ash
