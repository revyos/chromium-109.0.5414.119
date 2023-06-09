// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/bind.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/apps/intent_helper/intent_picker_features.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/intent_picker_bubble_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_view.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/ui/web_applications/test/web_app_navigation_browsertest.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/fenced_frame_test_util.h"
#include "content/public/test/prerender_test_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/features.h"
#include "ui/views/widget/any_widget_observer.h"
#include "url/gurl.h"

class IntentPickerBubbleViewBrowserTest
    : public web_app::WebAppNavigationBrowserTest,
      public ::testing::WithParamInterface<std::string> {
 public:
  IntentPickerBubbleViewBrowserTest() {
    std::vector<base::test::FeatureRef> disabled_features = {
        // TODO(schenney): Stop disabling Paint Holding. crbug.com/1001189
        blink::features::kPaintHolding,
        // TODO(crbug.com/1357905): Run relevant tests against the updated UI.
        apps::features::kLinkCapturingUiUpdate};
    scoped_feature_list_.InitWithFeatures({}, disabled_features);
  }

  void OpenNewTab(const GURL& url) {
    chrome::NewTab(browser());
    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    NavigateToLaunchingPage(browser());
    TestTabActionDoesNotOpenAppWindow(
        url, base::BindOnce(&ClickLinkAndWait, web_contents, url,
                            LinkTarget::SELF, GetParam()));
  }

  // Inserts an iframe in the main frame of |web_contents|.
  bool InsertIFrame(content::WebContents* web_contents) {
    return content::ExecuteScript(
        web_contents,
        "let iframe = document.createElement('iframe');"
        "iframe.id = 'iframe';"
        "document.body.appendChild(iframe);");
  }

  PageActionIconView* GetIntentPickerIcon() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->toolbar_button_provider()
        ->GetPageActionIconView(PageActionIconType::kIntentPicker);
  }

  IntentPickerBubbleView* intent_picker_bubble() {
    return IntentPickerBubbleView::intent_picker_bubble();
  }

  size_t GetItemContainerSize(IntentPickerBubbleView* bubble) {
    return bubble->GetViewByID(IntentPickerBubbleView::ViewId::kItemContainer)
        ->children()
        .size();
  }

  void VerifyBubbleWithTestWebApp() {
    EXPECT_EQ(1U, GetItemContainerSize(intent_picker_bubble()));
    auto& app_info = intent_picker_bubble()->app_info_for_testing();
    ASSERT_EQ(1U, app_info.size());
    EXPECT_EQ(test_web_app_id(), app_info[0].launch_name);
    EXPECT_EQ(GetAppName(), app_info[0].display_name);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

// Tests that clicking a link from a tabbed browser to outside the scope of an
// installed app does not show the intent picker.
IN_PROC_BROWSER_TEST_P(IntentPickerBubbleViewBrowserTest,
                       NavigationToOutofScopeLinkDoesNotShowIntentPicker) {
  InstallTestWebApp();

  const GURL out_of_scope_url =
      https_server().GetURL(GetAppUrlHost(), GetOutOfScopeUrlPath());
  NavigateToLaunchingPage(browser());
  TestTabActionDoesNotOpenAppWindow(
      out_of_scope_url,
      base::BindOnce(&ClickLinkAndWait,
                     browser()->tab_strip_model()->GetActiveWebContents(),
                     out_of_scope_url, LinkTarget::SELF, GetParam()));

  EXPECT_EQ(nullptr, intent_picker_bubble());
}

// TODO(crbug.com/1252812): Enable the following two tests on Lacros.
#if !BUILDFLAG(IS_CHROMEOS_LACROS)
// Tests that clicking a link from a tabbed browser to within the scope of an
// installed app shows the intent picker icon in Omnibox.
IN_PROC_BROWSER_TEST_P(IntentPickerBubbleViewBrowserTest,
                       NavigationToInScopeLinkShowsIntentPicker) {
  InstallTestWebApp();

  const GURL in_scope_url =
      https_server().GetURL(GetAppUrlHost(), GetInScopeUrlPath());
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  NavigateToLaunchingPage(browser());

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                       "IntentPickerBubbleView");
  TestTabActionDoesNotOpenAppWindow(
      in_scope_url, base::BindOnce(&ClickLinkAndWait, web_contents,
                                   in_scope_url, LinkTarget::SELF, GetParam()));

  PageActionIconView* intent_picker_view = GetIntentPickerIcon();
  EXPECT_TRUE(intent_picker_view->GetVisible());

#if !BUILDFLAG(IS_CHROMEOS)
  // On Chrome OS, the picker bubble will appear automatically.
  EXPECT_FALSE(intent_picker_bubble());
  GetIntentPickerIcon()->ExecuteForTesting();
#endif

  waiter.WaitIfNeededAndGet();
  ASSERT_TRUE(intent_picker_bubble());
  EXPECT_TRUE(intent_picker_bubble()->GetVisible());

  VerifyBubbleWithTestWebApp();

  intent_picker_bubble()->AcceptDialog();

  Browser* app_browser = BrowserList::GetInstance()->GetLastActive();
  EXPECT_TRUE(web_app::AppBrowserController::IsForWebApp(app_browser,
                                                         test_web_app_id()));
}

#if BUILDFLAG(IS_CHROMEOS)
// Tests that clicking a link from an app browser to either within or outside
// the scope of an installed app does not show the intent picker, even when an
// outside of scope link is opened within the context of the PWA.
IN_PROC_BROWSER_TEST_P(
    IntentPickerBubbleViewBrowserTest,
    NavigationInAppWindowToInScopeLinkDoesNotShowIntentPicker) {
  InstallTestWebApp();

  // No intent picker should be seen when first opening the web app.
  Browser* app_browser = OpenTestWebApp();
  EXPECT_EQ(nullptr, intent_picker_bubble());

  {
    const GURL in_scope_url =
        https_server().GetURL(GetAppUrlHost(), GetInScopeUrlPath());
    TestActionDoesNotOpenAppWindow(
        app_browser, in_scope_url,
        base::BindOnce(&ClickLinkAndWait,
                       app_browser->tab_strip_model()->GetActiveWebContents(),
                       in_scope_url, LinkTarget::SELF, GetParam()));

    EXPECT_EQ(nullptr, intent_picker_bubble());
  }

  {
    const GURL out_of_scope_url =
        https_server().GetURL(GetAppUrlHost(), GetOutOfScopeUrlPath());
    TestActionDoesNotOpenAppWindow(
        app_browser, out_of_scope_url,
        base::BindOnce(&ClickLinkAndWait,
                       app_browser->tab_strip_model()->GetActiveWebContents(),
                       out_of_scope_url, LinkTarget::SELF, GetParam()));

    EXPECT_EQ(nullptr, intent_picker_bubble());
  }
}
#endif  // BUILDFLAG(IS_CHROMEOS)
#endif  // !BUILDFLAG(IS_CHROMEOS_LACROS)

// Tests that the intent icon updates its visibiliy when switching between
// tabs.
IN_PROC_BROWSER_TEST_P(IntentPickerBubbleViewBrowserTest,
                       IconVisibilityAfterTabSwitching) {
  InstallTestWebApp();

  const GURL in_scope_url =
      https_server().GetURL(GetAppUrlHost(), GetInScopeUrlPath());
  const GURL out_of_scope_url =
      https_server().GetURL(GetAppUrlHost(), GetOutOfScopeUrlPath());

  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  // OpenNewTab opens a new tab and focus on the new tab.
  OpenNewTab(in_scope_url);
  EXPECT_TRUE(intent_picker_view->GetVisible());
  OpenNewTab(out_of_scope_url);
  EXPECT_FALSE(intent_picker_view->GetVisible());

  chrome::SelectPreviousTab(browser());
  EXPECT_TRUE(intent_picker_view->GetVisible());

  chrome::SelectNextTab(browser());
  EXPECT_FALSE(intent_picker_view->GetVisible());
}

// Tests that the navigation in iframe doesn't affect intent picker icon
IN_PROC_BROWSER_TEST_P(IntentPickerBubbleViewBrowserTest,
                       IframeNavigationDoesNotAffectIntentPicker) {
  InstallTestWebApp();

  const GURL in_scope_url =
      https_server().GetURL(GetAppUrlHost(), GetInScopeUrlPath());
  const GURL out_of_scope_url =
      https_server().GetURL(GetAppUrlHost(), GetOutOfScopeUrlPath());

  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  OpenNewTab(out_of_scope_url);
  content::WebContents* initial_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(InsertIFrame(initial_tab));

  EXPECT_TRUE(
      content::NavigateIframeToURL(initial_tab, "iframe", in_scope_url));
  EXPECT_FALSE(intent_picker_view->GetVisible());

  OpenNewTab(in_scope_url);
  content::WebContents* new_tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(InsertIFrame(new_tab));

  EXPECT_TRUE(
      content::NavigateIframeToURL(initial_tab, "iframe", out_of_scope_url));
  EXPECT_TRUE(intent_picker_view->GetVisible());
}

// Tests that the intent picker icon is not visible if the navigatation
// redirects to a URL that doesn't have an installed PWA.
IN_PROC_BROWSER_TEST_P(IntentPickerBubbleViewBrowserTest,
                       DoesNotShowIntentPickerWhenRedirectedOutOfScope) {
  InstallTestWebApp(GetOtherAppUrlHost(), /*app_scope=*/"/");

  const GURL out_of_scope_url =
      https_server().GetURL(GetAppUrlHost(), GetOutOfScopeUrlPath());
  const GURL in_scope_url = https_server().GetURL(GetOtherAppUrlHost(), "/");
  const GURL redirect_url = https_server().GetURL(
      GetOtherAppUrlHost(), CreateServerRedirect(out_of_scope_url));

  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  OpenNewTab(in_scope_url);
  EXPECT_TRUE(intent_picker_view->GetVisible());

  ClickLinkAndWaitForURL(browser()->tab_strip_model()->GetActiveWebContents(),
                         redirect_url, out_of_scope_url, LinkTarget::SELF,
                         GetParam());
  EXPECT_FALSE(intent_picker_view->GetVisible());
}

INSTANTIATE_TEST_SUITE_P(
    All,
    IntentPickerBubbleViewBrowserTest,
    testing::Values("", "noopener", "noreferrer", "nofollow"));

class IntentPickerBubbleViewPrerenderingBrowserTest
    : public IntentPickerBubbleViewBrowserTest {
 public:
  IntentPickerBubbleViewPrerenderingBrowserTest()
      : prerender_helper_(base::BindRepeating(
            &IntentPickerBubbleViewPrerenderingBrowserTest::GetWebContents,
            base::Unretained(this))) {}
  ~IntentPickerBubbleViewPrerenderingBrowserTest() override = default;
  IntentPickerBubbleViewPrerenderingBrowserTest(
      const IntentPickerBubbleViewPrerenderingBrowserTest&) = delete;

  IntentPickerBubbleViewPrerenderingBrowserTest& operator=(
      const IntentPickerBubbleViewPrerenderingBrowserTest&) = delete;

  void SetUp() override {
    prerender_helper_.SetUp(embedded_test_server());
    IntentPickerBubbleViewBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    IntentPickerBubbleViewBrowserTest::SetUpOnMainThread();
  }

  content::test::PrerenderTestHelper& prerender_test_helper() {
    return prerender_helper_;
  }

  content::WebContents* GetWebContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

 private:
  content::test::PrerenderTestHelper prerender_helper_;
};

IN_PROC_BROWSER_TEST_P(IntentPickerBubbleViewPrerenderingBrowserTest,
                       PrerenderingShouldNotShowIntentPicker) {
  InstallTestWebApp();

  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  const GURL initial_url =
      https_server().GetURL(GetAppUrlHost(), "/empty.html");
  OpenNewTab(initial_url);
  EXPECT_FALSE(intent_picker_view->GetVisible());

  // Load a prerender page and prerendering should not try to show the
  // intent picker.
  const GURL prerender_url = https_server().GetURL(
      GetAppUrlHost(), std::string(GetAppScopePath()) + "index1.html");
  int host_id = prerender_test_helper().AddPrerender(prerender_url);
  content::test::PrerenderHostObserver host_observer(*GetWebContents(),
                                                     host_id);
  EXPECT_FALSE(host_observer.was_activated());
  EXPECT_FALSE(intent_picker_view->GetVisible());

  // Activate the prerender page.
  prerender_test_helper().NavigatePrimaryPage(prerender_url);
  EXPECT_TRUE(host_observer.was_activated());

  // After activation, IntentPickerTabHelper should show the
  // intent picker.
  EXPECT_TRUE(intent_picker_view->GetVisible());
}

INSTANTIATE_TEST_SUITE_P(
    All,
    IntentPickerBubbleViewPrerenderingBrowserTest,
    testing::Values("", "noopener", "noreferrer", "nofollow"));

class IntentPickerBubbleViewFencedFrameBrowserTest
    : public IntentPickerBubbleViewBrowserTest {
 public:
  IntentPickerBubbleViewFencedFrameBrowserTest() = default;
  ~IntentPickerBubbleViewFencedFrameBrowserTest() override = default;
  IntentPickerBubbleViewFencedFrameBrowserTest(
      const IntentPickerBubbleViewFencedFrameBrowserTest&) = delete;

  IntentPickerBubbleViewFencedFrameBrowserTest& operator=(
      const IntentPickerBubbleViewFencedFrameBrowserTest&) = delete;

  content::test::FencedFrameTestHelper& fenced_frame_test_helper() {
    return fenced_frame_helper_;
  }

 private:
  content::test::FencedFrameTestHelper fenced_frame_helper_;
};

IN_PROC_BROWSER_TEST_P(IntentPickerBubbleViewFencedFrameBrowserTest,
                       ShouldShowIntentPickerInFencedFrame) {
  InstallTestWebApp();

  PageActionIconView* intent_picker_view = GetIntentPickerIcon();

  const GURL initial_url =
      https_server().GetURL(GetAppUrlHost(), "/empty.html");
  OpenNewTab(initial_url);
  EXPECT_FALSE(intent_picker_view->GetVisible());

  const GURL fenced_frame_url = https_server().GetURL(
      GetAppUrlHost(), std::string(GetAppScopePath()) + "index1.html");
  // Create a fenced frame.
  ASSERT_TRUE(
      fenced_frame_test_helper().CreateFencedFrame(browser()
                                                       ->tab_strip_model()
                                                       ->GetActiveWebContents()
                                                       ->GetPrimaryMainFrame(),
                                                   fenced_frame_url));

  EXPECT_FALSE(intent_picker_view->GetVisible());
}

INSTANTIATE_TEST_SUITE_P(
    All,
    IntentPickerBubbleViewFencedFrameBrowserTest,
    testing::Values("", "noopener", "noreferrer", "nofollow"));
