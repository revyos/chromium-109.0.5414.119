// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.autofill_assistant.generic_ui;

import static org.chromium.components.autofill_assistant.AssistantAccessibilityUtils.setAccessibility;

import android.content.Context;
import android.graphics.PorterDuff;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.Nullable;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.components.autofill.prefeditor.EditorFieldModel;
import org.chromium.components.autofill.prefeditor.EditorTextField;
import org.chromium.components.autofill_assistant.AssistantChevronStyle;
import org.chromium.components.autofill_assistant.LayoutUtils;
import org.chromium.components.autofill_assistant.R;
import org.chromium.components.autofill_assistant.user_data.AssistantVerticalExpander;
import org.chromium.components.autofill_assistant.user_data.AssistantVerticalExpanderAccordion;
import org.chromium.components.browser_ui.styles.SemanticColorUtils;
import org.chromium.ui.widget.ChromeImageView;

/** Generic view factory. */
@JNINamespace("autofill_assistant")
public class AssistantViewFactory {
    private static final String TAG = "AutofillAssistant";

    /** Attaches {@code view} to {@code container}. */
    @CalledByNative
    public static void addViewToContainer(ViewGroup container, View view) {
        container.addView(view);
    }

    /** Set view attributes. All padding values are in dp. */
    @CalledByNative
    public static void setViewAttributes(View view, Context context, int paddingStart,
            int paddingTop, int paddingEnd, int paddingBottom,
            @Nullable AssistantDrawable background, @Nullable String contentDescription,
            boolean visible, boolean enabled) {
        view.setPaddingRelative(AssistantDimension.getPixelSizeDp(context, paddingStart),
                AssistantDimension.getPixelSizeDp(context, paddingTop),
                AssistantDimension.getPixelSizeDp(context, paddingEnd),
                AssistantDimension.getPixelSizeDp(context, paddingBottom));
        if (background != null) {
            background.getDrawable(context, result -> {
                if (result != null) {
                    view.setBackground(result);
                }
            });
        }
        setAccessibility(view, contentDescription);
        view.setVisibility(visible ? View.VISIBLE : View.GONE);
        view.setEnabled(enabled);
    }

    /**
     * Sets layout parameters for {@code view}. {@code width} and {@code height} must bei either
     * MATCH_PARENT (-1), WRAP_CONTENT (-2) or a value in dp.
     */
    @CalledByNative
    public static void setViewLayoutParams(View view, Context context, int width, int height,
            float weight, int marginStart, int marginTop, int marginEnd, int marginBottom,
            int layoutGravity, int minimumWidth, int minimumHeight) {
        if (width > 0) {
            width = AssistantDimension.getPixelSizeDp(context, width);
        }
        if (height > 0) {
            height = AssistantDimension.getPixelSizeDp(context, height);
        }

        LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(width, height);
        layoutParams.weight = weight;
        layoutParams.setMarginStart(AssistantDimension.getPixelSizeDp(context, marginStart));
        layoutParams.setMarginEnd(AssistantDimension.getPixelSizeDp(context, marginEnd));
        layoutParams.topMargin = AssistantDimension.getPixelSizeDp(context, marginTop);
        layoutParams.bottomMargin = AssistantDimension.getPixelSizeDp(context, marginBottom);
        layoutParams.gravity = layoutGravity;
        view.setLayoutParams(layoutParams);
        view.setMinimumWidth(AssistantDimension.getPixelSizeDp(context, minimumWidth));
        view.setMinimumHeight(AssistantDimension.getPixelSizeDp(context, minimumHeight));
    }

    /** Creates a {@code android.widget.LinearLayout} widget. */
    @CalledByNative
    public static LinearLayout createLinearLayout(
            Context context, String identifier, int orientation) {
        LinearLayout linearLayout = new LinearLayout(context);
        linearLayout.setOrientation(orientation);
        linearLayout.setTag(identifier);
        return linearLayout;
    }

    /** Creates a {@code android.widget.TextView} widget. */
    @CalledByNative
    public static TextView createTextView(Context context, AssistantGenericUiDelegate delegate,
            String identifier, String text, @Nullable String textAppearance, int textGravity) {
        TextView textView = new TextView(context);
        AssistantViewInteractions.setViewText(textView, text, delegate);
        textView.setTag(identifier);
        if (textAppearance != null) {
            try {
                // TODO(b/203392437): Find the correct way of accessing a resource by name.
                if (textAppearance.equals("TextAppearance.ErrorCaption")) {
                    Log.i(TAG, "Using explicit style id for " + textAppearance);
                    ApiCompatibilityUtils.setTextAppearance(
                            textView, R.style.TextAppearance_ErrorCaption);
                } else {
                    int fieldId =
                            R.style.class.getField(textAppearance.replace('.', '_')).getInt(null);
                    if (fieldId != 0) {
                        ApiCompatibilityUtils.setTextAppearance(textView, fieldId);
                    } else {
                        Log.e(TAG, "Could not find field id for " + textAppearance);
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "Error looking up style id for " + textAppearance, e);
            }
        }
        textView.setGravity(textGravity);
        return textView;
    }

    /** Creates a divider widget as used in the {@code AssistantCollectUserData} action. */
    @CalledByNative
    public static View createDividerView(Context context, String identifier) {
        View divider = LayoutUtils.createInflater(context).inflate(
                org.chromium.components.autofill_assistant.R.layout
                        .autofill_assistant_payment_request_section_divider,
                null, false);
        divider.setTag(identifier);
        return divider;
    }

    /** Creates a {@code ChromeImageView} widget. */
    @CalledByNative
    public static ChromeImageView createImageView(Context context, String identifier,
            AssistantDrawable image, boolean useIconSemanticTinting) {
        ChromeImageView imageView = new ChromeImageView(context);
        imageView.setTag(identifier);
        image.getDrawable(context, result -> {
            if (result != null) {
                if (useIconSemanticTinting) {
                    result.setColorFilter(SemanticColorUtils.getDefaultIconColorAccent1(context),
                            PorterDuff.Mode.SRC_IN);
                }
                imageView.setImageDrawable(result);
            }
        });
        return imageView;
    }

    /** Creates a {@code EditorTextField} view. */
    @CalledByNative
    public static View createTextInputView(Context context, AssistantGenericUiDelegate delegate,
            String viewIdentifier, int type, String hint, String modelIdentifier,
            boolean focusAndShowKeyboard) {
        View view = new EditorTextField(context,
                EditorFieldModel.createTextInput(type, hint, /* suggestions = */ null,
                        /* formatter = */ null, /* validator = */ null,
                        /* valueIconGenerator = */ null, /* requiredErrorMessage = */ null,
                        /* invalidErrorMessage = */ null,
                        EditorFieldModel.LENGTH_COUNTER_LIMIT_NONE, ""),
                (v, actionId, event)
                        -> false,
                /* filter = */ null, new TextWatcher() {
                    @Override
                    public void beforeTextChanged(CharSequence s, int start, int count, int after) {
                    }

                    @Override
                    public void onTextChanged(CharSequence s, int start, int before, int count) {}

                    @Override
                    public void afterTextChanged(Editable value) {
                        delegate.onValueChanged(modelIdentifier,
                                AssistantValue.createForStrings(new String[] {value.toString()}));
                    }
                }, focusAndShowKeyboard);
        view.setTag(viewIdentifier);
        return view;
    }

    /**
     * Creates an {@code AssistantVerticalExpander} widget.
     * @param chevronStyle Should match the enum defined in
     *         components/autofill_assistant/browser/view_layout.proto
     */
    @CalledByNative
    public static AssistantVerticalExpander createVerticalExpander(Context context,
            String identifier, @Nullable View titleView, @Nullable View collapsedView,
            @Nullable View expandedView, @AssistantChevronStyle int chevronStyle) {
        AssistantVerticalExpander expander = new AssistantVerticalExpander(context, null);
        if (titleView != null) {
            expander.setTitleView(titleView,
                    new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT));
        }
        if (collapsedView != null) {
            expander.setCollapsedView(collapsedView,
                    new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT));
        }
        if (expandedView != null) {
            expander.setExpandedView(expandedView,
                    new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.WRAP_CONTENT));
        }
        expander.setChevronStyle(chevronStyle);
        expander.setFixed(collapsedView == null || expandedView == null);
        expander.setTag(identifier);
        return expander;
    }

    /** Creates an {@code AssistantVerticalExpanderAccordion} widget. */
    @CalledByNative
    public static AssistantVerticalExpanderAccordion createVerticalExpanderAccordion(
            Context context, String identifier, int orientation) {
        AssistantVerticalExpanderAccordion accordion =
                new AssistantVerticalExpanderAccordion(context, null);
        accordion.setOrientation(orientation);
        accordion.setTag(identifier);
        return accordion;
    }

    /** Creates a {@code CompoundButton} widget. */
    @CalledByNative
    public static View createToggleButton(Context context, AssistantGenericUiDelegate delegate,
            String identifier, @Nullable View leftContentView, @Nullable View rightContentView,
            boolean isCheckbox, String modelIdentifier) {
        AssistantToggleButton view = new AssistantToggleButton(context,
                result
                -> delegate.onValueChanged(
                        modelIdentifier, AssistantValue.createForBooleans(new boolean[] {result})),
                leftContentView, rightContentView, isCheckbox);
        return view;
    }
}
