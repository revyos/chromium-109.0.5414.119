// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_DESKS_DESK_BUTTON_BASE_H_
#define ASH_WM_DESKS_DESK_BUTTON_BASE_H_

#include "ash/ash_export.h"
#include "ash/wm/overview/overview_highlightable_view.h"
#include "base/functional/callback_forward.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/button/label_button.h"

namespace ash {

class WmHighlightItemBorder;

// The base class of buttons that appear in the `DesksBarView`.
class ASH_EXPORT DeskButtonBase : public views::LabelButton,
                                  public OverviewHighlightableView {
 public:
  METADATA_HEADER(DeskButtonBase);

  // This LabelButton will include either text or image inside. Set the text
  // of the button to `text` only if `set_text` is true, otherwise, the given
  // `text` will only be used for the tooltip, accessible name etc of the
  // button. If text of the button is empty, an image will be assigned to the
  // button instead.
  DeskButtonBase(const std::u16string& text,
                 bool set_text,
                 base::RepeatingClosure pressed_callback,
                 int border_corder_radius,
                 int corner_radius);
  ~DeskButtonBase() override;

  WmHighlightItemBorder* GetBorderPtr();

  // views::View:
  void OnFocus() override;
  void OnBlur() override;

  // views::LabelButton:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

  // OverviewHighlightableView:
  views::View* GetView() override;
  void MaybeActivateHighlightedView() override;
  void MaybeCloseHighlightedView(bool primary_action) override;
  void MaybeSwapHighlightedView(bool right) override;
  void OnViewHighlighted() override;
  void OnViewUnhighlighted() override;

  // Updates the label's text of the button. E.g, ZeroStateDefaultDeskButton
  // showing the desk's name, which should be updated on desk name changes.
  virtual void UpdateLabelText() {}

  // Sets `should_paint_background_` and repaints the button so that the button
  // may or may not have the background.
  void SetShouldPaintBackground(bool should_paint_background);

 protected:
  virtual void UpdateBorderState();

  // We paint the background within the button's bounds by default. But if
  // `paint_contents_only` is true, paints the contents' bounds of the button
  // only. For example, InnerExpandedDesksBarButton needs to be kept as the same
  // size of the desk preview, which has a gap between the view's contents and
  // the border.
  void set_paint_contents_only(bool paint_contents_only);

  // views::LabelButton:
  void UpdateBackgroundColor() override;

 private:
  friend class DesksTestApi;

  // If true, paints a background of the button with `background_color_`. The
  // button is painted with the background by default, exception like
  // ZeroStateIconButton only wants to be painted when the mouse hovers.
  bool should_paint_background_ = true;

  bool paint_contents_only_ = false;

  SkColor background_color_;

  const int corner_radius_;

  base::RepeatingClosure pressed_callback_;
};

}  // namespace ash

#endif  // ASH_WM_DESKS_DESK_BUTTON_BASE_H_