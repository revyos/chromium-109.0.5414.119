// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ash/components/dbus/cups_proxy/fake_cups_proxy_client.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/threading/thread_task_runner_handle.h"

namespace ash {

FakeCupsProxyClient::FakeCupsProxyClient() = default;
FakeCupsProxyClient::~FakeCupsProxyClient() = default;

void FakeCupsProxyClient::WaitForServiceToBeAvailable(
    dbus::ObjectProxy::WaitForServiceToBeAvailableCallback callback) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true));
}

void FakeCupsProxyClient::BootstrapMojoConnection(
    base::ScopedFD fd,
    base::OnceCallback<void(bool success)> result_callback) {
  const bool success = true;
  std::move(result_callback).Run(success);
}

}  // namespace ash
