// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/eche_app_ui/launch_app_helper.h"

#include "ash/components/phonehub/phone_hub_manager.h"
#include "ash/components/phonehub/screen_lock_manager.h"
#include "ash/constants/notifier_catalogs.h"
#include "ash/public/cpp/system/toast_data.h"
#include "ash/public/cpp/system/toast_manager.h"
#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/webui/eche_app_ui/eche_alert_generator.h"
#include "base/check.h"
#include "ui/gfx/image/image.h"

namespace ash {
namespace eche_app {

LaunchAppHelper::NotificationInfo::NotificationInfo(
    Category category,
    absl::variant<NotificationType, mojom::WebNotificationType> type)
    : category_(category), type_(type) {
  DCHECK(nullptr != absl::get_if<NotificationType>(&type)
             ? category == Category::kNative
             : category == Category::kWebUI);
  DCHECK(nullptr != absl::get_if<mojom::WebNotificationType>(&type)
             ? category == Category::kWebUI
             : category == Category::kNative);
}

LaunchAppHelper::NotificationInfo::~NotificationInfo() = default;

LaunchAppHelper::LaunchAppHelper(
    phonehub::PhoneHubManager* phone_hub_manager,
    LaunchEcheAppFunction launch_eche_app_function,
    LaunchNotificationFunction launch_notification_function,
    CloseNotificationFunction close_notification_function)
    : phone_hub_manager_(phone_hub_manager),
      launch_eche_app_function_(launch_eche_app_function),
      launch_notification_function_(launch_notification_function),
      close_notification_function_(close_notification_function) {}
LaunchAppHelper::~LaunchAppHelper() = default;

LaunchAppHelper::AppLaunchProhibitedReason
LaunchAppHelper::CheckAppLaunchProhibitedReason(FeatureStatus status) const {
  if (IsScreenLockRequired()) {
    return LaunchAppHelper::AppLaunchProhibitedReason::kDisabledByScreenLock;
  }

  return LaunchAppHelper::AppLaunchProhibitedReason::kNotProhibited;
}

bool LaunchAppHelper::IsScreenLockRequired() const {
  const bool enable_phone_screen_lock =
      phone_hub_manager_->GetScreenLockManager()->GetLockStatus() ==
      phonehub::ScreenLockManager::LockStatus::kLockedOn;
  auto* session_controller = ash::Shell::Get()->session_controller();
  const bool enable_cros_screen_lock =
      session_controller->CanLockScreen() &&
      session_controller->ShouldLockScreenAutomatically();
  // We should ask users to enable screen lock if they enabled screen lock on
  // their phone but didn't enable yet on CrOS.
  const bool should_lock = enable_phone_screen_lock && !enable_cros_screen_lock;
  return should_lock;
}

void LaunchAppHelper::ShowNotification(
    const absl::optional<std::u16string>& title,
    const absl::optional<std::u16string>& message,
    std::unique_ptr<NotificationInfo> info) const {
  launch_notification_function_.Run(title, message, std::move(info));
}

void LaunchAppHelper::CloseNotification(
    const std::string& notification_id) const {
  close_notification_function_.Run(notification_id);
}

void LaunchAppHelper::ShowToast(const std::u16string& text) const {
  ash::ToastManager::Get()->Show(ash::ToastData(
      kEcheAppToastId, ash::ToastCatalogName::kEcheAppToast, text));
}

void LaunchAppHelper::LaunchEcheApp(absl::optional<int64_t> notification_id,
                                    const std::string& package_name,
                                    const std::u16string& visible_name,
                                    const absl::optional<int64_t>& user_id,
                                    const gfx::Image& icon) const {
  launch_eche_app_function_.Run(notification_id, package_name, visible_name,
                                user_id, icon);
}

}  // namespace eche_app
}  // namespace ash
