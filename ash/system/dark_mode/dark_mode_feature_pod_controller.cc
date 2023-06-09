// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/dark_mode/dark_mode_feature_pod_controller.h"

#include "ash/constants/quick_settings_catalogs.h"
#include "ash/public/cpp/system_tray_client.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/style/dark_light_mode_controller_impl.h"
#include "ash/system/model/system_tray_model.h"
#include "ash/system/tray/tray_popup_utils.h"
#include "ash/system/unified/feature_pod_button.h"
#include "base/metrics/histogram_functions.h"
#include "ui/base/l10n/l10n_util.h"

namespace ash {

DarkModeFeaturePodController::DarkModeFeaturePodController(
    UnifiedSystemTrayController* tray_controller)
    : tray_controller_(tray_controller) {
  DCHECK(tray_controller_);
  DarkLightModeControllerImpl::Get()->AddObserver(this);
}

DarkModeFeaturePodController::~DarkModeFeaturePodController() {
  DarkLightModeControllerImpl::Get()->RemoveObserver(this);
}

FeaturePodButton* DarkModeFeaturePodController::CreateButton() {
  DCHECK(!button_);
  button_ = new FeaturePodButton(this);
  button_->SetVectorIcon(kUnifiedMenuDarkModeIcon);
  button_->SetLabel(l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_DARK_THEME));
  button_->SetLabelTooltip(l10n_util::GetStringUTF16(
      IDS_ASH_STATUS_TRAY_DARK_THEME_SETTINGS_TOOLTIP));
  // TODO(minch): Add the logic for login screen.
  // Disable dark mode feature pod in OOBE since only light mode should be
  // allowed there.
  button_->SetVisible(
      Shell::Get()->session_controller()->IsActiveUserSessionStarted() &&
      Shell::Get()->session_controller()->GetSessionState() !=
          session_manager::SessionState::OOBE);

  UpdateButton(DarkLightModeControllerImpl::Get()->IsDarkModeEnabled());
  return button_;
}

QsFeatureCatalogName DarkModeFeaturePodController::GetCatalogName() {
  return QsFeatureCatalogName::kDarkMode;
}

void DarkModeFeaturePodController::OnIconPressed() {
  // Toggling Dark theme feature pod button inside quick settings should cancel
  // auto scheduling. This ensures that on and off states of the pod button
  // match the non-scheduled states of Dark and Light buttons in
  // personalization hub respectively.
  auto* dark_light_mode_controller = DarkLightModeControllerImpl::Get();
  TrackToggleUMA(
      /*target_toggle_state=*/!dark_light_mode_controller->IsDarkModeEnabled());
  dark_light_mode_controller->SetAutoScheduleEnabled(
      /*enabled=*/false);
  dark_light_mode_controller->ToggleColorMode();
  base::UmaHistogramBoolean("Ash.DarkTheme.SystemTray.IsDarkModeEnabled",
                            dark_light_mode_controller->IsDarkModeEnabled());
}

void DarkModeFeaturePodController::OnLabelPressed() {
  TrackDiveInUMA();
  Shell::Get()->system_tray_model()->client()->ShowDarkModeSettings();
}

void DarkModeFeaturePodController::OnColorModeChanged(bool dark_mode_enabled) {
  UpdateButton(dark_mode_enabled);
}

void DarkModeFeaturePodController::UpdateButton(bool dark_mode_enabled) {
  button_->SetToggled(dark_mode_enabled);
  if (ash::Shell::Get()
          ->dark_light_mode_controller()
          ->GetAutoScheduleEnabled()) {
    button_->SetSubLabel(l10n_util::GetStringUTF16(
        dark_mode_enabled
            ? IDS_ASH_STATUS_TRAY_DARK_THEME_ON_STATE_AUTO_SCHEDULED
            : IDS_ASH_STATUS_TRAY_DARK_THEME_OFF_STATE_AUTO_SCHEDULED));
  } else {
    button_->SetSubLabel(l10n_util::GetStringUTF16(
        dark_mode_enabled ? IDS_ASH_STATUS_TRAY_DARK_THEME_ON_STATE
                          : IDS_ASH_STATUS_TRAY_DARK_THEME_OFF_STATE));
  }

  std::u16string tooltip_state = l10n_util::GetStringUTF16(
      dark_mode_enabled
          ? IDS_ASH_STATUS_TRAY_DARK_THEME_ENABLED_STATE_TOOLTIP
          : IDS_ASH_STATUS_TRAY_DARK_THEME_DISABLED_STATE_TOOLTIP);
  button_->SetIconTooltip(l10n_util::GetStringFUTF16(
      IDS_ASH_STATUS_TRAY_DARK_THEME_TOGGLE_TOOLTIP, tooltip_state));
}

}  // namespace ash
