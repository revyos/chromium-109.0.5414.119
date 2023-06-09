// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/locks/shared_web_contents_lock.h"

#include "chrome/browser/web_applications/locks/lock.h"

namespace web_app {

SharedWebContentsLockDescription::SharedWebContentsLockDescription()
    : LockDescription({}, LockDescription::Type::kBackgroundWebContents) {}
SharedWebContentsLockDescription::~SharedWebContentsLockDescription() = default;

SharedWebContentsLock::SharedWebContentsLock(
    content::WebContents& shared_web_contents)
    : shared_web_contents_(shared_web_contents) {}
SharedWebContentsLock::~SharedWebContentsLock() = default;
}  // namespace web_app
