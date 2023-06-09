// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/policy/arc/fake_android_management_client.h"

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"

namespace policy {

FakeAndroidManagementClient::FakeAndroidManagementClient() = default;

FakeAndroidManagementClient::~FakeAndroidManagementClient() = default;

void FakeAndroidManagementClient::StartCheckAndroidManagement(
    StatusCallback callback) {
  start_check_android_management_call_count_++;
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), result_));
}

}  // namespace policy
