// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feedback;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.chrome.browser.profiles.Profile;

import java.util.HashMap;
import java.util.Map;

/** Reports profile settings for users in a family group. */
@JNINamespace("chrome::android")
public class FamilyInfoFeedbackSource implements AsyncFeedbackSource {
    private static final String FAMILY_MEMBER_ROLE = "Family_Member_Role";

    private final Profile mProfile;
    private Map<String, String> mFeedbackMap = new HashMap<>();
    private boolean mIsReady;
    private Runnable mCallback;

    public FamilyInfoFeedbackSource(Profile profile) {
        mProfile = profile;
    }

    // AsyncFeedbackSource implementation.
    @Override
    public void start(final Runnable callback) {
        mCallback = callback;
        FamilyInfoFeedbackSourceJni.get().start(this, mProfile);
    }

    @CalledByNative
    private void processFamilyMemberRole(String familyRole) {
        // Adds a family role only if the user is enrolled in a Family group.
        if (!familyRole.isEmpty()) {
            mFeedbackMap.put(FAMILY_MEMBER_ROLE, familyRole);
        }
        mIsReady = true;
        if (mCallback != null) {
            mCallback.run();
        }
    }

    @Override
    public boolean isReady() {
        return mIsReady;
    }

    @Override
    public Map<String, String> getFeedback() {
        return mFeedbackMap;
    }

    @NativeMethods
    interface Natives {
        void start(FamilyInfoFeedbackSource source, Profile profile);
    }
}
