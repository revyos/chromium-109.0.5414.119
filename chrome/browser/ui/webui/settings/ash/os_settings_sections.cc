// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/ash/os_settings_sections.h"

#include "ash/components/phonehub/phone_hub_manager.h"
#include "build/branding_buildflags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/settings/ash/about_section.h"
#include "chrome/browser/ui/webui/settings/ash/accessibility_section.h"
#include "chrome/browser/ui/webui/settings/ash/apps_section.h"
#include "chrome/browser/ui/webui/settings/ash/bluetooth_section.h"
#include "chrome/browser/ui/webui/settings/ash/crostini_section.h"
#include "chrome/browser/ui/webui/settings/ash/date_time_section.h"
#include "chrome/browser/ui/webui/settings/ash/device_section.h"
#include "chrome/browser/ui/webui/settings/ash/files_section.h"
#include "chrome/browser/ui/webui/settings/ash/internet_section.h"
#include "chrome/browser/ui/webui/settings/ash/kerberos_section.h"
#include "chrome/browser/ui/webui/settings/ash/languages_section.h"
#include "chrome/browser/ui/webui/settings/ash/main_section.h"
#include "chrome/browser/ui/webui/settings/ash/multidevice_section.h"
#include "chrome/browser/ui/webui/settings/ash/people_section.h"
#include "chrome/browser/ui/webui/settings/ash/personalization_section.h"
#include "chrome/browser/ui/webui/settings/ash/printing_section.h"
#include "chrome/browser/ui/webui/settings/ash/privacy_section.h"
#include "chrome/browser/ui/webui/settings/ash/reset_section.h"
#include "chrome/browser/ui/webui/settings/ash/search_section.h"

namespace ash::settings {

namespace mojom {
using ::chromeos::settings::mojom::Section;
}

OsSettingsSections::OsSettingsSections(
    Profile* profile,
    SearchTagRegistry* search_tag_registry,
    multidevice_setup::MultiDeviceSetupClient* multidevice_setup_client,
    phonehub::PhoneHubManager* phone_hub_manager,
    syncer::SyncService* sync_service,
    SupervisedUserService* supervised_user_service,
    KerberosCredentialsManager* kerberos_credentials_manager,
    ArcAppListPrefs* arc_app_list_prefs,
    signin::IdentityManager* identity_manager,
    android_sms::AndroidSmsService* android_sms_service,
    CupsPrintersManager* printers_manager,
    apps::AppServiceProxy* app_service_proxy,
    eche_app::EcheAppManager* eche_app_manager) {
  // Special case: Main section does not have an associated enum value.
  sections_.push_back(
      std::make_unique<MainSection>(profile, search_tag_registry));

  auto internet_section =
      std::make_unique<InternetSection>(profile, search_tag_registry);
  sections_map_[mojom::Section::kNetwork] = internet_section.get();
  sections_.push_back(std::move(internet_section));

  auto bluetooth_section = std::make_unique<BluetoothSection>(
      profile, search_tag_registry, profile->GetPrefs());
  sections_map_[mojom::Section::kBluetooth] = bluetooth_section.get();
  sections_.push_back(std::move(bluetooth_section));

  auto multidevice_section = std::make_unique<MultiDeviceSection>(
      profile, search_tag_registry, multidevice_setup_client, phone_hub_manager,
      android_sms_service, profile->GetPrefs(), eche_app_manager);
  sections_map_[mojom::Section::kMultiDevice] = multidevice_section.get();
  sections_.push_back(std::move(multidevice_section));

  auto people_section = std::make_unique<PeopleSection>(
      profile, search_tag_registry, sync_service, supervised_user_service,
      identity_manager, profile->GetPrefs());
  sections_map_[mojom::Section::kPeople] = people_section.get();
  sections_.push_back(std::move(people_section));

  auto device_section = std::make_unique<DeviceSection>(
      profile, search_tag_registry, profile->GetPrefs());
  sections_map_[mojom::Section::kDevice] = device_section.get();
  sections_.push_back(std::move(device_section));

  auto personalization_section = std::make_unique<PersonalizationSection>(
      profile, search_tag_registry, profile->GetPrefs());
  sections_map_[mojom::Section::kPersonalization] =
      personalization_section.get();
  sections_.push_back(std::move(personalization_section));

  auto search_section =
      std::make_unique<SearchSection>(profile, search_tag_registry);
  sections_map_[mojom::Section::kSearchAndAssistant] = search_section.get();
  sections_.push_back(std::move(search_section));

  auto apps_section = std::make_unique<AppsSection>(
      profile, search_tag_registry, profile->GetPrefs(), arc_app_list_prefs,
      app_service_proxy);
  sections_map_[mojom::Section::kApps] = apps_section.get();
  sections_.push_back(std::move(apps_section));

  auto crostini_section = std::make_unique<CrostiniSection>(
      profile, search_tag_registry, profile->GetPrefs());
  sections_map_[mojom::Section::kCrostini] = crostini_section.get();
  sections_.push_back(std::move(crostini_section));

  auto date_time_section =
      std::make_unique<DateTimeSection>(profile, search_tag_registry);
  sections_map_[mojom::Section::kDateAndTime] = date_time_section.get();
  sections_.push_back(std::move(date_time_section));

  auto privacy_section = std::make_unique<PrivacySection>(
      profile, search_tag_registry, profile->GetPrefs());
  sections_map_[mojom::Section::kPrivacyAndSecurity] = privacy_section.get();
  sections_.push_back(std::move(privacy_section));

  auto language_section = std::make_unique<LanguagesSection>(
      profile, search_tag_registry, profile->GetPrefs());
  sections_map_[mojom::Section::kLanguagesAndInput] = language_section.get();
  sections_.push_back(std::move(language_section));

  auto files_section =
      std::make_unique<FilesSection>(profile, search_tag_registry);
  sections_map_[mojom::Section::kFiles] = files_section.get();
  sections_.push_back(std::move(files_section));

  auto printing_section = std::make_unique<PrintingSection>(
      profile, search_tag_registry, printers_manager);
  sections_map_[mojom::Section::kPrinting] = printing_section.get();
  sections_.push_back(std::move(printing_section));

  auto accessibility_section = std::make_unique<AccessibilitySection>(
      profile, search_tag_registry, profile->GetPrefs());
  sections_map_[mojom::Section::kAccessibility] = accessibility_section.get();
  sections_.push_back(std::move(accessibility_section));

  auto reset_section =
      std::make_unique<ResetSection>(profile, search_tag_registry);
  sections_map_[mojom::Section::kReset] = reset_section.get();
  sections_.push_back(std::move(reset_section));

#if BUILDFLAG(GOOGLE_CHROME_BRANDING)
  auto about_section = std::make_unique<AboutSection>(
      profile, search_tag_registry, profile->GetPrefs());
#else
  auto about_section =
      std::make_unique<AboutSection>(profile, search_tag_registry);
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING)
  sections_map_[mojom::Section::kAboutChromeOs] = about_section.get();
  sections_.push_back(std::move(about_section));

  auto kerberos_section = std::make_unique<KerberosSection>(
      profile, search_tag_registry, kerberos_credentials_manager);
  sections_map_[mojom::Section::kKerberos] = kerberos_section.get();
  sections_.push_back(std::move(kerberos_section));
}

OsSettingsSections::OsSettingsSections() = default;

OsSettingsSections::~OsSettingsSections() = default;

const OsSettingsSection* OsSettingsSections::GetSection(
    mojom::Section section) const {
  const auto it = sections_map_.find(section);
  CHECK(it != sections_map_.end());
  return it->second;
}

}  // namespace ash::settings
