// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/bubble/bubble_utils.h"

#include <memory>
#include <utility>

#include "ash/capture_mode/capture_mode_util.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/root_window_controller.h"
#include "ash/shell.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/check.h"
#include "ui/aura/window.h"
#include "ui/events/event.h"
#include "ui/events/types/event_type.h"
#include "ui/views/controls/label.h"

namespace ash {
namespace bubble_utils {
namespace {

// A label which invokes a constructor-specified callback in `OnThemeChanged()`.
class LabelWithThemeChangedCallback : public views::Label {
 public:
  using ThemeChangedCallback = base::RepeatingCallback<void(views::Label*)>;

  LabelWithThemeChangedCallback(const std::u16string& text,
                                ThemeChangedCallback theme_changed_callback)
      : views::Label(text),
        theme_changed_callback_(std::move(theme_changed_callback)) {}

  LabelWithThemeChangedCallback(const LabelWithThemeChangedCallback&) = delete;
  LabelWithThemeChangedCallback& operator=(
      const LabelWithThemeChangedCallback&) = delete;
  ~LabelWithThemeChangedCallback() override = default;

 private:
  // views::Label:
  void OnThemeChanged() override {
    views::Label::OnThemeChanged();
    theme_changed_callback_.Run(this);
  }

  ThemeChangedCallback theme_changed_callback_;
};

}  // namespace

bool ShouldCloseBubbleForEvent(const ui::LocatedEvent& event) {
  // Should only be called for "press" type events.
  DCHECK(event.type() == ui::ET_MOUSE_PRESSED ||
         event.type() == ui::ET_TOUCH_PRESSED ||
         event.type() == ui::ET_GESTURE_LONG_PRESS ||
         event.type() == ui::ET_GESTURE_TAP ||
         event.type() == ui::ET_GESTURE_TWO_FINGER_TAP)
      << event.type();

  // Users in a capture session may be trying to capture the bubble.
  if (capture_mode_util::IsCaptureModeActive())
    return false;

  aura::Window* target = static_cast<aura::Window*>(event.target());
  if (!target)
    return false;

  RootWindowController* root_controller =
      RootWindowController::ForWindow(target);
  if (!root_controller)
    return false;

  // Bubbles can spawn menus, so don't close for clicks inside menus.
  aura::Window* menu_container =
      root_controller->GetContainer(kShellWindowId_MenuContainer);
  if (menu_container->Contains(target))
    return false;

  // Taps on virtual keyboard should not close bubbles.
  aura::Window* keyboard_container =
      root_controller->GetContainer(kShellWindowId_VirtualKeyboardContainer);
  if (keyboard_container->Contains(target))
    return false;

  // Touch text selection controls should not close bubbles.
  // https://crbug.com/1165938
  aura::Window* settings_bubble_container =
      root_controller->GetContainer(kShellWindowId_SettingBubbleContainer);
  if (settings_bubble_container->Contains(target))
    return false;

  return true;
}

void ApplyStyle(views::Label* label,
                TypographyStyle style,
                AshColorProvider::ContentLayerType text_color) {
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(
      AshColorProvider::Get()->GetContentLayerColor(text_color));

  switch (style) {
    case TypographyStyle::kAnnotation1:
      label->SetFontList(gfx::FontList({"Roboto"}, gfx::Font::NORMAL, 12,
                                       gfx::Font::Weight::NORMAL));
      break;
    case TypographyStyle::kBody1:
      label->SetFontList(gfx::FontList({"Roboto"}, gfx::Font::NORMAL, 14,
                                       gfx::Font::Weight::NORMAL));
      break;
    case TypographyStyle::kBody2:
      label->SetFontList(gfx::FontList({"Roboto"}, gfx::Font::NORMAL, 13,
                                       gfx::Font::Weight::NORMAL));
      break;
    case TypographyStyle::kButton1:
      label->SetFontList(gfx::FontList({"Roboto"}, gfx::Font::NORMAL, 14,
                                       gfx::Font::Weight::MEDIUM));
      break;
    case TypographyStyle::kButton2:
      label->SetFontList(gfx::FontList({"Roboto"}, gfx::Font::NORMAL, 13,
                                       gfx::Font::Weight::MEDIUM));
      break;
    case TypographyStyle::kLabel1:
      label->SetFontList(gfx::FontList({"Roboto"}, gfx::Font::NORMAL, 10,
                                       gfx::Font::Weight::MEDIUM));
      break;
    case TypographyStyle::kTitle1:
      label->SetFontList(gfx::FontList({"Roboto"}, gfx::Font::NORMAL, 16,
                                       gfx::Font::Weight::MEDIUM));
      break;
  }
}

std::unique_ptr<views::Label> CreateLabel(
    TypographyStyle style,
    const std::u16string& text,
    AshColorProvider::ContentLayerType text_color) {
  auto label = std::make_unique<LabelWithThemeChangedCallback>(
      text,
      /*theme_changed_callback=*/base::BindRepeating(
          [](TypographyStyle style,
             AshColorProvider::ContentLayerType text_color,
             views::Label* label) { ApplyStyle(label, style, text_color); },
          style, text_color));
  // Apply `style` to `label` manually in case the view is painted without ever
  // having being added to the view hierarchy. In such cases, the `label` will
  // not receive an `OnThemeChanged()` event. This occurs, for example, with
  // holding space drag images.
  ApplyStyle(label.get(), style, text_color);
  return label;
}

}  // namespace bubble_utils
}  // namespace ash
