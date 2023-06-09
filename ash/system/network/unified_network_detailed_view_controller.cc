// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/network/unified_network_detailed_view_controller.h"

#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/network/network_list_view.h"
#include "ash/system/tray/detailed_view_delegate.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash {

UnifiedNetworkDetailedViewController::UnifiedNetworkDetailedViewController(
    UnifiedSystemTrayController* tray_controller)
    : detailed_view_delegate_(
          std::make_unique<DetailedViewDelegate>(tray_controller)) {
}

UnifiedNetworkDetailedViewController::~UnifiedNetworkDetailedViewController() =
    default;

views::View* UnifiedNetworkDetailedViewController::CreateView() {
  DCHECK(!view_);
  view_ =
      new NetworkListView(detailed_view_delegate_.get(),
                          Shell::Get()->session_controller()->login_status());
  view_->Init();
  return view_;
}

std::u16string UnifiedNetworkDetailedViewController::GetAccessibleName() const {
  return l10n_util::GetStringUTF16(
      IDS_ASH_QUICK_SETTINGS_BUBBLE_NETWORK_SETTINGS_ACCESSIBLE_DESCRIPTION);
}

}  // namespace ash
