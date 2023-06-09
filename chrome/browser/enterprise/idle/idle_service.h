// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ENTERPRISE_IDLE_IDLE_SERVICE_H_
#define CHROME_BROWSER_ENTERPRISE_IDLE_IDLE_SERVICE_H_

#include <memory>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "chrome/browser/enterprise/idle/action_runner.h"
#include "chrome/browser/enterprise/idle/browser_closer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/idle/idle_polling_service.h"

class Profile;

namespace enterprise_idle {

// Manages the state of a profile for the IdleProfileCloseTimeout enterprise
// policy. Keeps track of the policy's value, and listens for idle events.
// Closes the profile's window when it becomes idle, and shows the profile
// picker.
class IdleService : public KeyedService,
                    public ui::IdlePollingService::Observer {
 public:
  explicit IdleService(Profile* profile);

  IdleService(const IdleService&) = delete;
  IdleService& operator=(const IdleService&) = delete;

  ~IdleService() override;

  // ui::IdlePollingService::Observer:
  void OnIdleStateChange(
      const ui::IdlePollingService::State& polled_state) override;

 private:
  // Called when the IdleProfileCloseTimeout policy changes, via the
  // "idle_profile_close_timeout" pref it's mapped to.
  void OnIdleProfileCloseTimeoutPrefChanged();

  // Runs when the BrowserCloser finishes. Depending on the result, shows the
  // Profile Picker.
  void OnCloseFinished(BrowserCloser::CloseResult result);

  raw_ptr<Profile> const profile_;
  std::unique_ptr<ActionRunner> action_runner_;
  PrefChangeRegistrar pref_change_registrar_;

  bool is_idle_ = false;
  base::TimeDelta idle_threshold_;

  base::ScopedObservation<ui::IdlePollingService,
                          ui::IdlePollingService::Observer>
      polling_service_observation_{this};

  base::WeakPtrFactory<IdleService> weak_ptr_factory_{this};
};

}  // namespace enterprise_idle

#endif  // CHROME_BROWSER_ENTERPRISE_IDLE_IDLE_SERVICE_H_
