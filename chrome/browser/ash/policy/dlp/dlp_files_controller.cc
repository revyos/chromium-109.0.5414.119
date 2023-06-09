// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/policy/dlp/dlp_files_controller.h"

#include <sys/types.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "ash/public/cpp/new_window_delegate.h"
#include "base/bind.h"
#include "base/check.h"
#include "base/containers/contains.h"
#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/ranges/algorithm.h"
#include "base/strings/strcat.h"
#include "base/time/time.h"
#include "chrome/browser/apps/app_service/file_utils.h"
#include "chrome/browser/ash/drive/drive_integration_service.h"
#include "chrome/browser/ash/extensions/file_manager/system_notification_manager.h"
#include "chrome/browser/ash/file_manager/fileapi_util.h"
#include "chrome/browser/ash/file_manager/path_util.h"
#include "chrome/browser/ash/policy/dlp/dlp_files_event_storage.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_confidential_file.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_histogram_helper.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_policy_constants.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_reporting_manager.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_rules_manager.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_rules_manager_factory.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_warn_dialog.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_warn_notifier.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chromeos/dbus/dlp/dlp_client.h"
#include "chromeos/dbus/dlp/dlp_service.pb.h"
#include "chromeos/ui/base/file_icon_util.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/common/constants.h"
#include "storage/browser/file_system/file_system_context.h"
#include "storage/browser/file_system/file_system_url.h"
#include "storage/browser/file_system/recursive_operation_delegate.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace policy {

namespace {

// Timeout defining when two events having the same properties are considered
// duplicates.
// TODO(crbug.com/1368982): determine the value to use.
constexpr base::TimeDelta kCooldownTimeout = base::Seconds(5);

// The maximum number of entries that can be kept in the
// DlpFilesEventStorage.
// TODO(crbug.com/1366299): determine the value to use.
constexpr size_t kEntriesLimit = 100;

constexpr char kUploadBlockedNotificationId[] = "upload_dlp_blocked";
constexpr char kDownloadBlockedNotificationId[] = "download_dlp_blocked";

// FileSystemContext instance set for testing.
storage::FileSystemContext* g_file_system_context_for_testing = nullptr;

absl::optional<ino64_t> GetInodeValue(const base::FilePath& path) {
  struct stat file_stats;
  if (stat(path.value().c_str(), &file_stats) != 0)
    return absl::nullopt;
  return file_stats.st_ino;
}

std::vector<absl::optional<ino64_t>> GetFilesInodes(
    const std::vector<storage::FileSystemURL>& files) {
  std::vector<absl::optional<ino64_t>> inodes;
  for (const auto& file : files) {
    inodes.push_back(GetInodeValue(file.path()));
  }
  return inodes;
}

// Maps |file_path| to DlpRulesManager::Component if possible.
absl::optional<DlpRulesManager::Component> MapFilePathtoPolicyComponent(
    Profile* profile,
    const base::FilePath file_path) {
  if (base::FilePath(file_manager::util::GetAndroidFilesPath())
          .IsParent(file_path)) {
    return DlpRulesManager::Component::kArc;
  }

  if (base::FilePath(file_manager::util::kRemovableMediaPath)
          .IsParent(file_path)) {
    return DlpRulesManager::Component::kUsb;
  }

  drive::DriveIntegrationService* integration_service =
      drive::DriveIntegrationServiceFactory::FindForProfile(profile);
  if (integration_service && integration_service->is_enabled() &&
      integration_service->GetMountPointPath().IsParent(file_path)) {
    return DlpRulesManager::Component::kDrive;
  }

  base::FilePath linux_files =
      file_manager::util::GetCrostiniMountDirectory(profile);
  if (linux_files == file_path || linux_files.IsParent(file_path)) {
    return DlpRulesManager::Component::kCrostini;
  }

  return {};
}

// Maps |component| to DlpRulesManager::Component.
DlpRulesManager::Component MapProtoToPolicyComponent(
    ::dlp::DlpComponent component) {
  switch (component) {
    case ::dlp::DlpComponent::ARC:
      return DlpRulesManager::Component::kArc;
    case ::dlp::DlpComponent::CROSTINI:
      return DlpRulesManager::Component::kCrostini;
    case ::dlp::DlpComponent::PLUGIN_VM:
      return DlpRulesManager::Component::kPluginVm;
    case ::dlp::DlpComponent::USB:
      return DlpRulesManager::Component::kUsb;
    case ::dlp::DlpComponent::GOOGLE_DRIVE:
      return DlpRulesManager::Component::kDrive;
    case ::dlp::DlpComponent::UNKOWN_COMPONENT:
    case ::dlp::DlpComponent::SYSTEM:
      return DlpRulesManager::Component::kUnknownComponent;
  }
}

// Returns |g_file_system_context_for_testing| if set, otherwise
// it returns FileSystemContext* for the primary profile.
storage::FileSystemContext* GetFileSystemContext() {
  if (g_file_system_context_for_testing)
    return g_file_system_context_for_testing;

  auto* primary_profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(primary_profile);
  return file_manager::util::GetFileManagerFileSystemContext(primary_profile);
}

// Gets all files inside |root| recursively and runs |callback_| with the
// files list.
class FolderRecursionDelegate : public storage::RecursiveOperationDelegate {
 public:
  using FileURLsCallback =
      base::OnceCallback<void(std::vector<storage::FileSystemURL>)>;

  FolderRecursionDelegate(storage::FileSystemContext* file_system_context,
                          const storage::FileSystemURL& root,
                          FileURLsCallback callback)
      : RecursiveOperationDelegate(file_system_context),
        root_(root),
        callback_(std::move(callback)) {}

  FolderRecursionDelegate(const FolderRecursionDelegate&) = delete;
  FolderRecursionDelegate& operator=(const FolderRecursionDelegate&) = delete;

  ~FolderRecursionDelegate() override = default;

  // RecursiveOperationDelegate:
  void Run() override { NOTREACHED(); }
  void RunRecursively() override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    StartRecursiveOperation(root_,
                            storage::FileSystemOperation::ERROR_BEHAVIOR_SKIP,
                            base::BindOnce(&FolderRecursionDelegate::Completed,
                                           weak_ptr_factory_.GetWeakPtr()));
  }
  void ProcessFile(const storage::FileSystemURL& url,
                   StatusCallback callback) override {
    file_system_context()->operation_runner()->GetMetadata(
        url, storage::FileSystemOperation::GET_METADATA_FIELD_IS_DIRECTORY,
        base::BindOnce(&FolderRecursionDelegate::OnGetMetadata,
                       weak_ptr_factory_.GetWeakPtr(), url,
                       std::move(callback)));
  }
  void ProcessDirectory(const storage::FileSystemURL& url,
                        StatusCallback callback) override {
    std::move(callback).Run(base::File::FILE_OK);
  }
  void PostProcessDirectory(const storage::FileSystemURL& url,
                            StatusCallback callback) override {
    std::move(callback).Run(base::File::FILE_OK);
  }

 private:
  void OnGetMetadata(const storage::FileSystemURL& url,
                     StatusCallback callback,
                     base::File::Error result,
                     const base::File::Info& file_info) {
    if (result != base::File::FILE_OK) {
      std::move(callback).Run(result);
      return;
    }
    if (file_info.is_directory) {
      std::move(callback).Run(base::File::FILE_ERROR_NOT_A_FILE);
      return;
    }
    files_urls_.push_back(url);
    std::move(callback).Run(base::File::FILE_OK);
  }

  void Completed(base::File::Error result) {
    std::move(callback_).Run(std::move(files_urls_));
  }

  const storage::FileSystemURL& root_;
  FileURLsCallback callback_;
  std::vector<storage::FileSystemURL> files_urls_;

  base::WeakPtrFactory<FolderRecursionDelegate> weak_ptr_factory_{this};
};

// Gets all files inside |roots| recursively and runs |callback_| with the
// whole files list. Deletes itself after |callback_| is run.
// TODO(crbug.com/1378202): Extract RootsRecursionDelegate to another file to
// have better testing coverage.
class RootsRecursionDelegate {
 public:
  RootsRecursionDelegate(storage::FileSystemContext* file_system_context,
                         const std::vector<storage::FileSystemURL>& roots,
                         FolderRecursionDelegate::FileURLsCallback callback)
      : file_system_context_(file_system_context),
        roots_(roots),
        callback_(std::move(callback)) {}

  RootsRecursionDelegate(const RootsRecursionDelegate&) = delete;
  RootsRecursionDelegate& operator=(const RootsRecursionDelegate&) = delete;

  ~RootsRecursionDelegate() = default;

  // Starts getting all files inside |roots| recursively.
  void Run() {
    for (const auto& root : roots_) {
      auto recursion_delegate = std::make_unique<FolderRecursionDelegate>(
          file_system_context_, root,
          base::BindOnce(&RootsRecursionDelegate::Completed,
                         weak_ptr_factory_.GetWeakPtr()));
      recursion_delegate->RunRecursively();
      delegates_.push_back(std::move(recursion_delegate));
    }
  }

  // Runs |callback_| when all files are ready.
  void Completed(std::vector<storage::FileSystemURL> files_urls) {
    counter_++;
    files_urls_.insert(std::end(files_urls_), std::begin(files_urls),
                       std::end(files_urls));
    if (counter_ == roots_.size()) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(callback_), std::move(files_urls_)));
      content::GetIOThreadTaskRunner({})->DeleteSoon(FROM_HERE, this);
    }
  }

 private:
  // counts the number of |roots| processed.
  uint counter_ = 0;
  storage::FileSystemContext* file_system_context_ = nullptr;
  const std::vector<storage::FileSystemURL>& roots_;
  FolderRecursionDelegate::FileURLsCallback callback_;
  std::vector<storage::FileSystemURL> files_urls_;
  std::vector<std::unique_ptr<FolderRecursionDelegate>> delegates_;

  base::WeakPtrFactory<RootsRecursionDelegate> weak_ptr_factory_{this};
};

void GotFilesSourcesOfCopy(
    storage::FileSystemURL destination,
    std::vector<DlpFilesController::DlpFileMetadata> metadata) {
  if (metadata.size() == 0) {
    return;
  }
  DCHECK(metadata.size() == 1);
  if (!chromeos::DlpClient::Get() || !chromeos::DlpClient::Get()->IsAlive()) {
    return;
  }

  if (metadata[0].source_url.empty()) {
    return;
  }

  ::dlp::AddFileRequest request;
  request.set_file_path(destination.path().value());
  request.set_source_url(metadata[0].source_url);
  // TODO(https://crbug.com/1368497): we might want to use the callback for
  // error handling
  chromeos::DlpClient::Get()->AddFile(request, base::DoNothing());
}

// Returns an instance of NotificationDisplayService for the primary profile.
NotificationDisplayService* GetNotificationDisplayService() {
  auto* profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(profile);
  auto* display_service =
      NotificationDisplayServiceFactory::GetForProfile(profile);
  DCHECK(display_service);
  return display_service;
}

// Opens DLP Learn more link and closes the notification having
// `notification_id`.
void OnLearnMoreButtonClicked(const std::string& notification_id,
                              absl::optional<int> button_index) {
  if (!button_index || button_index.value() != 0)
    return;

  ash::NewWindowDelegate::GetPrimary()->OpenUrl(
      GURL(dlp::kDlpLearnMoreUrl),
      ash::NewWindowDelegate::OpenUrlFrom::kUserInteraction,
      ash::NewWindowDelegate::Disposition::kNewForegroundTab);

  GetNotificationDisplayService()->Close(NotificationHandler::Type::TRANSIENT,
                                         notification_id);
}

// Shows a system notification having `notification_id`, `title`, and `message`.
void ShowNotification(const std::string& notification_id,
                      const std::u16string& title,
                      const std::u16string& message) {
  auto notification = file_manager::CreateSystemNotification(
      notification_id, std::move(title), std::move(message),
      base::MakeRefCounted<message_center::HandleNotificationClickDelegate>(
          base::BindRepeating(&OnLearnMoreButtonClicked, notification_id)));
  notification->set_buttons(
      {message_center::ButtonInfo(l10n_util::GetStringUTF16(IDS_LEARN_MORE))});

  GetNotificationDisplayService()->Display(NotificationHandler::Type::TRANSIENT,
                                           *notification,
                                           /*metadata=*/nullptr);
}

}  // namespace

DlpFilesController::DlpFileMetadata::DlpFileMetadata(
    const std::string& source_url,
    bool is_dlp_restricted)
    : source_url(source_url), is_dlp_restricted(is_dlp_restricted) {}

DlpFilesController::DlpFileRestrictionDetails::DlpFileRestrictionDetails() =
    default;

DlpFilesController::DlpFileRestrictionDetails::DlpFileRestrictionDetails(
    DlpFileRestrictionDetails&&) = default;
DlpFilesController::DlpFileRestrictionDetails&
DlpFilesController::DlpFileRestrictionDetails::operator=(
    DlpFilesController::DlpFileRestrictionDetails&&) = default;

DlpFilesController::DlpFileRestrictionDetails::~DlpFileRestrictionDetails() =
    default;

DlpFilesController::FileDaemonInfo::FileDaemonInfo(
    ino64_t inode,
    const base::FilePath& path,
    const std::string& source_url)
    : inode(inode), path(path), source_url(source_url) {}
DlpFilesController::DlpFileDestination::DlpFileDestination() = default;
DlpFilesController::DlpFileDestination::DlpFileDestination(
    const std::string& url)
    : url_or_path(url) {}
DlpFilesController::DlpFileDestination::DlpFileDestination(
    const ::dlp::DlpComponent component)
    : component(MapProtoToPolicyComponent(component)) {}
DlpFilesController::DlpFileDestination::DlpFileDestination(
    const DlpRulesManager::Component component)
    : component(component) {}

DlpFilesController::DlpFileDestination::DlpFileDestination(
    const DlpFileDestination&) = default;
DlpFilesController::DlpFileDestination&
DlpFilesController::DlpFileDestination::operator=(const DlpFileDestination&) =
    default;
DlpFilesController::DlpFileDestination::DlpFileDestination(
    DlpFileDestination&&) = default;
DlpFilesController::DlpFileDestination&
DlpFilesController::DlpFileDestination::operator=(DlpFileDestination&&) =
    default;
bool DlpFilesController::DlpFileDestination::operator==(
    const DlpFileDestination& other) const {
  return component == other.component && url_or_path == other.url_or_path;
}
bool DlpFilesController::DlpFileDestination::operator!=(
    const DlpFileDestination& other) const {
  return !(*this == other);
}
bool DlpFilesController::DlpFileDestination::operator<(
    const DlpFileDestination& other) const {
  if (component.has_value() && other.component.has_value()) {
    return static_cast<int>(component.value()) <
           static_cast<int>(other.component.value());
  }
  if (component.has_value()) {
    return true;
  }
  if (other.component.has_value()) {
    return false;
  }
  DCHECK(url_or_path.has_value() && other.url_or_path.has_value());
  return url_or_path.value() < other.url_or_path.value();
}
bool DlpFilesController::DlpFileDestination::operator<=(
    const DlpFileDestination& other) const {
  return *this == other || *this < other;
}
bool DlpFilesController::DlpFileDestination::operator>(
    const DlpFileDestination& other) const {
  return !(*this <= other);
}
bool DlpFilesController::DlpFileDestination::operator>=(
    const DlpFileDestination& other) const {
  return !(*this < other);
}

DlpFilesController::DlpFileDestination::~DlpFileDestination() = default;

DlpFilesController::DlpFilesController(const DlpRulesManager& rules_manager)
    : rules_manager_(rules_manager),
      warn_notifier_(std::make_unique<DlpWarnNotifier>()),
      event_storage_(std::make_unique<DlpFilesEventStorage>(kCooldownTimeout,
                                                            kEntriesLimit)) {}

DlpFilesController::~DlpFilesController() = default;

void DlpFilesController::GetDisallowedTransfers(
    const std::vector<storage::FileSystemURL>& transferred_files,
    storage::FileSystemURL destination,
    bool is_move,
    GetDisallowedTransfersCallback result_callback) {
  if (!chromeos::DlpClient::Get() || !chromeos::DlpClient::Get()->IsAlive()) {
    std::move(result_callback).Run(std::vector<storage::FileSystemURL>());
    return;
  }

  auto* file_system_context = GetFileSystemContext();
  if (!file_system_context) {
    std::move(result_callback).Run(std::vector<storage::FileSystemURL>());
    return;
  }

  auto* roots_recursion_delegate = new RootsRecursionDelegate(
      file_system_context, transferred_files,
      base::BindOnce(&DlpFilesController::OnGetFilesUrls,
                     weak_ptr_factory_.GetWeakPtr(), std::move(destination),
                     is_move, std::move(result_callback)));
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&RootsRecursionDelegate::Run,
                     // base::Unretained() is safe since |recursion_delegate|
                     // will delete itself after all the files list if ready.
                     base::Unretained(roots_recursion_delegate)));
}

void DlpFilesController::CopySourceInformation(
    const storage::FileSystemURL& source,
    const storage::FileSystemURL& destination) {
  auto* profile = ProfileManager::GetPrimaryUserProfile();

  // One path is external component.
  if (MapFilePathtoPolicyComponent(profile, source.path()).has_value() ||
      MapFilePathtoPolicyComponent(profile, destination.path()).has_value()) {
    return;
  }
  GetDlpMetadata({source}, base::BindOnce(&GotFilesSourcesOfCopy, destination));
}

void DlpFilesController::GetDlpMetadata(
    const std::vector<storage::FileSystemURL>& files,
    GetDlpMetadataCallback result_callback) {
  if (!chromeos::DlpClient::Get() || !chromeos::DlpClient::Get()->IsAlive()) {
    std::move(result_callback).Run(std::vector<DlpFileMetadata>());
    return;
  }

  std::vector<absl::optional<ino64_t>> inodes = GetFilesInodes(files);
  ::dlp::GetFilesSourcesRequest request;
  for (const auto& inode : inodes) {
    if (inode.has_value()) {
      request.add_files_inodes(inode.value());
    }
  }
  chromeos::DlpClient::Get()->GetFilesSources(
      request, base::BindOnce(&DlpFilesController::ReturnDlpMetadata,
                              weak_ptr_factory_.GetWeakPtr(), std::move(inodes),
                              std::move(result_callback)));
}

void DlpFilesController::FilterDisallowedUploads(
    std::vector<blink::mojom::FileChooserFileInfoPtr> uploaded_files,
    const GURL& destination,
    FilterDisallowedUploadsCallback result_callback) {
  if (uploaded_files.empty()) {
    std::move(result_callback).Run(std::move(uploaded_files));
    return;
  }

  if (!chromeos::DlpClient::Get() || !chromeos::DlpClient::Get()->IsAlive()) {
    std::move(result_callback).Run(std::move(uploaded_files));
    return;
  }

  ::dlp::CheckFilesTransferRequest request;
  for (const auto& file : uploaded_files) {
    if (file && file->is_native_file())
      request.add_files_paths(file->get_native_file()->file_path.value());
  }
  if (request.files_paths().empty()) {
    std::move(result_callback).Run(std::move(uploaded_files));
    return;
  }

  request.set_destination_url(destination.spec());
  request.set_file_action(::dlp::FileAction::UPLOAD);
  auto return_uploads_callback = base::BindOnce(
      &DlpFilesController::ReturnAllowedUploads, weak_ptr_factory_.GetWeakPtr(),
      std::move(uploaded_files), std::move(result_callback));
  auto close_dialog_callback =
      base::BindOnce(&DlpFilesController::MaybeCloseDialog,
                     // base::Unretained() is safe since |this| is bound to
                     // |return_uploads_callback|, which will be called after
                     // |close_dialog_callback|
                     base::Unretained(this));
  chromeos::DlpClient::Get()->CheckFilesTransfer(
      request, std::move(close_dialog_callback)
                   .Then(std::move(return_uploads_callback)));
}

void DlpFilesController::CheckIfDownloadAllowed(
    const GURL& download_url,
    const base::FilePath& file_path,
    CheckIfDownloadAllowedCallback result_callback) {
  auto* profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(profile);

  auto dst_component =
      MapFilePathtoPolicyComponent(profile, base::FilePath(file_path));
  if (!dst_component.has_value()) {
    // We may block downloads only if saved to external component, otherwise
    // downloads should be allowed.
    std::move(result_callback).Run(true);
    return;
  }

  FileDaemonInfo file_info({}, file_path, download_url.spec());
  IsFilesTransferRestricted(
      {std::move(file_info)}, DlpFileDestination(file_path.value()),
      FileAction::kDownload,
      base::BindOnce(
          [](CheckIfDownloadAllowedCallback result_callback,
             const std::vector<FileDaemonInfo>& restricted_files) {
            bool is_allowed = restricted_files.empty();
            if (!is_allowed) {
              ShowNotification(
                  kDownloadBlockedNotificationId,
                  l10n_util::GetStringUTF16(
                      IDS_POLICY_DLP_FILES_DOWNLOAD_BLOCK_TITLE),
                  l10n_util::GetStringUTF16(
                      IDS_POLICY_DLP_FILES_DOWNLOAD_BLOCK_MESSAGE));
            }
            std::move(result_callback).Run(is_allowed);
          },
          std::move(result_callback)));
}

void DlpFilesController::CheckIfLaunchAllowed(
    const apps::AppUpdate& app_update,
    apps::IntentPtr intent,
    CheckIfLaunchAllowedCallback result_callback) {
  if (intent->files.empty()) {
    std::move(result_callback).Run(/*is_allowed=*/true);
    return;
  }
  auto* profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(profile);
  ::dlp::CheckFilesTransferRequest request;
  for (const auto& file : intent->files) {
    auto file_url = apps::GetFileSystemURL(profile, file->url);
    request.add_files_paths(file_url.path().value());
  }

  request.set_file_action(intent->IsShareIntent() ? ::dlp::FileAction::SHARE
                                                  : ::dlp::FileAction::OPEN);

  switch (app_update.AppType()) {
    case apps::AppType::kStandaloneBrowserChromeApp:
    case apps::AppType::kExtension:
    case apps::AppType::kStandaloneBrowserExtension:
    case apps::AppType::kChromeApp:
      request.set_destination_url(base::StrCat(
          {extensions::kExtensionScheme, "://", app_update.AppId()}));
      break;

    case apps::AppType::kArc:
      request.set_destination_component(::dlp::DlpComponent::ARC);
      break;
    case apps::AppType::kCrostini:
      request.set_destination_component(::dlp::DlpComponent::CROSTINI);
      break;
    case apps::AppType::kPluginVm:
      request.set_destination_component(::dlp::DlpComponent::PLUGIN_VM);
      break;
    case apps::AppType::kWeb:
      request.set_destination_url(app_update.PublisherId());
      break;
    case apps::AppType::kUnknown:
    case apps::AppType::kBuiltIn:
    case apps::AppType::kMacOs:
    case apps::AppType::kStandaloneBrowser:
    case apps::AppType::kRemote:
    case apps::AppType::kBorealis:
    case apps::AppType::kSystemWeb:
      break;
  }
  chromeos::DlpClient::Get()->CheckFilesTransfer(
      request, base::BindOnce(&DlpFilesController::LaunchIfAllowed,
                              weak_ptr_factory_.GetWeakPtr(),
                              std::move(result_callback)));
}

void DlpFilesController::IsFilesTransferRestricted(
    const std::vector<FileDaemonInfo>& transferred_files,
    const DlpFileDestination& destination,
    FileAction files_action,
    IsFilesTransferRestrictedCallback result_callback) {
  policy::DlpRulesManager* dlp_rules_manager =
      policy::DlpRulesManagerFactory::GetForPrimaryProfile();
  if (!dlp_rules_manager) {
    std::move(result_callback).Run(std::vector<FileDaemonInfo>());
    return;
  }
  auto* profile = ProfileManager::GetPrimaryUserProfile();
  DCHECK(profile);
  absl::optional<DlpRulesManager::Component> dst_component;
  if (destination.component.has_value()) {
    dst_component = *destination.component;
  } else {
    DCHECK(destination.url_or_path.has_value());
    dst_component = MapFilePathtoPolicyComponent(
        profile, base::FilePath(*destination.url_or_path));
  }

  DlpFileDestination deduplication_dst;

  std::vector<FileDaemonInfo> restricted_files;
  std::vector<FileDaemonInfo> warned_files;
  std::vector<DlpConfidentialFile> dialog_files;
  absl::optional<std::string> destination_pattern;
  std::vector<std::string> warned_source_patterns;
  for (const auto& file : transferred_files) {
    DlpRulesManager::Level level;
    std::string source_pattern;
    if (dst_component.has_value()) {
      level = rules_manager_.IsRestrictedComponent(
          GURL(file.source_url), dst_component.value(),
          DlpRulesManager::Restriction::kFiles, &source_pattern);
      deduplication_dst = DlpFileDestination(dst_component.value());
      MaybeReportEvent(file.inode, file.path, source_pattern, deduplication_dst,
                       absl::nullopt, level);
    } else {
      // TODO(crbug.com/1286366): Revisit whether passing files paths here
      // make sense.
      DCHECK(destination.url_or_path.has_value());
      destination_pattern = std::string();
      level = rules_manager_.IsRestrictedDestination(
          GURL(file.source_url), GURL(*destination.url_or_path),
          DlpRulesManager::Restriction::kFiles, &source_pattern,
          &destination_pattern.value());
      deduplication_dst = destination;
      MaybeReportEvent(file.inode, file.path, source_pattern, deduplication_dst,
                       destination_pattern, level);
    }

    if (level == DlpRulesManager::Level::kBlock) {
      restricted_files.push_back(file);
      DlpHistogramEnumeration(dlp::kFileActionBlockedUMA, files_action);
    } else if (level == DlpRulesManager::Level::kWarn) {
      warned_files.push_back(file);
      warned_source_patterns.emplace_back(source_pattern);
      if (files_action != FileAction::kDownload) {
        dialog_files.emplace_back(file.path);
      }
      DlpHistogramEnumeration(dlp::kFileActionWarnedUMA, files_action);
    }
  }

  if (warned_files.empty()) {
    std::move(result_callback).Run(std::move(restricted_files));
    return;
  }

  if (warn_dialog_widget_ && !warn_dialog_widget_->IsClosed()) {
    warn_dialog_widget_->CloseWithReason(
        views::Widget::ClosedReason::kUnspecified);
  }

  warn_dialog_widget_ = warn_notifier_->ShowDlpFilesWarningDialog(
      base::BindOnce(&DlpFilesController::OnDlpWarnDialogReply,
                     weak_ptr_factory_.GetWeakPtr(),
                     std::move(restricted_files), std::move(warned_files),
                     std::move(warned_source_patterns),
                     std::move(deduplication_dst), destination_pattern,
                     files_action, std::move(result_callback)),
      std::move(dialog_files), dst_component, destination_pattern,
      files_action);
}

std::vector<DlpFilesController::DlpFileRestrictionDetails>
DlpFilesController::GetDlpRestrictionDetails(const std::string& source_url) {
  const GURL source(source_url);
  const DlpRulesManager::AggregatedDestinations aggregated_destinations =
      rules_manager_.GetAggregatedDestinations(
          source, DlpRulesManager::Restriction::kFiles);
  const DlpRulesManager::AggregatedComponents aggregated_components =
      rules_manager_.GetAggregatedComponents(
          source, DlpRulesManager::Restriction::kFiles);

  std::vector<DlpFilesController::DlpFileRestrictionDetails> result;
  // Add levels for which urls are set.
  for (const auto& [level, urls] : aggregated_destinations) {
    DlpFileRestrictionDetails details;
    details.level = level;
    base::ranges::move(urls.begin(), urls.end(),
                       std::back_inserter(details.urls));
    // Add the components for this level, if any.
    const auto it = aggregated_components.find(level);
    if (it != aggregated_components.end()) {
      base::ranges::move(it->second.begin(), it->second.end(),
                         std::back_inserter(details.components));
    }
    result.emplace_back(std::move(details));
  }

  // There might be levels for which only components are set, so we need to add
  // those separately.
  for (const auto& [level, components] : aggregated_components) {
    if (aggregated_destinations.find(level) != aggregated_destinations.end()) {
      // Already added in the previous loop.
      continue;
    }
    DlpFileRestrictionDetails details;
    details.level = level;
    base::ranges::move(components.begin(), components.end(),
                       std::back_inserter(details.components));
    result.emplace_back(std::move(details));
  }

  return result;
}

std::vector<DlpRulesManager::Component>
DlpFilesController::GetBlockedComponents(const std::string& source_url) {
  const GURL source(source_url);
  const DlpRulesManager::AggregatedComponents aggregated_components =
      rules_manager_.GetAggregatedComponents(
          source, DlpRulesManager::Restriction::kFiles);

  std::vector<DlpRulesManager::Component> result;
  const auto it = aggregated_components.find(DlpRulesManager::Level::kBlock);
  if (it != aggregated_components.end()) {
    base::ranges::move(it->second.begin(), it->second.end(),
                       std::back_inserter(result));
  }
  return result;
}

bool DlpFilesController::IsDlpPolicyMatched(const FileDaemonInfo& file) {
  bool restricted = false;

  std::string src_pattern;

  policy::DlpRulesManager::Level level = rules_manager_.IsRestrictedByAnyRule(
      GURL(file.source_url.spec()),
      policy::DlpRulesManager::Restriction::kFiles, &src_pattern);

  switch (level) {
    case policy::DlpRulesManager::Level::kBlock:
      restricted = true;
      DlpHistogramEnumeration(dlp::kFileActionBlockedUMA, FileAction::kUnknown);
      break;
    case policy::DlpRulesManager::Level::kWarn:
      DlpHistogramEnumeration(dlp::kFileActionWarnedUMA, FileAction::kUnknown);
      // TODO(crbug.com/1172959): Implement Warning mode for Files restriction
      break;
    default:
      break;
  }

  MaybeReportEvent(
      file.inode, file.path, src_pattern,
      DlpFileDestination(DlpRulesManager::Component::kUnknownComponent),
      absl::nullopt, level);

  return restricted;
}

void DlpFilesController::SetWarnNotifierForTesting(
    std::unique_ptr<DlpWarnNotifier> warn_notifier) {
  DCHECK(warn_notifier);
  warn_notifier_ = std::move(warn_notifier);
}

DlpFilesEventStorage* DlpFilesController::GetEventStorageForTesting() {
  return event_storage_.get();
}

void DlpFilesController::SetFileSystemContextForTesting(
    storage::FileSystemContext* file_system_context) {
  g_file_system_context_for_testing = file_system_context;
}

void DlpFilesController::OnDlpWarnDialogReply(
    std::vector<FileDaemonInfo> restricted_files,
    std::vector<FileDaemonInfo> warned_files,
    std::vector<std::string> warned_src_patterns,
    const DlpFileDestination& dst,
    const absl::optional<std::string>& dst_pattern,
    FileAction files_action,
    IsFilesTransferRestrictedCallback callback,
    bool should_proceed) {
  if (!should_proceed) {
    restricted_files.insert(restricted_files.end(),
                            std::make_move_iterator(warned_files.begin()),
                            std::make_move_iterator(warned_files.end()));
  } else {
    DCHECK(warned_files.size() == warned_src_patterns.size());
    for (size_t i = 0; i < warned_files.size(); ++i) {
      DlpHistogramEnumeration(dlp::kFileActionWarnProceededUMA, files_action);
      MaybeReportEvent(warned_files[i].inode, warned_files[i].path,
                       warned_src_patterns[i], dst, dst_pattern, absl::nullopt);
    }
  }
  std::move(callback).Run(std::move(restricted_files));
}

void DlpFilesController::ReturnDisallowedTransfers(
    base::flat_map<std::string, storage::FileSystemURL> files_map,
    GetDisallowedTransfersCallback result_callback,
    ::dlp::CheckFilesTransferResponse response) {
  std::vector<storage::FileSystemURL> restricted_files;
  if (response.has_error_message()) {
    LOG(ERROR) << "Failed to get check files transfer, error: "
               << response.error_message();
    for (const auto& [file_path, file_system_url] : files_map)
      restricted_files.push_back(file_system_url);
    std::move(result_callback).Run(std::move(restricted_files));
    return;
  }
  for (const auto& file : response.files_paths()) {
    DCHECK(files_map.find(file) != files_map.end());
    restricted_files.push_back(files_map.at(file));
  }
  std::move(result_callback).Run(std::move(restricted_files));
}

void DlpFilesController::ReturnAllowedUploads(
    std::vector<blink::mojom::FileChooserFileInfoPtr> uploaded_files,
    FilterDisallowedUploadsCallback result_callback,
    ::dlp::CheckFilesTransferResponse response) {
  if (response.has_error_message()) {
    LOG(ERROR) << "Failed to get check files transfer, error: "
               << response.error_message();
    std::move(result_callback)
        .Run(std::vector<blink::mojom::FileChooserFileInfoPtr>());
    return;
  }
  std::set<std::string> restricted_files(response.files_paths().begin(),
                                         response.files_paths().end());
  if (!restricted_files.empty()) {
    ShowNotification(
        kUploadBlockedNotificationId,
        l10n_util::GetStringUTF16(IDS_POLICY_DLP_FILES_UPLOAD_BLOCK_TITLE),
        l10n_util::GetPluralStringFUTF16(
            IDS_POLICY_DLP_FILES_UPLOAD_BLOCK_MESSAGE,
            restricted_files.size()));
  }
  std::vector<blink::mojom::FileChooserFileInfoPtr> filtered_files;
  for (auto& file : uploaded_files) {
    if (file && file->is_native_file() &&
        base::Contains(restricted_files,
                       file->get_native_file()->file_path.value())) {
      continue;
    }
    filtered_files.push_back(std::move(file));
  }
  std::move(result_callback).Run(std::move(filtered_files));
}

void DlpFilesController::ReturnDlpMetadata(
    std::vector<absl::optional<ino64_t>> inodes,
    GetDlpMetadataCallback result_callback,
    const ::dlp::GetFilesSourcesResponse response) {
  if (response.has_error_message()) {
    LOG(ERROR) << "Failed to get files sources, error: "
               << response.error_message();
  }

  base::flat_map<ino64_t, DlpFileMetadata> metadata_map;
  for (const auto& metadata : response.files_metadata()) {
    DlpRulesManager::Level level = rules_manager_.IsRestrictedByAnyRule(
        GURL(metadata.source_url()), DlpRulesManager::Restriction::kFiles,
        nullptr);
    bool is_dlp_restricted = level != DlpRulesManager::Level::kNotSet &&
                             level != DlpRulesManager::Level::kAllow;
    metadata_map.emplace(
        metadata.inode(),
        DlpFileMetadata(metadata.source_url(), is_dlp_restricted));
  }

  std::vector<DlpFileMetadata> result;
  for (const auto& inode : inodes) {
    if (!inode.has_value()) {
      result.emplace_back("", false);
      continue;
    }
    auto metadata_itr = metadata_map.find(inode.value());
    if (metadata_itr == metadata_map.end()) {
      result.emplace_back("", false);
    } else {
      result.emplace_back(metadata_itr->second);
    }
  }

  std::move(result_callback).Run(std::move(result));
}

void DlpFilesController::LaunchIfAllowed(
    CheckIfLaunchAllowedCallback result_callback,
    ::dlp::CheckFilesTransferResponse response) {
  if (response.has_error_message()) {
    LOG(ERROR) << "Failed to get check files transfer, error: "
               << response.error_message();
    std::move(result_callback).Run(/*is_allowed=*/true);
    return;
  }

  if (!response.files_paths().empty()) {
    // TODO(crbug.com/1382065): Show block notification.
    std::move(result_callback).Run(/*is_allowed=*/false);
    return;
  }
  std::move(result_callback).Run(/*is_allowed=*/true);
}

void DlpFilesController::MaybeReportEvent(
    ino64_t inode,
    const base::FilePath& path,
    const std::string& source_pattern,
    const DlpFileDestination& dst,
    const absl::optional<std::string>& dst_pattern,
    absl::optional<DlpRulesManager::Level> level) {
  const bool is_warning_proceeded_event = !level.has_value();

  if (!is_warning_proceeded_event &&
      (level.value() == DlpRulesManager::Level::kAllow ||
       level.value() == DlpRulesManager::Level::kNotSet)) {
    return;
  }

  DlpReportingManager* reporting_manager = rules_manager_.GetReportingManager();
  if (!reporting_manager) {
    return;
  }

  // Warning proceeded events are always user-initiated since they are triggered
  // only when the user interacts with the warning dialog.
  if (!is_warning_proceeded_event &&
      !event_storage_->StoreEventAndCheckIfItShouldBeReported(inode, dst)) {
    return;
  }

  std::unique_ptr<DlpPolicyEventBuilder> event_builder =
      is_warning_proceeded_event
          ? DlpPolicyEventBuilder::WarningProceededEvent(
                source_pattern, DlpRulesManager::Restriction::kFiles)
          : DlpPolicyEventBuilder::Event(source_pattern,
                                         DlpRulesManager::Restriction::kFiles,
                                         level.value());

  event_builder->SetContentName(path.BaseName().value());

  if (dst_pattern.has_value()) {
    DCHECK(!dst.component.has_value());
    event_builder->SetDestinationPattern(dst_pattern.value());
  } else {
    DCHECK(dst.component.has_value());
    event_builder->SetDestinationComponent(dst.component.value());
  }
  reporting_manager->ReportEvent(event_builder->Create());
}

::dlp::CheckFilesTransferResponse DlpFilesController::MaybeCloseDialog(
    ::dlp::CheckFilesTransferResponse response) {
  if (response.has_error_message() && warn_dialog_widget_ &&
      !warn_dialog_widget_->IsClosed()) {
    warn_dialog_widget_->CloseWithReason(
        views::Widget::ClosedReason::kUnspecified);
  }
  return response;
}

void DlpFilesController::OnGetFilesUrls(
    storage::FileSystemURL destination,
    bool is_move,
    GetDisallowedTransfersCallback result_callback,
    std::vector<storage::FileSystemURL> transferred_files) {
  ::dlp::CheckFilesTransferRequest request;
  base::flat_map<std::string, storage::FileSystemURL> filtered_files;
  for (const auto& file : transferred_files) {
    // If the file is in the same file system as the destination, no
    // restrictions should be applied.
    if (!file.IsInSameFileSystem(destination)) {
      auto file_path = file.path().value();
      filtered_files[file_path] = file;
      request.add_files_paths(file_path);
    }
  }
  if (filtered_files.empty()) {
    std::move(result_callback).Run(std::vector<storage::FileSystemURL>());
    return;
  }

  request.set_destination_url(destination.path().value());
  request.set_file_action(is_move ? ::dlp::FileAction::MOVE
                                  : ::dlp::FileAction::COPY);

  auto return_transfers_callback =
      base::BindOnce(&DlpFilesController::ReturnDisallowedTransfers,
                     weak_ptr_factory_.GetWeakPtr(), std::move(filtered_files),
                     std::move(result_callback));
  auto close_dialog_callback =
      base::BindOnce(&DlpFilesController::MaybeCloseDialog,
                     // base::Unretained() is safe since |this| is bound to
                     // |return_transfers_callback|, which will be called after
                     // |close_dialog_callback|
                     base::Unretained(this));
  chromeos::DlpClient::Get()->CheckFilesTransfer(
      request, std::move(close_dialog_callback)
                   .Then(std::move(return_transfers_callback)));
}

}  // namespace policy
