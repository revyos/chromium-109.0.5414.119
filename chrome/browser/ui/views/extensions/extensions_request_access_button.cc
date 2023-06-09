// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/extensions/extensions_request_access_button.h"

#include <algorithm>
#include <iterator>
#include <memory>

#include "base/bind.h"
#include "base/check_op.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/extensions/extension_action_view_controller.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_controller.h"
#include "chrome/browser/ui/views/extensions/extensions_dialogs_utils.h"
#include "chrome/browser/ui/views/extensions/extensions_request_access_button_hover_card.h"
#include "chrome/browser/ui/views/extensions/extensions_request_access_dialog_view.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_button.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_container.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"

ExtensionsRequestAccessButton::ExtensionsRequestAccessButton(Browser* browser)
    : ToolbarButton(
          base::BindRepeating(&ExtensionsRequestAccessButton::OnButtonPressed,
                              base::Unretained(this))),
      browser_(browser) {}

ExtensionsRequestAccessButton::~ExtensionsRequestAccessButton() = default;

void ExtensionsRequestAccessButton::UpdateExtensionsRequestingAccess(
    std::vector<ToolbarActionViewController*> extensions_requesting_access) {
  DCHECK(!extensions_requesting_access.empty());
  extensions_requesting_access_ = extensions_requesting_access;

  // TODO(crbug.com/1239772): Set the label and background color without borders
  // separately to match the mocks. For now, using SetHighlight to display that
  // adds a border and highlight color in addition to the label.
  absl::optional<SkColor> color;
  SetHighlight(l10n_util::GetStringFUTF16Int(
                   IDS_EXTENSIONS_REQUEST_ACCESS_BUTTON,
                   static_cast<int>(extensions_requesting_access_.size())),
               color);
}

void ExtensionsRequestAccessButton::MaybeShowHoverCard() {
  // TODO(crbug.com/1319555): Don't show the hover card if the dialog opened by
  // pressing the button is still open. This will be easier to add once we
  // address the TODO below for blocked action dialog.
  if (ExtensionsRequestAccessButtonHoverCard::IsShowing() ||
      !GetWidget()->IsMouseEventsEnabled())
    return;

  ExtensionsRequestAccessButtonHoverCard::ShowBubble(
      GetActiveWebContents(), this, extensions_requesting_access_);
}

std::vector<std::string>
ExtensionsRequestAccessButton::GetExtensionsNamesForTesting() {
  std::vector<std::string> extension_names;
  for (auto* extension : extensions_requesting_access_) {
    extension_names.push_back(base::UTF16ToUTF8(extension->GetActionName()));
  }
  return extension_names;
}

std::u16string ExtensionsRequestAccessButton::GetTooltipText(
    const gfx::Point& p) const {
  // Request access button hover cards replace tooltips.
  return std::u16string();
}

void ExtensionsRequestAccessButton::OnButtonPressed() {
  if (ExtensionsRequestAccessButtonHoverCard::IsShowing()) {
    ExtensionsRequestAccessButtonHoverCard::HideBubble();
  }

  content::WebContents* web_contents = GetActiveWebContents();
  extensions::ExtensionActionRunner* action_runner =
      extensions::ExtensionActionRunner::GetForWebContents(web_contents);
  if (!action_runner)
    return;

  DCHECK_GT(extensions_requesting_access_.size(), 0u);
  const extensions::ExtensionSet& enabled_extensions =
      extensions::ExtensionRegistry::Get(browser_->profile())
          ->enabled_extensions();
  std::vector<const extensions::Extension*> extensions;
  for (auto* extension : extensions_requesting_access_) {
    extensions.push_back(enabled_extensions.GetByID(extension->GetId()));
  }

  base::RecordAction(base::UserMetricsAction(
      "Extensions.Toolbar.ExtensionsActivatedFromRequestAccessButton"));
  action_runner->GrantTabPermissions(extensions);
}

// Linux enter/leave events are sometimes flaky, so we don't want to "miss"
// an enter event and fail to hover the button. This is effectively a no-op if
// the button is already showing the hover card (crbug.com/1326272).
void ExtensionsRequestAccessButton::OnMouseMoved(const ui::MouseEvent& event) {
  MaybeShowHoverCard();
}

void ExtensionsRequestAccessButton::OnMouseEntered(
    const ui::MouseEvent& event) {
  MaybeShowHoverCard();
}

void ExtensionsRequestAccessButton::OnMouseExited(const ui::MouseEvent& event) {
  ExtensionsRequestAccessButtonHoverCard::HideBubble();
}

content::WebContents* ExtensionsRequestAccessButton::GetActiveWebContents() {
  return browser_->tab_strip_model()->GetActiveWebContents();
}
