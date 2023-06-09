// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import 'chrome://resources/cr_elements/cr_shared_vars.css.js';
import 'chrome://resources/cr_components/app_management/shared_vars.css.js';

const $_documentContainer = document.createElement('template');
$_documentContainer.innerHTML = `{__html_template__}`;
document.head.appendChild($_documentContainer.content);
