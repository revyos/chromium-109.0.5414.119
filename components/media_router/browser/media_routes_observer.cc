// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/media_router/browser/media_routes_observer.h"

#include "base/check.h"
#include "components/media_router/browser/media_router.h"

namespace media_router {

MediaRoutesObserver::MediaRoutesObserver(MediaRouter* router)
    : router_(router) {
  DCHECK(router_);
  router_->RegisterMediaRoutesObserver(this);
}

MediaRoutesObserver::~MediaRoutesObserver() {
  router_->UnregisterMediaRoutesObserver(this);
}

}  // namespace media_router
