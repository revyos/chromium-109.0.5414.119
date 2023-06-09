// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "components/update_client/task_update.h"

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/update_client/update_client.h"
#include "components/update_client/update_engine.h"

namespace update_client {

TaskUpdate::TaskUpdate(
    scoped_refptr<UpdateEngine> update_engine,
    bool is_foreground,
    bool is_install,
    const std::vector<std::string>& ids,
    UpdateClient::CrxDataCallback crx_data_callback,
    UpdateClient::CrxStateChangeCallback crx_state_change_callback,
    Callback callback)
    : update_engine_(update_engine),
      is_foreground_(is_foreground),
      is_install_(is_install),
      ids_(ids),
      crx_data_callback_(std::move(crx_data_callback)),
      crx_state_change_callback_(crx_state_change_callback),
      callback_(std::move(callback)) {}

TaskUpdate::~TaskUpdate() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void TaskUpdate::Run() {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (ids_.empty()) {
    TaskComplete(Error::INVALID_ARGUMENT);
    return;
  }

  if (cancelled_) {
    TaskComplete(Error::UPDATE_CANCELED);
    return;
  }

  cancel_callback_ = update_engine_->Update(
      is_foreground_, is_install_, ids_, std::move(crx_data_callback_),
      std::move(crx_state_change_callback_),
      base::BindOnce(&TaskUpdate::TaskComplete, this));
}

void TaskUpdate::Cancel() {
  DCHECK(thread_checker_.CalledOnValidThread());
  cancelled_ = true;
  cancel_callback_.Run();
}

std::vector<std::string> TaskUpdate::GetIds() const {
  return ids_;
}

void TaskUpdate::TaskComplete(Error error) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // NOTE: Do not post the callback_ directly to the task thread to ensure the
  // UpdateClient reference (to which the callback is bound) does not get
  // released before the callback is run during shutdown, when the task runner
  // gets destroyed.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&TaskUpdate::RunCallback, this, error));
}

void TaskUpdate::RunCallback(Error error) {
  std::move(callback_).Run(this, error);
}

}  // namespace update_client
