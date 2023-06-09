// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/login/quick_unlock/quick_unlock_factory.h"

#include "chrome/browser/ash/login/quick_unlock/quick_unlock_storage.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"

namespace ash {
namespace quick_unlock {

// static
QuickUnlockStorage* QuickUnlockFactory::GetForProfile(Profile* profile) {
  return static_cast<QuickUnlockStorage*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
QuickUnlockStorage* QuickUnlockFactory::GetForUser(
    const user_manager::User* user) {
  Profile* profile = ProfileHelper::Get()->GetProfileByUser(user);
  if (!profile)
    return nullptr;

  return GetForProfile(profile);
}

// static
QuickUnlockStorage* QuickUnlockFactory::GetForAccountId(
    const AccountId& account_id) {
  const user_manager::User* user =
      user_manager::UserManager::Get()->FindUser(account_id);
  if (!user)
    return nullptr;

  return GetForUser(user);
}

// static
QuickUnlockFactory* QuickUnlockFactory::GetInstance() {
  return base::Singleton<QuickUnlockFactory>::get();
}

QuickUnlockFactory::QuickUnlockFactory()
    : ProfileKeyedServiceFactory("QuickUnlockFactory") {}

QuickUnlockFactory::~QuickUnlockFactory() {}

KeyedService* QuickUnlockFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  return new QuickUnlockStorage(Profile::FromBrowserContext(context));
}

}  // namespace quick_unlock
}  // namespace ash
