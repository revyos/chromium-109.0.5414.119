// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.autofill_assistant.infobox;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * State for the infobox of the Autofill Assistant.
 */
@JNINamespace("autofill_assistant")
public class AssistantInfoBoxModel extends PropertyModel {
    @VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
    public static final WritableObjectPropertyKey<AssistantInfoBox> INFO_BOX =
            new WritableObjectPropertyKey<>();

    public AssistantInfoBoxModel() {
        super(INFO_BOX);
    }

    @CalledByNative
    private void setInfoBox(AssistantInfoBox infobox) {
        set(INFO_BOX, infobox);
    }

    @CalledByNative
    private void clearInfoBox() {
        set(INFO_BOX, null);
    }
}
