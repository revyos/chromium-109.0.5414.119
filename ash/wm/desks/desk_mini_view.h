// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_DESKS_DESK_MINI_VIEW_H_
#define ASH_WM_DESKS_DESK_MINI_VIEW_H_

#include <memory>

#include "ash/ash_export.h"
#include "ash/wm/desks/desk.h"
#include "ash/wm/desks/desks_controller.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view_observer.h"

namespace ash {

class CloseButton;
class DeskActionContextMenu;
class DeskActionView;
class DeskNameView;
class DeskPreviewView;
class DesksBarView;

// A view that acts as a mini representation (a.k.a. desk thumbnail) of a
// virtual desk in the desk bar view when overview mode is active. This view
// shows a preview of the contents of the associated desk, its title, and
// supports desk activation and removal.
class ASH_EXPORT DeskMiniView : public views::View,
                                public Desk::Observer,
                                public views::TextfieldController,
                                public views::ViewObserver {
 public:
  // Returns the width of the desk preview based on its |preview_height| and the
  // aspect ratio of the root window taken from |root_window_size|.
  static int GetPreviewWidth(const gfx::Size& root_window_size,
                             int preview_height);

  // The desk preview bounds are proportional to the bounds of the display on
  // which it resides.
  static gfx::Rect GetDeskPreviewBounds(aura::Window* root_window);

  DeskMiniView(DesksBarView* owner_bar, aura::Window* root_window, Desk* desk);

  DeskMiniView(const DeskMiniView&) = delete;
  DeskMiniView& operator=(const DeskMiniView&) = delete;

  ~DeskMiniView() override;

  aura::Window* root_window() { return root_window_; }

  Desk* desk() { return desk_; }

  DeskNameView* desk_name_view() { return desk_name_view_; }

  const CloseButton* close_desk_button() const { return close_desk_button_; }

  const DeskActionView* desk_action_view() const { return desk_action_view_; }
  DeskActionView* desk_action_view() { return desk_action_view_; }

  DesksBarView* owner_bar() { return owner_bar_; }
  const DeskPreviewView* desk_preview() const { return desk_preview_; }
  DeskPreviewView* desk_preview() { return desk_preview_; }

  bool is_animating_to_remove() const { return is_animating_to_remove_; }
  void set_is_animating_to_remove(bool value) {
    is_animating_to_remove_ = value;
  }

  gfx::Rect GetPreviewBoundsInScreen() const;

  // Returns the associated desk's container window on the display this
  // mini_view resides on.
  aura::Window* GetDeskContainer() const;

  // Returns true if the desk's name is being modified (i.e. the DeskNameView
  // has the focus).
  bool IsDeskNameBeingModified() const;

  // Updates the visibility state of the desk buttons depending on whether this
  // view is mouse hovered, or if switch access is enabled.
  void UpdateDeskButtonVisibility();

  // Gesture tapping may affect the visibility of the desk buttons. There's only
  // one mini_view that shows the desk buttons on long press at any time.
  // This is useful for touch-only UIs.
  void OnWidgetGestureTap(const gfx::Rect& screen_rect, bool is_long_gesture);

  // Updates the border color of the DeskPreviewView based on the activation
  // state of the corresponding desk and whether the desks template grid is
  // visible.
  void UpdateBorderColor();

  // Gets the preview border's insets.
  gfx::Insets GetPreviewBorderInsets() const;

  bool IsPointOnMiniView(const gfx::Point& screen_location) const;

  // Hides the `desk_action_view_` and opens `context_menu_`. Called when
  // `desk_preview_` is right-clicked or long-pressed. `source` is the type of
  // action that caused the context menu to be opened (e.g. long press versus
  // mouse click), and is provided to the context menu runner when the menu is
  // open in `DeskActionContextMenu::ShowContextMenuForViewImpl` so that it can
  // further evaluate menu positioning. This ends up doing nothing in particular
  // in the case of the `DeskActionContextMenu` because we use a
  // `views::MenuRunner::FIXED_ANCHOR` run type parameter, but the
  // `MenuRunner::RunMenuAt` function still requires this parameter, so we pass
  // it down to the function through this parameter.
  void OpenContextMenu(ui::MenuSourceType source);

  // Closes context menu on this mini view if one exists.
  void MaybeCloseContextMenu();

  // Sets either the `desk_action_view_` or the `close_desk_button_` visibility
  // to false depending on whether the `kDesksCloseAll` feature is active, and
  // then removes the desk. If `close_type` is `kCloseAllWindows*`, this
  // function tells the `DesksController` to remove `desk_`'s windows as well,
  // and wait for the user to confirm.
  void OnRemovingDesk(DeskCloseType close_type);

  // views::View:
  const char* GetClassName() const override;
  void Layout() override;
  gfx::Size CalculatePreferredSize() const override;
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override;
  void OnThemeChanged() override;

  // Desk::Observer:
  void OnContentChanged() override;
  void OnDeskDestroyed(const Desk* desk) override;
  void OnDeskNameChanged(const std::u16string& new_name) override;

  // views::TextfieldController:
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  bool HandleMouseEvent(views::Textfield* sender,
                        const ui::MouseEvent& mouse_event) override;

  // views::ViewObserver:
  void OnViewFocused(views::View* observed_view) override;
  void OnViewBlurred(views::View* observed_view) override;

 private:
  friend class DesksTestApi;

  // Callback for when `context_menu_` is closed. Makes `desk_action_view_`
  // visible.
  void OnContextMenuClosed();

  void OnDeskPreviewPressed();

  // Layout |desk_name_view_| given the current bounds of the desk preview.
  void LayoutDeskNameView(const gfx::Rect& preview_bounds);

  DesksBarView* const owner_bar_;

  // The root window on which this mini_view is created.
  aura::Window* const root_window_;

  // The associated desk. Can be null when the desk is deleted before this
  // mini_view completes its removal animation. See comment above
  // OnDeskRemoved().
  Desk* desk_;  // Not owned.

  // The view that shows a preview of the desk contents.
  DeskPreviewView* desk_preview_ = nullptr;

  // The editable desk name.
  DeskNameView* desk_name_view_ = nullptr;

  // The close button that shows on hover.
  CloseButton* close_desk_button_ = nullptr;

  // When the Close-All flag is enabled, we store the hover interface for desk
  // actions here.
  DeskActionView* desk_action_view_ = nullptr;

  // The context menu that appears when `desk_preview_` is right-clicked or
  // long-pressed.
  std::unique_ptr<DeskActionContextMenu> context_menu_;

  // True when this mini view is being animated to be removed from the bar.
  bool is_animating_to_remove_ = false;

  // We force showing desk buttons when the mini_view is long pressed or
  // tapped using touch gestures.
  bool force_show_desk_buttons_ = false;

  // Prevents `desk_action_view_` from becoming visible while `context_menu_` is
  // open.
  bool is_context_menu_open_ = false;

  // When the DeskNameView is focused, we select all its text. However, if it is
  // focused via a mouse press event, on mouse release will clear the selection.
  // Therefore, we defer selecting all text until we receive that mouse release.
  bool defer_select_all_ = false;

  bool is_desk_name_being_modified_ = false;

  // This is initialized to true and tells the OnViewBlurred function if the
  // user wants to set a new desk name. We set this to false if the
  // HandleKeyEvent function detects that the escape key was pressed so that
  // OnViewBlurred does not change the name of `desk_`.
  bool should_commit_name_changes_ = true;
};

}  // namespace ash

#endif  // ASH_WM_DESKS_DESK_MINI_VIEW_H_
