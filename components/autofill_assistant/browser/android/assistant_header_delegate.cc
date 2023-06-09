// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill_assistant/browser/android/assistant_header_delegate.h"

#include "components/autofill_assistant/android/jni_headers/AssistantHeaderDelegate_jni.h"
#include "components/autofill_assistant/browser/android/ui_controller_android.h"

using base::android::AttachCurrentThread;

namespace autofill_assistant {

AssistantHeaderDelegate::AssistantHeaderDelegate(
    UiControllerAndroid* ui_controller)
    : ui_controller_(ui_controller) {
  java_assistant_header_delegate_ = Java_AssistantHeaderDelegate_create(
      AttachCurrentThread(), reinterpret_cast<intptr_t>(this));
}

AssistantHeaderDelegate::~AssistantHeaderDelegate() {
  Java_AssistantHeaderDelegate_clearNativePtr(AttachCurrentThread(),
                                              java_assistant_header_delegate_);
}

void AssistantHeaderDelegate::OnFeedbackButtonClicked(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jcaller) {
  ui_controller_->OnHeaderFeedbackButtonClicked();
}

void AssistantHeaderDelegate::OnTtsButtonClicked(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jcaller) {
  ui_controller_->OnTtsButtonClicked();
}

base::android::ScopedJavaGlobalRef<jobject>
AssistantHeaderDelegate::GetJavaObject() const {
  return java_assistant_header_delegate_;
}

}  // namespace autofill_assistant
