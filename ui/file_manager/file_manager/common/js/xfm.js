// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {notifications} from './notifications_browser_proxy.js';
import {storage} from './storage_adapter.js';

// namespace
export const xfm = {
  notifications,
  storage,
  /**
   * @return {!chrome.app.window.AppWindow}
   */
  getCurrentWindow: () => {
    return /** @type {!chrome.app.window.AppWindow} */ ({
      minimize: () => {
        // TODO(1097066): Implement.
      },
      focus: () => {
        window.focus();
      },
    });
  },
};
