// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_INTEREST_GROUP_MANAGER_H_
#define CONTENT_PUBLIC_BROWSER_INTEREST_GROUP_MANAGER_H_

#include "base/callback_forward.h"
#include "content/common/content_export.h"
#include "url/origin.h"

namespace content {

// InterestGroupManager is a per-StoragePartition class that owns shared
// state needed to run FLEDGE auctions. It lives on the UI thread.
class InterestGroupManager {
 public:
  // Gets a list of all interest group joining origins. Each joining origin
  // will only appear once. A joining origin is the top-frame origin for a page
  // on which the join action occurred. Interest groups the user is joined to
  // can be used later as part of on device ad auctions. Control over whether
  // sites can join clients to interest groups is provided by the
  // IsInterestGroupAPIAllowed function on the ContentBrowserClient.
  virtual void GetAllInterestGroupJoiningOrigins(
      base::OnceCallback<void(std::vector<url::Origin>)> callback) = 0;

 protected:
  virtual ~InterestGroupManager() = default;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_INTEREST_GROUP_MANAGER_H_
