// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions.basic;

import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.verify;
import static org.robolectric.Shadows.shadowOf;

import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;

import androidx.annotation.DrawableRes;
import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.robolectric.annotation.Config;

import org.chromium.base.ContextUtils;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.omnibox.OmniboxSuggestionType;
import org.chromium.chrome.browser.omnibox.R;
import org.chromium.chrome.browser.omnibox.ShadowUrlBarData;
import org.chromium.chrome.browser.omnibox.UrlBarEditingTextStateProvider;
import org.chromium.chrome.browser.omnibox.suggestions.FaviconFetcher;
import org.chromium.chrome.browser.omnibox.suggestions.FaviconFetcher.FaviconFetchCompleteListener;
import org.chromium.chrome.browser.omnibox.suggestions.FaviconFetcher.FaviconType;
import org.chromium.chrome.browser.omnibox.suggestions.SuggestionHost;
import org.chromium.chrome.browser.omnibox.suggestions.base.BaseSuggestionViewProperties;
import org.chromium.chrome.browser.omnibox.suggestions.base.SuggestionDrawableState;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.components.omnibox.AutocompleteMatch;
import org.chromium.components.omnibox.AutocompleteMatchBuilder;
import org.chromium.ui.modelutil.PropertyModel;
import org.chromium.url.GURL;
import org.chromium.url.JUnitTestGURLs;
import org.chromium.url.ShadowGURL;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Tests for {@link BasicSuggestionProcessor}.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE, shadows = {ShadowGURL.class, ShadowUrlBarData.class})
public class BasicSuggestionProcessorUnitTest {
    private static final GURL EXTERNAL_URL = JUnitTestGURLs.getGURL(JUnitTestGURLs.URL_1);
    private static final GURL INTERNAL_URL = JUnitTestGURLs.getGURL(JUnitTestGURLs.NTP_URL);
    private static final @DrawableRes int ICON_BOOKMARK = R.drawable.btn_star;
    private static final @DrawableRes int ICON_GLOBE = R.drawable.ic_globe_24dp;
    private static final @DrawableRes int ICON_HISTORY = R.drawable.ic_history_googblue_24dp;
    private static final @DrawableRes int ICON_MAGNIFIER = R.drawable.ic_suggestion_magnifier;
    private static final @DrawableRes int ICON_TRENDS = R.drawable.trending_up_black_24dp;
    private static final @DrawableRes int ICON_VOICE = R.drawable.btn_mic;
    private static final @DrawableRes int ICON_FAVICON = 0; // Favicons do not come from resources.

    private static final Map<Integer, String> ICON_TYPE_NAMES = new HashMap<Integer, String>() {
        {
            put(ICON_BOOKMARK, "BOOKMARK");
            put(ICON_HISTORY, "HISTORY");
            put(ICON_GLOBE, "GLOBE");
            put(ICON_MAGNIFIER, "MAGNIFIER");
            put(ICON_VOICE, "VOICE");
            put(ICON_FAVICON, "FAVICON");
        }
    };

    private static final Map<Integer, String> SUGGESTION_TYPE_NAMES = new HashMap<Integer, String>(
            OmniboxSuggestionType.NUM_TYPES) {
        {
            put(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, "URL_WHAT_YOU_TYPED");
            put(OmniboxSuggestionType.HISTORY_URL, "HISTORY_URL");
            put(OmniboxSuggestionType.HISTORY_TITLE, "HISTORY_TITLE");
            put(OmniboxSuggestionType.HISTORY_BODY, "HISTORY_BODY");
            put(OmniboxSuggestionType.HISTORY_KEYWORD, "HISTORY_KEYWORD");
            put(OmniboxSuggestionType.NAVSUGGEST, "NAVSUGGEST");
            put(OmniboxSuggestionType.SEARCH_WHAT_YOU_TYPED, "SEARCH_WHAT_YOU_TYPED");
            put(OmniboxSuggestionType.SEARCH_HISTORY, "SEARCH_HISTORY");
            put(OmniboxSuggestionType.SEARCH_SUGGEST, "SEARCH_SUGGEST");
            put(OmniboxSuggestionType.SEARCH_SUGGEST_ENTITY, "SEARCH_SUGGEST_ENTITY");
            put(OmniboxSuggestionType.SEARCH_SUGGEST_TAIL, "SEARCH_SUGGEST_TAIL");
            put(OmniboxSuggestionType.SEARCH_SUGGEST_PERSONALIZED, "SEARCH_SUGGEST_PERSONALIZED");
            put(OmniboxSuggestionType.SEARCH_SUGGEST_PROFILE, "SEARCH_SUGGEST_PROFILE");
            put(OmniboxSuggestionType.SEARCH_OTHER_ENGINE, "SEARCH_OTHER_ENGINE");
            put(OmniboxSuggestionType.NAVSUGGEST_PERSONALIZED, "NAVSUGGEST_PERSONALIZED");
            put(OmniboxSuggestionType.VOICE_SUGGEST, "VOICE_SUGGEST");
            put(OmniboxSuggestionType.DOCUMENT_SUGGESTION, "DOCUMENT_SUGGESTION");
            // Note: CALCULATOR suggestions are not handled by basic suggestion processor.
            // These suggestions are now processed by AnswerSuggestionProcessor instead.
        }
    };

    public @Rule TestRule mFeaturesProcessor = new Features.JUnitProcessor();
    public @Rule MockitoRule mMockitoRule = MockitoJUnit.rule();

    private @Mock SuggestionHost mSuggestionHost;
    private @Mock UrlBarEditingTextStateProvider mUrlBarText;
    private @Mock Bitmap mBitmap;
    private @Mock FaviconFetcher mIconFetcher;

    private BasicSuggestionProcessor mProcessor;
    private AutocompleteMatch mSuggestion;
    private PropertyModel mModel;

    private class BookmarkPredicate implements BasicSuggestionProcessor.BookmarkState {
        boolean mState;

        @Override
        public boolean isBookmarked(GURL url) {
            return mState;
        }
    }

    private final BookmarkPredicate mIsBookmarked = new BookmarkPredicate();

    @Before
    public void setUp() {
        doReturn("").when(mUrlBarText).getTextWithoutAutocomplete();
        mProcessor = new BasicSuggestionProcessor(ContextUtils.getApplicationContext(),
                mSuggestionHost, mUrlBarText, mIconFetcher, mIsBookmarked);
    }

    /**
     * Create Suggestion for test.
     * Do not use directly; use helper methods to create specific suggestion type instead.
     */
    private AutocompleteMatchBuilder createSuggestionBuilder(int type, String title) {
        return AutocompleteMatchBuilder.searchWithType(type).setDisplayText(title);
    }

    /** Create search suggestion for test. */
    private void createSearchSuggestion(int type, String title) {
        mSuggestion = createSuggestionBuilder(type, title).setIsSearch(true).build();
        mModel = mProcessor.createModel();
        mProcessor.populateModel(mSuggestion, mModel, 0);
    }

    /** Create URL suggestion with supplied text and target URL for test. */
    private void createUrlSuggestion(int type, String title, GURL url) {
        mSuggestion = createSuggestionBuilder(type, title).setIsSearch(false).setUrl(url).build();
        mModel = mProcessor.createModel();
        mProcessor.populateModel(mSuggestion, mModel, 0);
    }

    /** Create URL suggestion for test. */
    private void createUrlSuggestion(int type, String title) {
        createUrlSuggestion(type, title, GURL.emptyGURL());
    }

    /** Create switch to tab suggestion for test. */
    private void createSwitchToTabSuggestion(int type, String title) {
        mSuggestion = createSuggestionBuilder(type, title).setHasTabMatch(true).build();
        mModel = mProcessor.createModel();
        mProcessor.populateModel(mSuggestion, mModel, 0);
    }

    private void assertSuggestionTypeAndIcon(
            @OmniboxSuggestionType int expectedType, @DrawableRes int expectedIconRes) {
        SuggestionDrawableState sds = mModel.get(BaseSuggestionViewProperties.ICON);
        @DrawableRes
        int actualIconRes = shadowOf(sds.drawable).getCreatedFromResId();
        Assert.assertEquals(
                String.format("%s: Want Icon %s, Got %s", SUGGESTION_TYPE_NAMES.get(expectedType),
                        ICON_TYPE_NAMES.get(expectedIconRes), ICON_TYPE_NAMES.get(actualIconRes)),
                expectedIconRes, actualIconRes);
    }

    @Test
    @SmallTest
    public void getSuggestionIconTypeForSearch_Default() {
        int[][] testCases = {
                {OmniboxSuggestionType.URL_WHAT_YOU_TYPED, ICON_MAGNIFIER},
                {OmniboxSuggestionType.HISTORY_URL, ICON_MAGNIFIER},
                {OmniboxSuggestionType.HISTORY_TITLE, ICON_MAGNIFIER},
                {OmniboxSuggestionType.HISTORY_BODY, ICON_MAGNIFIER},
                {OmniboxSuggestionType.HISTORY_KEYWORD, ICON_MAGNIFIER},
                {OmniboxSuggestionType.NAVSUGGEST, ICON_MAGNIFIER},
                {OmniboxSuggestionType.SEARCH_WHAT_YOU_TYPED, ICON_MAGNIFIER},
                {OmniboxSuggestionType.SEARCH_HISTORY, ICON_HISTORY},
                {OmniboxSuggestionType.SEARCH_SUGGEST, ICON_MAGNIFIER},
                {OmniboxSuggestionType.SEARCH_SUGGEST_ENTITY, ICON_MAGNIFIER},
                {OmniboxSuggestionType.SEARCH_SUGGEST_TAIL, ICON_MAGNIFIER},
                {OmniboxSuggestionType.SEARCH_SUGGEST_PERSONALIZED, ICON_HISTORY},
                {OmniboxSuggestionType.SEARCH_SUGGEST_PROFILE, ICON_MAGNIFIER},
                {OmniboxSuggestionType.SEARCH_OTHER_ENGINE, ICON_MAGNIFIER},
                {OmniboxSuggestionType.NAVSUGGEST_PERSONALIZED, ICON_MAGNIFIER},
                {OmniboxSuggestionType.VOICE_SUGGEST, ICON_VOICE},
                {OmniboxSuggestionType.DOCUMENT_SUGGESTION, ICON_MAGNIFIER},
        };

        mProcessor.onNativeInitialized();
        for (int[] testCase : testCases) {
            createSearchSuggestion(testCase[0], "");
            Assert.assertTrue(mModel.get(SuggestionViewProperties.IS_SEARCH_SUGGESTION));
            assertSuggestionTypeAndIcon(testCase[0], testCase[1]);
        }
    }

    @Test
    @SmallTest
    public void getSuggestionIconTypeForUrl_Default() {
        int[][] testCases = {
                {OmniboxSuggestionType.URL_WHAT_YOU_TYPED, ICON_GLOBE},
                {OmniboxSuggestionType.HISTORY_URL, ICON_GLOBE},
                {OmniboxSuggestionType.HISTORY_TITLE, ICON_GLOBE},
                {OmniboxSuggestionType.HISTORY_BODY, ICON_GLOBE},
                {OmniboxSuggestionType.HISTORY_KEYWORD, ICON_GLOBE},
                {OmniboxSuggestionType.NAVSUGGEST, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_WHAT_YOU_TYPED, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_HISTORY, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_SUGGEST, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_SUGGEST_ENTITY, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_SUGGEST_TAIL, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_SUGGEST_PERSONALIZED, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_SUGGEST_PROFILE, ICON_GLOBE},
                {OmniboxSuggestionType.SEARCH_OTHER_ENGINE, ICON_GLOBE},
                {OmniboxSuggestionType.NAVSUGGEST_PERSONALIZED, ICON_GLOBE},
                {OmniboxSuggestionType.VOICE_SUGGEST, ICON_GLOBE},
                {OmniboxSuggestionType.DOCUMENT_SUGGESTION, ICON_GLOBE},
        };

        mProcessor.onNativeInitialized();
        for (int[] testCase : testCases) {
            createUrlSuggestion(testCase[0], "");
            Assert.assertFalse(mModel.get(SuggestionViewProperties.IS_SEARCH_SUGGESTION));
            assertSuggestionTypeAndIcon(testCase[0], testCase[1]);
        }
    }

    @Test
    @SmallTest
    public void getSuggestionIconTypeForBookmarks_Default() {
        int[][] testCases = {
                {OmniboxSuggestionType.URL_WHAT_YOU_TYPED, ICON_BOOKMARK},
                {OmniboxSuggestionType.HISTORY_URL, ICON_BOOKMARK},
                {OmniboxSuggestionType.HISTORY_TITLE, ICON_BOOKMARK},
                {OmniboxSuggestionType.HISTORY_BODY, ICON_BOOKMARK},
                {OmniboxSuggestionType.HISTORY_KEYWORD, ICON_BOOKMARK},
                {OmniboxSuggestionType.NAVSUGGEST, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_WHAT_YOU_TYPED, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_HISTORY, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_SUGGEST, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_SUGGEST_ENTITY, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_SUGGEST_TAIL, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_SUGGEST_PERSONALIZED, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_SUGGEST_PROFILE, ICON_BOOKMARK},
                {OmniboxSuggestionType.SEARCH_OTHER_ENGINE, ICON_BOOKMARK},
                {OmniboxSuggestionType.NAVSUGGEST_PERSONALIZED, ICON_BOOKMARK},
                {OmniboxSuggestionType.VOICE_SUGGEST, ICON_BOOKMARK},
                {OmniboxSuggestionType.DOCUMENT_SUGGESTION, ICON_BOOKMARK},
        };

        mIsBookmarked.mState = true;

        mProcessor.onNativeInitialized();
        for (int[] testCase : testCases) {
            createUrlSuggestion(testCase[0], "");
            Assert.assertFalse(mModel.get(SuggestionViewProperties.IS_SEARCH_SUGGESTION));
            assertSuggestionTypeAndIcon(testCase[0], testCase[1]);
        }
    }

    @Test
    @SmallTest
    public void getSuggestionIconTypeForTrendingQueries() {
        int[][] testCases = {
                {OmniboxSuggestionType.URL_WHAT_YOU_TYPED, ICON_TRENDS},
                {OmniboxSuggestionType.SEARCH_HISTORY, ICON_HISTORY},
                {OmniboxSuggestionType.SEARCH_SUGGEST, ICON_TRENDS},
                {OmniboxSuggestionType.SEARCH_SUGGEST_TAIL, ICON_TRENDS},
                {OmniboxSuggestionType.SEARCH_SUGGEST_PERSONALIZED, ICON_HISTORY},
                {OmniboxSuggestionType.VOICE_SUGGEST, ICON_VOICE},
        };

        mProcessor.onNativeInitialized();
        for (int[] testCase : testCases) {
            mSuggestion = createSuggestionBuilder(testCase[0], "").addSubtype(143).build();
            mModel = mProcessor.createModel();
            mProcessor.populateModel(mSuggestion, mModel, 0);
            Assert.assertTrue(mModel.get(SuggestionViewProperties.IS_SEARCH_SUGGESTION));
            assertSuggestionTypeAndIcon(testCase[0], testCase[1]);
        }
    }

    @Test
    @SmallTest
    public void refineIconNotShownForWhatYouTypedSuggestions() {
        final String typed = "Typed content";
        doReturn(typed).when(mUrlBarText).getTextWithoutAutocomplete();
        createSearchSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, typed);
        PropertyModel model = mProcessor.createModel();
        mProcessor.populateModel(mSuggestion, model, 0);
        Assert.assertNull(mModel.get(BaseSuggestionViewProperties.ACTIONS));

        createUrlSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, typed);
        mProcessor.populateModel(mSuggestion, model, 0);
        Assert.assertNull(mModel.get(BaseSuggestionViewProperties.ACTIONS));
    }

    @Test
    @SmallTest
    public void refineIconShownForRefineSuggestions() {
        final String typed = "Typed conte";
        final String refined = "Typed content";
        doReturn(typed).when(mUrlBarText).getTextWithoutAutocomplete();
        createSearchSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, refined);
        PropertyModel model = mProcessor.createModel();
        mProcessor.populateModel(mSuggestion, model, 0);
        Assert.assertNotNull(mModel.get(BaseSuggestionViewProperties.ACTIONS));

        createUrlSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, refined);
        mProcessor.populateModel(mSuggestion, model, 0);
        Assert.assertNotNull(mModel.get(BaseSuggestionViewProperties.ACTIONS));

        final List<BaseSuggestionViewProperties.Action> actions =
                mModel.get(BaseSuggestionViewProperties.ACTIONS);
        Assert.assertEquals(actions.size(), 1);
        final SuggestionDrawableState iconState = actions.get(0).icon;
        Assert.assertEquals(iconState.resourceId, R.drawable.btn_suggestion_refine);
    }

    @Test
    @SmallTest
    public void switchTabIconShownForSwitchToTabSuggestions() {
        final String tabMatch = "tab match";
        createSwitchToTabSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, tabMatch);
        PropertyModel model = mProcessor.createModel();
        mProcessor.populateModel(mSuggestion, model, 0);
        Assert.assertNotNull(mModel.get(BaseSuggestionViewProperties.ACTIONS));

        final List<BaseSuggestionViewProperties.Action> actions =
                mModel.get(BaseSuggestionViewProperties.ACTIONS);
        Assert.assertEquals(actions.size(), 1);
        final SuggestionDrawableState iconState = actions.get(0).icon;
        Assert.assertEquals(iconState.resourceId, R.drawable.switch_to_tab);
    }

    @Test
    @SmallTest
    public void suggestionFavicons_showFaviconWhenAvailable() {
        final ArgumentCaptor<FaviconFetchCompleteListener> callback =
                ArgumentCaptor.forClass(FaviconFetchCompleteListener.class);
        mProcessor.onNativeInitialized();
        createUrlSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, "");
        SuggestionDrawableState icon1 = mModel.get(BaseSuggestionViewProperties.ICON);
        Assert.assertNotNull(icon1);

        verify(mIconFetcher)
                .fetchFaviconWithBackoff(eq(mSuggestion.getUrl()), eq(false), callback.capture());
        callback.getValue().onFaviconFetchComplete(mBitmap, FaviconType.REGULAR);
        SuggestionDrawableState icon2 = mModel.get(BaseSuggestionViewProperties.ICON);
        Assert.assertNotNull(icon2);

        Assert.assertNotEquals(icon1, icon2);
        Assert.assertEquals(mBitmap, ((BitmapDrawable) icon2.drawable).getBitmap());
    }

    @Test
    @SmallTest
    public void suggestionFavicons_doNotReplaceFallbackIconWhenNoFaviconIsAvailable() {
        final ArgumentCaptor<FaviconFetchCompleteListener> callback =
                ArgumentCaptor.forClass(FaviconFetchCompleteListener.class);
        mProcessor.onNativeInitialized();
        createUrlSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, "");
        SuggestionDrawableState icon1 = mModel.get(BaseSuggestionViewProperties.ICON);
        Assert.assertNotNull(icon1);

        verify(mIconFetcher)
                .fetchFaviconWithBackoff(eq(mSuggestion.getUrl()), eq(false), callback.capture());
        callback.getValue().onFaviconFetchComplete(null, FaviconType.NONE);
        SuggestionDrawableState icon2 = mModel.get(BaseSuggestionViewProperties.ICON);
        Assert.assertNotNull(icon2);

        Assert.assertEquals(icon1, icon2);
    }

    @Test
    @SmallTest
    public void searchSuggestions_searchQueriesCanWrapAroundWithFeatureEnabled() {
        mProcessor.onNativeInitialized();
        createSearchSuggestion(OmniboxSuggestionType.SEARCH_WHAT_YOU_TYPED, "");
        Assert.assertEquals(mModel.get(SuggestionViewProperties.ALLOW_WRAP_AROUND), true);

        createUrlSuggestion(OmniboxSuggestionType.URL_WHAT_YOU_TYPED, "");
        Assert.assertEquals(mModel.get(SuggestionViewProperties.ALLOW_WRAP_AROUND), false);
    }

    @Test
    @SmallTest
    public void internalUrlSuggestions_doNotPresentInternalScheme() {
        mProcessor.onNativeInitialized();
        // URLs that are rejected by UrlBarData should not be presented to the User.
        ShadowUrlBarData.sShouldShowNextUrl = false;
        createUrlSuggestion(
                OmniboxSuggestionType.URL_WHAT_YOU_TYPED, "", new GURL(JUnitTestGURLs.URL_1));
        Assert.assertNull(mModel.get(SuggestionViewProperties.TEXT_LINE_2_TEXT));
    }
}