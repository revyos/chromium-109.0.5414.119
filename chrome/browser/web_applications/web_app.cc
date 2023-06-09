// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app.h"

#include <ostream>
#include <tuple>
#include <utility>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/web_applications/user_display_mode.h"
#include "chrome/browser/web_applications/web_app_chromeos_data.h"
#include "chrome/browser/web_applications/web_app_constants.h"
#include "chrome/browser/web_applications/web_app_helpers.h"
#include "chrome/browser/web_applications/web_app_sources.h"
#include "chrome/browser/web_applications/web_app_utils.h"
#include "components/sync/base/time.h"
#include "third_party/blink/public/common/manifest/manifest_util.h"
#include "third_party/blink/public/common/permissions_policy/origin_with_possible_wildcards.h"
#include "third_party/blink/public/common/permissions_policy/policy_helper_public.h"
#include "third_party/blink/public/mojom/manifest/manifest.mojom.h"
#include "ui/gfx/color_utils.h"

namespace web_app {

namespace {

std::string ColorToString(absl::optional<SkColor> color) {
  return color.has_value() ? color_utils::SkColorToRgbaString(color.value())
                           : "none";
}

std::string ApiApprovalStateToString(ApiApprovalState state) {
  switch (state) {
    case ApiApprovalState::kRequiresPrompt:
      return "kRequiresPrompt";
    case ApiApprovalState::kAllowed:
      return "kAllowed";
    case ApiApprovalState::kDisallowed:
      return "kDisallowed";
  }
}

std::string OsIntegrationStateToString(OsIntegrationState state) {
  switch (state) {
    case OsIntegrationState::kEnabled:
      return "kEnabled";
    case OsIntegrationState::kDisabled:
      return "kDisabled";
  }
}

}  // namespace

WebApp::WebApp(const AppId& app_id)
    : app_id_(app_id),
      chromeos_data_(IsChromeOsDataMandatory()
                         ? absl::make_optional<WebAppChromeOsData>()
                         : absl::nullopt) {}

WebApp::~WebApp() = default;

WebApp::WebApp(const WebApp& web_app) = default;

WebApp& WebApp::operator=(WebApp&& web_app) = default;

const SortedSizesPx& WebApp::downloaded_icon_sizes(IconPurpose purpose) const {
  switch (purpose) {
    case IconPurpose::ANY:
      return downloaded_icon_sizes_any_;
    case IconPurpose::MONOCHROME:
      return downloaded_icon_sizes_monochrome_;
    case IconPurpose::MASKABLE:
      return downloaded_icon_sizes_maskable_;
  }
}

void WebApp::AddSource(WebAppManagement::Type source) {
  sources_[source] = true;
}

void WebApp::RemoveSource(WebAppManagement::Type source) {
  sources_[source] = false;
  management_to_external_config_map_.erase(source);
}

bool WebApp::HasAnySources() const {
  return sources_.any();
}

bool WebApp::HasOnlySource(WebAppManagement::Type source) const {
  WebAppSources specified_sources;
  specified_sources[source] = true;
  return HasAnySpecifiedSourcesAndNoOtherSources(sources_, specified_sources);
}

WebAppSources WebApp::GetSources() const {
  return sources_;
}

bool WebApp::IsSynced() const {
  return sources_[WebAppManagement::kSync];
}

bool WebApp::IsPreinstalledApp() const {
  return sources_[WebAppManagement::kDefault];
}

bool WebApp::IsPolicyInstalledApp() const {
  return sources_[WebAppManagement::kPolicy];
}

bool WebApp::IsSystemApp() const {
  return sources_[WebAppManagement::kSystem];
}

bool WebApp::IsWebAppStoreInstalledApp() const {
  return sources_[WebAppManagement::kWebAppStore];
}

bool WebApp::IsSubAppInstalledApp() const {
  return sources_[WebAppManagement::kSubApp];
}

bool WebApp::IsKioskInstalledApp() const {
  return sources_[WebAppManagement::kKiosk];
}

bool WebApp::CanUserUninstallWebApp() const {
  return web_app::CanUserUninstallWebApp(sources_);
}

bool WebApp::WasInstalledByUser() const {
  return sources_[WebAppManagement::kSync] ||
         sources_[WebAppManagement::kWebAppStore];
}

WebAppManagement::Type WebApp::GetHighestPrioritySource() const {
  // Enumerators in Source enum are declaretd in the order of priority.
  // Top priority sources are declared first.
  for (int i = WebAppManagement::kMinValue; i <= WebAppManagement::kMaxValue;
       ++i) {
    auto source = static_cast<WebAppManagement::Type>(i);
    if (sources_[source])
      return source;
  }

  NOTREACHED();
  return WebAppManagement::kMaxValue;
}

void WebApp::SetName(const std::string& name) {
  name_ = name;
}

void WebApp::SetDescription(const std::string& description) {
  description_ = description;
}

void WebApp::SetStartUrl(const GURL& start_url) {
  DCHECK(start_url.is_valid());
  start_url_ = start_url;
}

void WebApp::SetScope(const GURL& scope) {
  DCHECK(scope.is_empty() || scope.is_valid());
  scope_ = scope;
}

void WebApp::SetThemeColor(absl::optional<SkColor> theme_color) {
  theme_color_ = theme_color;
}

void WebApp::SetDarkModeThemeColor(
    absl::optional<SkColor> dark_mode_theme_color) {
  dark_mode_theme_color_ = dark_mode_theme_color;
}

void WebApp::SetBackgroundColor(absl::optional<SkColor> background_color) {
  background_color_ = background_color;
}

void WebApp::SetDarkModeBackgroundColor(
    absl::optional<SkColor> dark_mode_background_color) {
  dark_mode_background_color_ = dark_mode_background_color;
}

void WebApp::SetDisplayMode(DisplayMode display_mode) {
  DCHECK_NE(DisplayMode::kUndefined, display_mode);
  display_mode_ = display_mode;
}

void WebApp::SetUserDisplayMode(UserDisplayMode user_display_mode) {
  user_display_mode_ = user_display_mode;
}

void WebApp::SetDisplayModeOverride(
    std::vector<DisplayMode> display_mode_override) {
  display_mode_override_ = std::move(display_mode_override);
}

void WebApp::SetUserPageOrdinal(syncer::StringOrdinal page_ordinal) {
  user_page_ordinal_ = std::move(page_ordinal);
}

void WebApp::SetUserLaunchOrdinal(syncer::StringOrdinal launch_ordinal) {
  user_launch_ordinal_ = std::move(launch_ordinal);
}

void WebApp::SetWebAppChromeOsData(
    absl::optional<WebAppChromeOsData> chromeos_data) {
  chromeos_data_ = std::move(chromeos_data);
}

void WebApp::SetIsLocallyInstalled(bool is_locally_installed) {
  is_locally_installed_ = is_locally_installed;
}

void WebApp::SetIsFromSyncAndPendingInstallation(
    bool is_from_sync_and_pending_installation) {
  is_from_sync_and_pending_installation_ =
      is_from_sync_and_pending_installation;
}

void WebApp::SetIsUninstalling(bool is_uninstalling) {
  is_uninstalling_ = is_uninstalling;
}

void WebApp::SetManifestIcons(std::vector<apps::IconInfo> manifest_icons) {
  manifest_icons_ = std::move(manifest_icons);
}

void WebApp::SetDownloadedIconSizes(IconPurpose purpose, SortedSizesPx sizes) {
  switch (purpose) {
    case IconPurpose::ANY:
      downloaded_icon_sizes_any_ = std::move(sizes);
      break;
    case IconPurpose::MONOCHROME:
      downloaded_icon_sizes_monochrome_ = std::move(sizes);
      break;
    case IconPurpose::MASKABLE:
      downloaded_icon_sizes_maskable_ = std::move(sizes);
      break;
  }
}

void WebApp::SetIsGeneratedIcon(bool is_generated_icon) {
  is_generated_icon_ = is_generated_icon;
}

void WebApp::SetFileHandlers(apps::FileHandlers file_handlers) {
  file_handlers_ = std::move(file_handlers);
}

void WebApp::SetFileHandlerApprovalState(ApiApprovalState approval_state) {
  file_handler_approval_state_ = approval_state;
}

void WebApp::SetFileHandlerOsIntegrationState(OsIntegrationState state) {
  file_handler_os_integration_state_ = state;
}

void WebApp::SetShareTarget(absl::optional<apps::ShareTarget> share_target) {
  share_target_ = std::move(share_target);
}

void WebApp::SetAdditionalSearchTerms(
    std::vector<std::string> additional_search_terms) {
  additional_search_terms_ = std::move(additional_search_terms);
}

void WebApp::SetProtocolHandlers(
    std::vector<apps::ProtocolHandlerInfo> handlers) {
  protocol_handlers_ = std::move(handlers);
}

void WebApp::SetAllowedLaunchProtocols(
    base::flat_set<std::string> allowed_launch_protocols) {
  allowed_launch_protocols_ = std::move(allowed_launch_protocols);
}

void WebApp::SetDisallowedLaunchProtocols(
    base::flat_set<std::string> disallowed_launch_protocols) {
  disallowed_launch_protocols_ = std::move(disallowed_launch_protocols);
}

void WebApp::SetUrlHandlers(apps::UrlHandlers url_handlers) {
  url_handlers_ = std::move(url_handlers);
}

void WebApp::SetLockScreenStartUrl(const GURL& lock_screen_start_url) {
  DCHECK(lock_screen_start_url.is_empty() || lock_screen_start_url.is_valid());
  lock_screen_start_url_ = lock_screen_start_url;
}

void WebApp::SetNoteTakingNewNoteUrl(const GURL& note_taking_new_note_url) {
  DCHECK(note_taking_new_note_url.is_empty() ||
         note_taking_new_note_url.is_valid());
  note_taking_new_note_url_ = note_taking_new_note_url;
}

void WebApp::SetShortcutsMenuItemInfos(
    std::vector<WebAppShortcutsMenuItemInfo> shortcuts_menu_item_infos) {
  shortcuts_menu_item_infos_ = std::move(shortcuts_menu_item_infos);
}

void WebApp::SetDownloadedShortcutsMenuIconsSizes(
    std::vector<IconSizes> sizes) {
  downloaded_shortcuts_menu_icons_sizes_ = std::move(sizes);
}

void WebApp::SetLastBadgingTime(const base::Time& time) {
  last_badging_time_ = time;
}

void WebApp::SetLastLaunchTime(const base::Time& time) {
  last_launch_time_ = time;
}

void WebApp::SetInstallTime(const base::Time& time) {
  install_time_ = time;
}

void WebApp::SetManifestUpdateTime(const base::Time& time) {
  manifest_update_time_ = time;
}

void WebApp::SetRunOnOsLoginMode(RunOnOsLoginMode mode) {
  run_on_os_login_mode_ = mode;
}

void WebApp::SetRunOnOsLoginOsIntegrationState(RunOnOsLoginMode state) {
  run_on_os_login_os_integration_state_ = state;
}

void WebApp::SetSyncFallbackData(SyncFallbackData sync_fallback_data) {
  sync_fallback_data_ = std::move(sync_fallback_data);
}

void WebApp::SetCaptureLinks(blink::mojom::CaptureLinks capture_links) {
  capture_links_ = capture_links;
}

void WebApp::SetLaunchQueryParams(
    absl::optional<std::string> launch_query_params) {
  launch_query_params_ = std::move(launch_query_params);
}

void WebApp::SetManifestUrl(const GURL& manifest_url) {
  manifest_url_ = manifest_url;
}

void WebApp::SetManifestId(const absl::optional<std::string>& manifest_id) {
  manifest_id_ = manifest_id;
}

void WebApp::SetWindowControlsOverlayEnabled(bool enabled) {
  window_controls_overlay_enabled_ = enabled;
}

void WebApp::SetStorageIsolated(bool is_storage_isolated) {
  is_storage_isolated_ = is_storage_isolated;
}

void WebApp::SetLaunchHandler(absl::optional<LaunchHandler> launch_handler) {
  launch_handler_ = std::move(launch_handler);
}

void WebApp::SetParentAppId(const absl::optional<AppId>& parent_app_id) {
  parent_app_id_ = parent_app_id;
}

void WebApp::SetPermissionsPolicy(
    blink::ParsedPermissionsPolicy permissions_policy) {
  permissions_policy_ = std::move(permissions_policy);
}

void WebApp::SetInstallSourceForMetrics(
    absl::optional<webapps::WebappInstallSource> install_source) {
  install_source_for_metrics_ = install_source;
}

void WebApp::SetAppSizeInBytes(absl::optional<int64_t> app_size_in_bytes) {
  app_size_in_bytes_ = app_size_in_bytes;
}

void WebApp::SetDataSizeInBytes(absl::optional<int64_t> data_size_in_bytes) {
  data_size_in_bytes_ = data_size_in_bytes;
}

void WebApp::SetWebAppManagementExternalConfigMap(
    ExternalConfigMap management_to_external_config_map) {
  management_to_external_config_map_ =
      std::move(management_to_external_config_map);
}

void WebApp::SetTabStrip(absl::optional<blink::Manifest::TabStrip> tab_strip) {
  tab_strip_ = std::move(tab_strip);
}

void WebApp::SetCurrentOsIntegrationStates(
    absl::optional<proto::WebAppOsIntegrationState>
        current_os_integration_states) {
  current_os_integration_states_ = std::move(current_os_integration_states);
}

void WebApp::SetIsolationData(IsolationData isolation_data) {
  isolation_data_ = isolation_data;
}

void WebApp::AddPlaceholderInfoToManagementExternalConfigMap(
    WebAppManagement::Type type,
    bool is_placeholder) {
  DCHECK_NE(type, WebAppManagement::Type::kSync);
  management_to_external_config_map_[type].is_placeholder = is_placeholder;
}

void WebApp::AddInstallURLToManagementExternalConfigMap(
    WebAppManagement::Type type,
    GURL install_url) {
  DCHECK_NE(type, WebAppManagement::Type::kSync);
  DCHECK(install_url.is_valid());
  management_to_external_config_map_[type].install_urls.emplace(install_url);
}

void WebApp::AddExternalSourceInformation(WebAppManagement::Type type,
                                          GURL install_url,
                                          bool is_placeholder) {
  AddInstallURLToManagementExternalConfigMap(type, install_url);
  AddPlaceholderInfoToManagementExternalConfigMap(type, is_placeholder);
}

bool WebApp::RemoveInstallUrlForSource(WebAppManagement::Type type,
                                       GURL install_url) {
  if (!management_to_external_config_map_.count(type))
    return false;

  bool removed =
      management_to_external_config_map_[type].install_urls.erase(install_url);
  if (management_to_external_config_map_[type].install_urls.empty()) {
    management_to_external_config_map_.erase(type);
  }
  return removed;
}

void WebApp::SetAlwaysShowToolbarInFullscreen(bool show) {
  always_show_toolbar_in_fullscreen_ = show;
}

WebApp::ClientData::ClientData() = default;

WebApp::ClientData::~ClientData() = default;

WebApp::ClientData::ClientData(const ClientData& client_data) = default;

base::Value WebApp::ClientData::AsDebugValue() const {
  base::Value root(base::Value::Type::DICTIONARY);
  root.SetKey("system_web_app_data", system_web_app_data
                                         ? system_web_app_data->AsDebugValue()
                                         : base::Value());
  return root;
}

WebApp::SyncFallbackData::SyncFallbackData() = default;

WebApp::SyncFallbackData::~SyncFallbackData() = default;

WebApp::SyncFallbackData::SyncFallbackData(
    const SyncFallbackData& sync_fallback_data) = default;

WebApp::SyncFallbackData::SyncFallbackData(
    SyncFallbackData&& sync_fallback_data) noexcept = default;

WebApp::SyncFallbackData& WebApp::SyncFallbackData::operator=(
    SyncFallbackData&& sync_fallback_data) = default;

base::Value WebApp::SyncFallbackData::AsDebugValue() const {
  base::Value root(base::Value::Type::DICTIONARY);
  root.SetStringKey("name", name);
  root.SetStringKey("theme_color", ColorToString(theme_color));
  root.SetStringKey("scope", scope.spec());
  base::Value& manifest_icons_json =
      *root.SetKey("manifest_icons", base::Value(base::Value::Type::LIST));
  for (const apps::IconInfo& icon_info : icon_infos)
    manifest_icons_json.Append(icon_info.AsDebugValue());
  return root;
}

WebApp::ExternalManagementConfig::ExternalManagementConfig() = default;

WebApp::ExternalManagementConfig::~ExternalManagementConfig() = default;

WebApp::ExternalManagementConfig::ExternalManagementConfig(
    const ExternalManagementConfig& external_management_config) = default;

WebApp::ExternalManagementConfig& WebApp::ExternalManagementConfig::operator=(
    ExternalManagementConfig&& external_management_config) = default;

base::Value::Dict WebApp::ExternalManagementConfig::AsDebugValue() const {
  base::Value::Dict root;
  base::Value::List urls;
  for (auto it : install_urls) {
    urls.Append(it.spec());
  }
  root.Set("install_urls", std::move(urls));
  root.Set("is_placeholder", is_placeholder);
  return root;
}

bool WebApp::operator==(const WebApp& other) const {
  auto AsTuple = [](const WebApp& app) {
    // Keep in order declared in web_app.h.
    return std::make_tuple(
        // Disable clang-format so diffs are clearer when fields are added.
        // clang-format off
        app.app_id_,
        app.sources_,
        app.name_,
        app.description_,
        app.start_url_,
        app.launch_query_params_,
        app.scope_,
        app.theme_color_,
        app.dark_mode_theme_color_,
        app.background_color_,
        app.dark_mode_background_color_,
        app.display_mode_,
        app.user_display_mode_,
        app.display_mode_override_,
        app.user_page_ordinal_,
        app.user_launch_ordinal_,
        app.chromeos_data_,
        app.is_locally_installed_,
        app.is_from_sync_and_pending_installation_,
        app.is_uninstalling_,
        app.manifest_icons_,
        app.downloaded_icon_sizes_any_,
        app.downloaded_icon_sizes_monochrome_,
        app.downloaded_icon_sizes_maskable_,
        app.is_generated_icon_,
        app.shortcuts_menu_item_infos_,
        app.downloaded_shortcuts_menu_icons_sizes_,
        app.file_handlers_,
        app.share_target_,
        app.additional_search_terms_,
        app.protocol_handlers_,
        app.allowed_launch_protocols_,
        app.disallowed_launch_protocols_,
        app.url_handlers_,
        app.lock_screen_start_url_,
        app.note_taking_new_note_url_,
        app.last_badging_time_,
        app.last_launch_time_,
        app.install_time_,
        app.manifest_update_time_,
        app.run_on_os_login_mode_,
        app.run_on_os_login_os_integration_state_,
        app.sync_fallback_data_,
        app.capture_links_,
        app.manifest_url_,
        app.manifest_id_,
        app.client_data_.system_web_app_data,
        app.file_handler_approval_state_,
        app.file_handler_os_integration_state_,
        app.window_controls_overlay_enabled_,
        app.is_storage_isolated_,
        app.launch_handler_,
        app.parent_app_id_,
        app.permissions_policy_,
        app.install_source_for_metrics_,
        app.app_size_in_bytes_,
        app.data_size_in_bytes_,
        app.management_to_external_config_map_,
        app.tab_strip_,
        app.always_show_toolbar_in_fullscreen_,
        app.current_os_integration_states_.value_or(proto::WebAppOsIntegrationState()).SerializeAsString(),
        app.isolation_data_
        // clang-format on
    );
  };
  return (AsTuple(*this) == AsTuple(other));
}

bool WebApp::operator!=(const WebApp& other) const {
  return !(*this == other);
}

base::Value WebApp::AsDebugValue() const {
  base::Value root(base::Value::Type::DICTIONARY);

  auto ConvertList = [](const auto& list) {
    base::Value list_json(base::Value::Type::LIST);
    for (const auto& item : list)
      list_json.Append(item);
    return list_json;
  };

  auto ConvertDebugValueList = [](const auto& list) {
    base::Value list_json(base::Value::Type::LIST);
    for (const auto& item : list)
      list_json.Append(item.AsDebugValue());
    return list_json;
  };

  auto ConvertOptional = [](const auto& value) {
    return value ? base::Value(*value) : base::Value();
  };

  // Prefix with a ! so these fields appear at the top when serialized.
  root.SetStringKey("!app_id", app_id_);

  root.SetStringKey("!name", name_);

  root.SetKey("additional_search_terms", ConvertList(additional_search_terms_));

  root.SetStringKey("app_service_icon_url",
                    base::StrCat({"chrome://app-icon/", app_id_, "/32"}));

  if (app_size_in_bytes_.has_value()) {
    root.SetStringKey("app_size_in_bytes",
                      base::NumberToString(app_size_in_bytes_.value()));
  } else {
    root.SetStringKey("app_size_in_bytes", "");
  }

  root.SetKey("allowed_launch_protocols",
              ConvertList(allowed_launch_protocols_));

  if (data_size_in_bytes_.has_value()) {
    root.SetStringKey("data_size_in_bytes",
                      base::NumberToString(data_size_in_bytes_.value()));
  } else {
    root.SetStringKey("data_size_in_bytes", "");
  }

  root.SetKey("disallowed_launch_protocols",
              ConvertList(disallowed_launch_protocols_));

  root.SetStringKey("background_color", ColorToString(background_color_));

  root.SetStringKey("dark_mode_theme_color",
                    ColorToString(dark_mode_theme_color_));

  root.SetStringKey("dark_mode_background_color",
                    ColorToString(dark_mode_background_color_));

  root.SetStringKey("capture_links", base::StreamableToString(capture_links_));

  root.SetKey("chromeos_data",
              chromeos_data_ ? chromeos_data_->AsDebugValue() : base::Value());

  root.SetKey("client_data", client_data_.AsDebugValue());

  if (data_size_in_bytes_.has_value()) {
    root.SetStringKey("data_size_in_bytes",
                      base::NumberToString(data_size_in_bytes_.value()));
  } else {
    root.SetStringKey("data_size_in_bytes", "");
  }

  root.SetStringKey("description", description_);

  root.SetStringKey("display_mode", blink::DisplayModeToString(display_mode_));

  base::Value& display_override =
      *root.SetKey("display_override", base::Value(base::Value::Type::LIST));
  for (const DisplayMode& mode : display_mode_override_)
    display_override.Append(blink::DisplayModeToString(mode));

  base::Value& downloaded_icon_sizes_json = *root.SetKey(
      "downloaded_icon_sizes", base::Value(base::Value::Type::DICTIONARY));
  for (IconPurpose purpose : kIconPurposes) {
    downloaded_icon_sizes_json.SetKey(
        base::StreamableToString(purpose),
        ConvertList(downloaded_icon_sizes(purpose)));
  }

  base::Value& downloaded_shortcuts_menu_icons_sizes =
      *root.SetKey("downloaded_shortcuts_menu_icons_sizes",
                   base::Value(base::Value::Type::LIST));
  for (size_t i = 0; i < downloaded_shortcuts_menu_icons_sizes_.size(); ++i) {
    const IconSizes& icon_sizes = downloaded_shortcuts_menu_icons_sizes_[i];
    base::Value entry(base::Value::Type::DICTIONARY);
    entry.SetIntKey("index", i);
    for (IconPurpose purpose : kIconPurposes) {
      entry.SetKey(base::StreamableToString(purpose),
                   ConvertList(icon_sizes.GetSizesForPurpose(purpose)));
    }
    downloaded_shortcuts_menu_icons_sizes.Append(std::move(entry));
  }

  root.SetStringKey("file_handler_approval_state",
                    ApiApprovalStateToString(file_handler_approval_state_));

  root.SetStringKey(
      "file_handler_os_integration_state",
      OsIntegrationStateToString(file_handler_os_integration_state_));

  root.SetKey("file_handlers", ConvertDebugValueList(file_handlers_));

  root.SetKey("manifest_icons", ConvertDebugValueList(manifest_icons_));

  if (install_source_for_metrics_) {
    root.SetIntKey("install_source_for_metrics",
                   static_cast<int>(*install_source_for_metrics_));
  } else {
    root.SetStringKey("install_source_for_metrics", "not set");
  }

  base::Value::Dict external_map;
  for (auto it : management_to_external_config_map_) {
    external_map.Set(base::StreamableToString(it.first),
                     it.second.AsDebugValue());
  }

  root.SetKey("management_type_to_external_configuration_map",
              base::Value(std::move(external_map)));

  root.SetStringKey("install_time", base::StreamableToString(install_time_));

  root.SetBoolKey("is_generated_icon", is_generated_icon_);

  root.SetBoolKey("is_from_sync_and_pending_installation",
                  is_from_sync_and_pending_installation_);

  root.SetBoolKey("is_locally_installed", is_locally_installed_);

  root.SetBoolKey("is_storage_isolated", is_storage_isolated_);

  root.SetBoolKey("is_uninstalling", is_uninstalling_);

  root.SetStringKey("last_badging_time",
                    base::StreamableToString(last_badging_time_));

  root.SetStringKey("last_launch_time",
                    base::StreamableToString(last_launch_time_));

  if (launch_handler_) {
    base::Value& launch_handler_json = *root.SetKey(
        "launch_handler", base::Value(base::Value::Type::DICTIONARY));
    launch_handler_json.SetStringKey(
        "client_mode", base::StreamableToString(launch_handler_->client_mode));
  } else {
    root.SetKey("launch_handler", base::Value());
  }

  root.SetKey("launch_query_params", ConvertOptional(launch_query_params_));

  root.SetKey("manifest_id", ConvertOptional(manifest_id_));

  root.SetStringKey("manifest_update_time",
                    base::StreamableToString(manifest_update_time_));

  root.SetStringKey("manifest_url", base::StreamableToString(manifest_url_));

  root.SetStringKey("lock_screen_start_url",
                    base::StreamableToString(lock_screen_start_url_));

  root.SetStringKey("note_taking_new_note_url",
                    base::StreamableToString(note_taking_new_note_url_));

  root.SetStringKey("parent_app_id",
                    parent_app_id_ ? *parent_app_id_ : AppId());

  if (!permissions_policy_.empty()) {
    base::Value& policy_list = *root.SetKey(
        "permissions_policy", base::Value(base::Value::Type::LIST));
    const auto& feature_to_name_map =
        blink::GetPermissionsPolicyFeatureToNameMap();
    for (const auto& decl : permissions_policy_) {
      base::Value json_decl(base::Value::Type::DICTIONARY);
      const auto& feature_name = feature_to_name_map.find(decl.feature);
      if (feature_name == feature_to_name_map.end()) {
        continue;
      }
      json_decl.SetStringKey("feature", feature_name->second);
      base::Value& allowlist_json = *json_decl.SetKey(
          "allowed_origins", base::Value(base::Value::Type::LIST));
      for (const auto& origin_with_possible_wildcards : decl.allowed_origins) {
        allowlist_json.Append(origin_with_possible_wildcards.Serialize());
      }
      json_decl.SetBoolKey("matches_all_origins", decl.matches_all_origins);
      json_decl.SetBoolKey("matches_opaque_src", decl.matches_opaque_src);
      policy_list.Append(std::move(json_decl));
    }
  }

  root.SetKey("protocol_handlers", ConvertDebugValueList(protocol_handlers_));

  root.SetStringKey("run_on_os_login_mode",
                    RunOnOsLoginModeToString(run_on_os_login_mode_));
  root.SetStringKey(
      "run_on_os_login_os_integration_state",
      run_on_os_login_os_integration_state_
          ? RunOnOsLoginModeToString(*run_on_os_login_os_integration_state_)
          : "not set");

  root.SetStringKey("scope", base::StreamableToString(scope_));

  root.SetKey("share_target",
              share_target_ ? share_target_->AsDebugValue() : base::Value());

  root.SetKey("shortcuts_menu_item_infos",
              ConvertDebugValueList(shortcuts_menu_item_infos_));

  base::Value& sources =
      *root.SetKey("sources", base::Value(base::Value::Type::LIST));
  for (int i = WebAppManagement::Type::kMinValue;
       i <= WebAppManagement::Type::kMaxValue; ++i) {
    if (sources_[i]) {
      sources.Append(
          base::StreamableToString(static_cast<WebAppManagement::Type>(i)));
    }
  }

  root.SetStringKey("start_url", base::StreamableToString(start_url_));

  root.SetKey("sync_fallback_data", sync_fallback_data_.AsDebugValue());

  root.SetStringKey("theme_color", ColorToString(theme_color_));

  root.SetStringKey("unhashed_app_id",
                    GenerateAppIdUnhashed(manifest_id_, start_url_));

  root.SetKey("url_handlers", ConvertDebugValueList(url_handlers_));

  root.SetStringKey("user_display_mode",
                    user_display_mode_.has_value()
                        ? ConvertUserDisplayModeToString(*user_display_mode_)
                        : "");

  root.SetStringKey("user_launch_ordinal",
                    user_launch_ordinal_.ToDebugString());

  root.SetStringKey("user_page_ordinal", user_page_ordinal_.ToDebugString());

  root.SetBoolKey("window_controls_overlay_enabled",
                  window_controls_overlay_enabled_);

  if (tab_strip_.has_value()) {
    base::Value& tab_strip_json =
        *root.SetKey("tab_strip", base::Value(base::Value::Type::DICTIONARY));
    if (absl::holds_alternative<TabStrip::Visibility>(
            tab_strip_.value().new_tab_button)) {
      tab_strip_json.SetStringKey(
          "new_tab_button",
          base::StreamableToString(absl::get<TabStrip::Visibility>(
              tab_strip_.value().new_tab_button)));
    } else {
      base::Value& new_tab_button_json = *tab_strip_json.SetKey(
          "new_tab_button", base::Value(base::Value::Type::DICTIONARY));
      new_tab_button_json.SetStringKey(
          "url", base::StreamableToString(
                     absl::get<blink::Manifest::NewTabButtonParams>(
                         tab_strip_.value().new_tab_button)
                         .url.value_or(GURL(""))));
    }

    if (absl::holds_alternative<TabStrip::Visibility>(
            tab_strip_.value().home_tab)) {
      tab_strip_json.SetStringKey(
          "home_tab", base::StreamableToString(absl::get<TabStrip::Visibility>(
                          tab_strip_.value().home_tab)));
    } else {
      tab_strip_json.SetKey("home_tab",
                            base::Value(base::Value::Type::DICTIONARY));
      // TODO(crbug.com/897314): Add debug info for home tab icons.
    }
  } else {
    root.SetKey("tab_strip", base::Value());
  }

  root.SetBoolKey("always_show_toolbar_in_fullscreen",
                  always_show_toolbar_in_fullscreen_);

  if (current_os_integration_states_.has_value()) {
    root.SetKey("current_os_integration_states", base::Value());
    // TODO(crbug.com/1295044) : Add logic to parse and show data.
  }

  if (isolation_data_.has_value()) {
    root.SetKey("isolation_data", isolation_data_->AsDebugValue());
  }

  return root;
}

std::ostream& operator<<(std::ostream& out, const WebApp& app) {
  return out << app.AsDebugValue();
}

bool operator==(const WebApp::SyncFallbackData& sync_fallback_data1,
                const WebApp::SyncFallbackData& sync_fallback_data2) {
  return std::tie(sync_fallback_data1.name, sync_fallback_data1.theme_color,
                  sync_fallback_data1.scope, sync_fallback_data1.icon_infos) ==
         std::tie(sync_fallback_data2.name, sync_fallback_data2.theme_color,
                  sync_fallback_data2.scope, sync_fallback_data2.icon_infos);
}

bool operator!=(const WebApp::SyncFallbackData& sync_fallback_data1,
                const WebApp::SyncFallbackData& sync_fallback_data2) {
  return !(sync_fallback_data1 == sync_fallback_data2);
}

bool operator==(const WebApp::ExternalManagementConfig& management_config1,
                const WebApp::ExternalManagementConfig& management_config2) {
  return management_config1.install_urls == management_config2.install_urls &&
         management_config1.is_placeholder == management_config2.is_placeholder;
}

bool operator!=(const WebApp::ExternalManagementConfig& management_config1,
                const WebApp::ExternalManagementConfig& management_config2) {
  return !(management_config1 == management_config2);
}

}  // namespace web_app
