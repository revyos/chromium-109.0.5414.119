// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/payment_request_sheet_controller.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view.h"
#include "chrome/browser/ui/views/payments/payment_request_views_util.h"
#include "components/payments/content/payment_request.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_search.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/table_layout_view.h"
#include "ui/views/metadata/view_factory.h"
#include "ui/views/painter.h"

namespace payments {

namespace internal {

// This class is the actual sheet that gets pushed on the view_stack_. It
// implements views::FocusTraversable to trap focus within its hierarchy. This
// way, focus doesn't leave the topmost sheet on the view stack to go on views
// that happen to be underneath.
// This class also overrides RequestFocus() to allow consumers to specify which
// view should be focused first (through SetFirstFocusableView). If no initial
// view is specified, the first view added to the hierarchy will get focus when
// this SheetView's RequestFocus() is called.
class SheetView : public views::BoxLayoutView, public views::FocusTraversable {
 public:
  METADATA_HEADER(SheetView);
  explicit SheetView(const base::RepeatingCallback<void(bool*)>&
                         enter_key_accelerator_callback)
      : enter_key_accelerator_callback_(enter_key_accelerator_callback) {
    if (enter_key_accelerator_callback_)
      AddAccelerator(enter_key_accelerator_);
  }
  SheetView(const SheetView&) = delete;
  SheetView& operator=(const SheetView&) = delete;
  ~SheetView() override = default;

  // Sets |view| as the first focusable view in this pane. If it's nullptr, the
  // fallback is to use focus_search_ to find the first focusable view.
  void SetFirstFocusableView(views::View* view) { first_focusable_ = view; }

 private:
  // views::FocusTraversable:
  views::FocusSearch* GetFocusSearch() override { return focus_search_.get(); }

  views::FocusTraversable* GetFocusTraversableParent() override {
    return parent()->GetFocusTraversable();
  }

  views::View* GetFocusTraversableParentView() override { return this; }

  // views::View:
  views::FocusTraversable* GetPaneFocusTraversable() override { return this; }

  void RequestFocus() override {
    // In accessibility contexts, we want to focus the title of the sheet.
    views::View* title =
        GetViewByID(static_cast<int>(payments::DialogViewID::SHEET_TITLE));
    views::FocusManager* focus = GetFocusManager();
    DCHECK(focus);

    title->RequestFocus();

    // RequestFocus only works if we are in an accessible context, and is a
    // no-op otherwise. Thus, if the focused view didn't change, we need to
    // proceed with setting the focus for standard usage.
    if (focus->GetFocusedView() == title)
      return;

    views::View* first_focusable = first_focusable_;

    if (!first_focusable) {
      views::FocusTraversable* dummy_focus_traversable;
      views::View* dummy_focus_traversable_view;
      first_focusable = focus_search_->FindNextFocusableView(
          nullptr, views::FocusSearch::SearchDirection::kForwards,
          views::FocusSearch::TraversalDirection::kDown,
          views::FocusSearch::StartingViewPolicy::kSkipStartingView,
          views::FocusSearch::AnchoredDialogPolicy::kCanGoIntoAnchoredDialog,
          &dummy_focus_traversable, &dummy_focus_traversable_view);
    }

    if (first_focusable)
      first_focusable->RequestFocus();
  }

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (accelerator != enter_key_accelerator_ ||
        !enter_key_accelerator_callback_)
      return views::View::AcceleratorPressed(accelerator);

    bool is_enabled = false;
    enter_key_accelerator_callback_.Run(&is_enabled);
    return is_enabled;
  }

  void ViewHierarchyChanged(
      const views::ViewHierarchyChangedDetails& details) override {
    if (!details.is_add && details.child == first_focusable_)
      first_focusable_ = nullptr;
  }

  raw_ptr<views::View> first_focusable_ = nullptr;
  std::unique_ptr<views::FocusSearch> focus_search_ =
      std::make_unique<views::FocusSearch>(/*root=*/this,
                                           /*cycle=*/true,
                                           /*accessibility_mode=*/false);
  ui::Accelerator enter_key_accelerator_{ui::VKEY_RETURN, ui::EF_NONE};
  base::RepeatingCallback<void(bool*)> enter_key_accelerator_callback_;
};

BEGIN_METADATA(SheetView, views::BoxLayoutView)
END_METADATA

BEGIN_VIEW_BUILDER(, SheetView, views::BoxLayoutView)
END_VIEW_BUILDER

// A scroll view that displays a separator on the bounds where content is
// scrolled out of view. For example, if the view can be scrolled up to reveal
// more content, the top of the content area will display a separator.
class BorderedScrollView : public views::ScrollView {
 public:
  METADATA_HEADER(BorderedScrollView);

  // The painter used by the scroll view to display the border.
  class BorderedScrollViewBorderPainter : public views::Painter {
   public:
    BorderedScrollViewBorderPainter(SkColor color,
                                    BorderedScrollView* scroll_view)
        : color_(color), scroll_view_(scroll_view) {}
    BorderedScrollViewBorderPainter(const BorderedScrollViewBorderPainter&) =
        delete;
    BorderedScrollViewBorderPainter& operator=(
        const BorderedScrollViewBorderPainter&) = delete;
    ~BorderedScrollViewBorderPainter() override = default;

   private:
    // views::Painter:
    gfx::Size GetMinimumSize() const override { return gfx::Size(0, 2); }

    void Paint(gfx::Canvas* canvas, const gfx::Size& size) override {
      if (scroll_view_->GetTopBorder()) {
        canvas->Draw1pxLine(gfx::PointF(), gfx::PointF(size.width(), 0),
                            color_);
      }

      if (scroll_view_->GetBottomBorder()) {
        canvas->Draw1pxLine(gfx::PointF(0, size.height() - 1),
                            gfx::PointF(size.width(), size.height() - 1),
                            color_);
      }
    }

   private:
    SkColor color_;
    // The scroll view that owns the border that owns this painter.
    raw_ptr<BorderedScrollView> scroll_view_;
  };

  BorderedScrollView() {
    SetBackground(
        views::CreateThemedSolidBackground(ui::kColorDialogBackground));
  }

  bool GetTopBorder() const { return GetVisibleRect().y() > 0; }

  bool GetBottomBorder() const {
    return GetVisibleRect().bottom() < contents()->height();
  }

  // views::ScrollView:
  void ScrollToPosition(views::ScrollBar* source, int position) override {
    views::ScrollView::ScrollToPosition(source, position);
    SchedulePaint();
  }
  void OnThemeChanged() override {
    ScrollView::OnThemeChanged();
    SetBorder(views::CreateBorderPainter(
        std::make_unique<BorderedScrollViewBorderPainter>(
            GetColorProvider()->GetColor(ui::kColorSeparator), this),
        gfx::Insets::VH(1, 0)));
  }
};

BEGIN_METADATA(BorderedScrollView, views::ScrollView)
ADD_READONLY_PROPERTY_METADATA(bool, TopBorder)
ADD_READONLY_PROPERTY_METADATA(bool, BottomBorder)
END_METADATA

}  // namespace internal

}  // namespace payments

DEFINE_VIEW_BUILDER(, payments::internal::SheetView)

namespace payments {

PaymentRequestSheetController::PaymentRequestSheetController(
    base::WeakPtr<PaymentRequestSpec> spec,
    base::WeakPtr<PaymentRequestState> state,
    base::WeakPtr<PaymentRequestDialogView> dialog)
    : spec_(spec), state_(state), dialog_(dialog) {}

PaymentRequestSheetController::~PaymentRequestSheetController() = default;

std::unique_ptr<views::View> PaymentRequestSheetController::CreateView() {
  // Create the footer now so that it's known if there's a primary button or not
  // before creating the sheet view. This way, it's possible to determine
  // whether there's something to do when the user hits enter.
  std::unique_ptr<views::View> footer = CreateFooterView();
  auto view =
      views::Builder<internal::SheetView>(
          std::make_unique<internal::SheetView>(
              primary_button_
                  ? base::BindRepeating(&PaymentRequestSheetController::
                                            PerformPrimaryButtonAction,
                                        weak_ptr_factory_.GetWeakPtr())
                  : base::RepeatingCallback<void(bool*)>()))
          .SetOrientation(views::BoxLayout::Orientation::kVertical)
          .CustomConfigure(base::BindOnce(
              [](PaymentRequestSheetController* controller,
                 internal::SheetView* sheet_view) {
                DialogViewID sheet_id;
                if (controller->GetSheetId(&sheet_id))
                  sheet_view->SetID(static_cast<int>(sheet_id));

                sheet_view->SetBackground(views::CreateThemedSolidBackground(
                    ui::kColorDialogBackground));

                // Paint the sheets to layers, otherwise the MD buttons (which
                // do paint to a layer) won't do proper clipping.
                sheet_view->SetPaintToLayer();
              },
              base::Unretained(this)))
          .AddChildren(
              views::Builder<views::View>()
                  .CopyAddressTo(&header_view_)
                  .CustomConfigure(base::BindOnce(
                      [](PaymentRequestSheetController* controller,
                         views::View* view) {
                        PopulateSheetHeaderView(
                            controller->ShouldShowHeaderBackArrow(),
                            controller->CreateHeaderContentView(view),
                            base::BindRepeating(&PaymentRequestSheetController::
                                                    BackButtonPressed,
                                                base::Unretained(controller)),
                            view, controller->GetHeaderBackground(view));
                      },
                      base::Unretained(this))),
              views::Builder<views::View>()
                  .CopyAddressTo(&header_content_separator_container_)
                  .SetUseDefaultFillLayout(true),
              // |content_view| will go into a views::ScrollView so it needs to
              // be sized now otherwise it'll be sized to the ScrollView's
              // viewport height, preventing the scroll bar from ever being
              // shown.
              views::Builder<views::ScrollView>(
                  DisplayDynamicBorderForHiddenContents()
                      ? std::make_unique<internal::BorderedScrollView>()
                      : std::make_unique<views::ScrollView>())
                  .CopyAddressTo(&scroll_)
                  .SetHorizontalScrollBarMode(
                      views::ScrollView::ScrollBarMode::kDisabled)
                  .SetContents(
                      views::Builder<views::TableLayoutView>()
                          .CopyAddressTo(&pane_)
                          .AddColumn(views::LayoutAlignment::kStretch,
                                     views::LayoutAlignment::kStart,
                                     views::TableLayout::kFixedSize,
                                     views::TableLayout::ColumnSize::kFixed,
                                     dialog_->GetActualDialogWidth(),
                                     dialog_->GetActualDialogWidth())
                          .AddRows(1, views::TableLayout::kFixedSize, 0)
                          .AddChild(
                              views::Builder<views::View>()
                                  .CopyAddressTo(&content_view_)
                                  .SetID(static_cast<int>(
                                      DialogViewID::CONTENT_VIEW))
                                  .CustomConfigure(base::BindOnce(
                                      [](views::View* content_view) {
                                        content_view->SetPaintToLayer();
                                        content_view->layer()
                                            ->SetFillsBoundsOpaquely(true);
                                        content_view->SetBackground(
                                            views::CreateThemedSolidBackground(
                                                ui::kColorDialogBackground));
                                      })))))
          .Build();

  view->SetFlexForView(scroll_, 1.0);
  if (footer)
    view->AddChildView(std::move(footer));

  UpdateContentView();

  view->SetFirstFocusableView(GetFirstFocusedView());
  return view;
}

void PaymentRequestSheetController::UpdateContentView() {
  // Do not update the view if the payment request is being aborted.
  if (!is_active_)
    return;

  content_view_->RemoveAllChildViews();
  FillContentView(content_view_);
  RelayoutPane();
}

void PaymentRequestSheetController::UpdateHeaderView() {
  // Do not update the view if the payment request is being aborted.
  if (!is_active_)
    return;

  header_view_->RemoveAllChildViews();
  PopulateSheetHeaderView(
      ShouldShowHeaderBackArrow(), CreateHeaderContentView(header_view_),
      base::BindRepeating(&PaymentRequestSheetController::BackButtonPressed,
                          base::Unretained(this)),
      header_view_, GetHeaderBackground(header_view_));
  header_view_->InvalidateLayout();
  header_view_->SchedulePaint();
}

void PaymentRequestSheetController::UpdateFocus(views::View* focused_view) {
  DialogViewID sheet_id;
  if (GetSheetId(&sheet_id)) {
    internal::SheetView* sheet_view = static_cast<internal::SheetView*>(
        dialog()->GetViewByID(static_cast<int>(sheet_id)));
    // This will be null on first call since it's not been set until CreateView
    // returns, and the first call to UpdateFocus() comes from CreateView.
    if (sheet_view) {
      sheet_view->SetFirstFocusableView(focused_view);
      dialog()->RequestFocus();
    }
  }
}

void PaymentRequestSheetController::RelayoutPane() {
  // Do not update the view if the payment request is being aborted.
  if (!is_active_)
    return;

  content_view_->InvalidateLayout();
  pane_->SizeToPreferredSize();
  // Now that the content and its surrounding pane are updated, force a Layout
  // on the ScrollView so that it updates its scroll bars now.
  scroll_->InvalidateLayout();
}

bool PaymentRequestSheetController::ShouldShowPrimaryButton() {
  return true;
}

std::u16string PaymentRequestSheetController::GetPrimaryButtonLabel() {
  return l10n_util::GetStringUTF16(IDS_PAYMENTS_CONTINUE_BUTTON);
}

PaymentRequestSheetController::ButtonCallback
PaymentRequestSheetController::GetPrimaryButtonCallback() {
  return base::BindRepeating(
      [](const base::WeakPtr<PaymentRequestDialogView>& dialog) {
        if (dialog->IsInteractive())
          dialog->Pay();
      },
      dialog());
}

int PaymentRequestSheetController::GetPrimaryButtonId() {
  return static_cast<int>(DialogViewID::PAY_BUTTON);
}

bool PaymentRequestSheetController::GetPrimaryButtonEnabled() {
  return state()->is_ready_to_pay();
}

bool PaymentRequestSheetController::ShouldShowSecondaryButton() {
  return true;
}

std::u16string PaymentRequestSheetController::GetSecondaryButtonLabel() {
  return l10n_util::GetStringUTF16(IDS_PAYMENTS_CANCEL_PAYMENT);
}

PaymentRequestSheetController::ButtonCallback
PaymentRequestSheetController::GetSecondaryButtonCallback() {
  return base::BindRepeating(&PaymentRequestSheetController::CloseButtonPressed,
                             base::Unretained(this));
}

int PaymentRequestSheetController::GetSecondaryButtonId() {
  return static_cast<int>(DialogViewID::CANCEL_BUTTON);
}

bool PaymentRequestSheetController::ShouldShowHeaderBackArrow() {
  return true;
}

std::unique_ptr<views::View>
PaymentRequestSheetController::CreateExtraFooterView() {
  return nullptr;
}

std::unique_ptr<views::View>
PaymentRequestSheetController::CreateHeaderContentView(
    views::View* header_view) {
  return views::Builder<views::Label>()
      .SetText(GetSheetTitle())
      .SetTextContext(views::style::CONTEXT_DIALOG_TITLE)
      .SetHorizontalAlignment(gfx::ALIGN_LEFT)
      .SetID(static_cast<int>(DialogViewID::SHEET_TITLE))
      .SetFocusBehavior(views::View::FocusBehavior::ACCESSIBLE_ONLY)
      .Build();
}

std::unique_ptr<views::Background>
PaymentRequestSheetController::GetHeaderBackground(views::View* header_view) {
  return views::CreateThemedSolidBackground(ui::kColorDialogBackground);
}

std::unique_ptr<views::View> PaymentRequestSheetController::CreateFooterView() {
  std::unique_ptr<views::View> extra_view = CreateExtraFooterView();
  views::BoxLayoutView* trailing_buttons_container = nullptr;
  bool has_extra_view = !!extra_view;

  auto container =
      views::Builder<views::TableLayoutView>()
          .SetBorder(views::CreateEmptyBorder(16))
          .AddColumn(views::LayoutAlignment::kStart,
                     views::LayoutAlignment::kCenter,
                     views::TableLayout::kFixedSize,
                     views::TableLayout::ColumnSize::kUsePreferred, 0, 0)
          .AddPaddingColumn(1.0, 0)
          .AddColumn(views::LayoutAlignment::kEnd,
                     views::LayoutAlignment::kCenter,
                     views::TableLayout::kFixedSize,
                     views::TableLayout::ColumnSize::kUsePreferred, 0, 0)
          .AddRows(1, views::TableLayout::kFixedSize, 0)
          .AddChildren(
              views::Builder<views::View>(
                  extra_view ? std::move(extra_view)
                             : std::make_unique<views::View>()),
              views::Builder<views::BoxLayoutView>()
                  .CopyAddressTo(&trailing_buttons_container)
                  .SetOrientation(views::BoxLayout::Orientation::kHorizontal)
                  .SetBetweenChildSpacing(kPaymentRequestButtonSpacing)
                  .CustomConfigure(base::BindOnce(
                      [](PaymentRequestSheetController* controller,
                         views::BoxLayoutView* container) {
#if BUILDFLAG(IS_MAC)
                        controller->AddSecondaryButton(container);
                        controller->AddPrimaryButton(container);
#else
                        controller->AddPrimaryButton(container);
                        controller->AddSecondaryButton(container);
#endif  // BUILDFLAG(IS_MAC)
                      },
                      base::Unretained(this))))
          .Build();

  if (!has_extra_view && trailing_buttons_container->children().empty()) {
    // If there's no extra view and no button, return null to signal that no
    // footer should be rendered.
    return nullptr;
  }

  return container;
}

views::View* PaymentRequestSheetController::GetFirstFocusedView() {
  if (primary_button_ && primary_button_->GetEnabled())
    return primary_button_;

  if (secondary_button_)
    return secondary_button_;

  DCHECK(content_view_);
  return content_view_;
}

bool PaymentRequestSheetController::GetSheetId(DialogViewID* sheet_id) {
  return false;
}

bool PaymentRequestSheetController::DisplayDynamicBorderForHiddenContents() {
  return true;
}

void PaymentRequestSheetController::CloseButtonPressed() {
  if (dialog()->IsInteractive())
    dialog()->CloseDialog();
}

void PaymentRequestSheetController::AddPrimaryButton(views::View* container) {
  if (ShouldShowPrimaryButton()) {
    views::Builder<views::View>(container)
        .AddChild(views::Builder<views::MdTextButton>()
                      .CopyAddressTo(&primary_button_)
                      .SetCallback(GetPrimaryButtonCallback())
                      .SetText(GetPrimaryButtonLabel())
                      .SetID(GetPrimaryButtonId())
                      .SetEnabled(GetPrimaryButtonEnabled())
                      .SetFocusBehavior(views::View::FocusBehavior::ALWAYS)
                      .SetProminent(true))
        .BuildChildren();
  }
}

void PaymentRequestSheetController::AddSecondaryButton(views::View* container) {
  if (ShouldShowSecondaryButton()) {
    views::Builder<views::View>(container)
        .AddChild(views::Builder<views::MdTextButton>()
                      .CopyAddressTo(&secondary_button_)
                      .SetCallback(GetSecondaryButtonCallback())
                      .SetText(GetSecondaryButtonLabel())
                      .SetID(GetSecondaryButtonId())
                      .SetFocusBehavior(views::View::FocusBehavior::ALWAYS))
        .BuildChildren();
  }
}

void PaymentRequestSheetController::PerformPrimaryButtonAction(
    bool* is_enabled) {
  // Set |is_enabled| to "true" to prevent other views from handling the event.
  *is_enabled = true;

  if (dialog()->IsInteractive() && primary_button_ &&
      primary_button_->GetEnabled()) {
    ButtonCallback callback = GetPrimaryButtonCallback();
    if (callback)
      callback.Run();
  }
}

void PaymentRequestSheetController::BackButtonPressed() {
  if (dialog()->IsInteractive())
    dialog()->GoBack();
}

}  // namespace payments
