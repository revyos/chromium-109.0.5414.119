// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ash/components/dbus/arc/fake_arc_obb_mounter_client.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/threading/thread_task_runner_handle.h"

namespace ash {

FakeArcObbMounterClient::FakeArcObbMounterClient() = default;

FakeArcObbMounterClient::~FakeArcObbMounterClient() = default;

void FakeArcObbMounterClient::Init(dbus::Bus* bus) {}

void FakeArcObbMounterClient::MountObb(
    const std::string& obb_file,
    const std::string& mount_path,
    int32_t owner_gid,
    chromeos::VoidDBusMethodCallback callback) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), false));
}

void FakeArcObbMounterClient::UnmountObb(
    const std::string& mount_path,
    chromeos::VoidDBusMethodCallback callback) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), false));
}

}  // namespace ash
