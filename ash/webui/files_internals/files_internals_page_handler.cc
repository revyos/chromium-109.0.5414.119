// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/files_internals/files_internals_page_handler.h"

#include "ash/webui/files_internals/files_internals_ui.h"

namespace ash {

FilesInternalsPageHandler::FilesInternalsPageHandler(
    FilesInternalsUI* files_internals_ui,
    mojo::PendingReceiver<mojom::files_internals::PageHandler> receiver)
    : files_internals_ui_(files_internals_ui),
      receiver_(this, std::move(receiver)) {}

FilesInternalsPageHandler::~FilesInternalsPageHandler() = default;

void FilesInternalsPageHandler::GetSmbfsEnableVerboseLogging(
    GetSmbfsEnableVerboseLoggingCallback callback) {
  std::move(callback).Run(
      files_internals_ui_->delegate()->GetSmbfsEnableVerboseLogging());
}

void FilesInternalsPageHandler::SetSmbfsEnableVerboseLogging(bool enabled) {
  files_internals_ui_->delegate()->SetSmbfsEnableVerboseLogging(enabled);
}

}  // namespace ash
