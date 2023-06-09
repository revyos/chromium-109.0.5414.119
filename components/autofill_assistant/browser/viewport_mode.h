// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_VIEWPORT_MODE_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_VIEWPORT_MODE_H_

namespace autofill_assistant {

// GENERATED_JAVA_ENUM_PACKAGE: (
// org.chromium.components.autofill_assistant)
// GENERATED_JAVA_CLASS_NAME_OVERRIDE: AssistantViewportMode
enum ViewportMode {
  // Don't resize the layout nor visual viewport.
  NO_RESIZE = 0,

  // Resize the layout viewport by the peek height of the sheet.
  RESIZE_LAYOUT_VIEWPORT = 1,

  // Resize the visual viewport by the height of the sheet.
  RESIZE_VISUAL_VIEWPORT = 2,
};

}  // namespace autofill_assistant

#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_VIEWPORT_MODE_H_
