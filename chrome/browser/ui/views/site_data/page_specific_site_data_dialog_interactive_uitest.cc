// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "chrome/browser/browsing_data/cookies_tree_model.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/collected_cookies_views.h"
#include "chrome/browser/ui/views/page_info/page_info_cookies_content_view.h"
#include "chrome/browser/ui/views/page_info/page_info_main_view.h"
#include "chrome/browser/ui/views/page_info/page_info_view_factory.h"
#include "chrome/browser/ui/views/site_data/page_specific_site_data_dialog.h"
#include "chrome/browser/ui/views/site_data/page_specific_site_data_dialog_controller.h"
#include "chrome/browser/ui/views/site_data/site_data_row_view.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/page_info/core/features.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/interaction/expect_call_in_scope.h"
#include "ui/base/interaction/interaction_sequence.h"
#include "ui/base/interaction/interaction_test_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/tree/tree_view.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/interaction/interaction_test_util_views.h"
#include "ui/views/view_utils.h"

namespace {
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWebContentsElementId);
const char kFirstPartyAllowedRow[] = "FirstPartyAllowedRow";
const char kThirdPartyBlockedRow[] = "ThirdPartyBlockedRow";
const char kOnlyPartitionedRow[] = "OnlyPartitionedRow";
const char kMixedPartitionedRow[] = "MixedPartitionedRow";
const char kCookiesDialogHistogramName[] = "Privacy.CookiesInUseDialog.Action";
}  // namespace

class PageSpecificSiteDataDialogInteractiveUiTest
    : public InteractiveBrowserTest {
 public:
  PageSpecificSiteDataDialogInteractiveUiTest() {
    feature_list_.InitWithFeatures({page_info::kPageSpecificSiteDataDialog,
                                    page_info::kPageInfoCookiesSubpage,
                                    net::features::kPartitionedCookies},
                                   {});
    https_server_ = std::make_unique<net::EmbeddedTestServer>(
        net::EmbeddedTestServer::TYPE_HTTPS);
  }

  ~PageSpecificSiteDataDialogInteractiveUiTest() override = default;
  PageSpecificSiteDataDialogInteractiveUiTest(
      const PageSpecificSiteDataDialogInteractiveUiTest&) = delete;
  void operator=(const PageSpecificSiteDataDialogInteractiveUiTest&) = delete;

  void SetUp() override {
    https_server()->SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    https_server()->ServeFilesFromSourceDirectory(GetChromeTestDataDir());

    set_open_about_blank_on_browser_launch(true);
    ASSERT_TRUE(https_server()->InitializeAndListen());
    InteractiveBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    InteractiveBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    content::SetupCrossSiteRedirector(https_server());
    https_server()->StartAcceptingConnections();
    histograms_ = std::make_unique<base::HistogramTester>();
    histograms_->ExpectTotalCount(kCookiesDialogHistogramName, 0);
    incognito_browser_ = CreateIncognitoBrowser();
    InstrumentTab(incognito_browser_, kWebContentsElementId);
  }

  void TearDownOnMainThread() override {
    incognito_browser_ = nullptr;
    histograms_.reset();
    EXPECT_TRUE(https_server()->ShutdownAndWaitUntilComplete());
    InteractiveBrowserTest::TearDownOnMainThread();
  }

  net::EmbeddedTestServer* https_server() { return https_server_.get(); }

  // Returns a callback that queries an expected histogram count.
  auto ExpectActionCount(PageSpecificSiteDataDialogAction action, int count) {
    return base::BindLambdaForTesting([this, action, count]() {
      histograms().ExpectBucketCount(kCookiesDialogHistogramName, action,
                                     count);
    });
  }

  // Returns a common sequence of setup steps for all tests.
  MultiStep NavigateAndOpenDialog(ui::ElementIdentifier section_id) {
    const GURL third_party_cookie_page_url = https_server()->GetURL(
        "a.test", "/third_party_partitioned_cookies.html");
    return Steps(
        NavigateWebContents(kWebContentsElementId, third_party_cookie_page_url),
        PressButton(kLocationIconElementId),
        PressButton(PageInfoMainView::kCookieButtonElementId),
        PressButton(PageInfoCookiesContentView::kCookieDialogButton),
        InAnyContext(AfterShow(
            section_id,
            ExpectActionCount(PageSpecificSiteDataDialogAction::kDialogOpened,
                              1))));
  }

  // Returns a test step that verifies that the label for `row` matches
  // `string_id`.
  auto CheckRowLabel(ElementSpecifier row, int string_id) {
    return CheckView(row, base::BindOnce([](SiteDataRowView* row) {
                       return row->state_label_for_testing()->GetText();
                     }),
                     l10n_util::GetStringUTF16(string_id));
  }

  // Returns a step that opens the menu for a SiteDataRow.
  auto OpenRowMenu(ElementSpecifier row) {
    return WithElement(
        row, base::BindOnce([](ui::TrackedElement* el) {
          views::test::InteractionTestUtilSimulatorViews::PressButton(
              AsView<SiteDataRowView>(el)->menu_button_for_testing());
        }));
  }

  // Returns a step that clicks the delete button on a SiteDataRow.
  auto DeleteRow(ElementSpecifier row) {
    return WithElement(
        row, base::BindOnce([](ui::TrackedElement* el) {
          views::test::InteractionTestUtilSimulatorViews::PressButton(
              AsView<SiteDataRowView>(el)->delete_button_for_testing());
        }));
  }

  const base::HistogramTester& histograms() const { return *histograms_; }
  ui::ElementContext context() const {
    return incognito_browser_->window()->GetElementContext();
  }

 private:
  base::raw_ptr<Browser> incognito_browser_ = nullptr;
  std::unique_ptr<base::HistogramTester> histograms_;
  base::test::ScopedFeatureList feature_list_;
  std::unique_ptr<net::EmbeddedTestServer> https_server_;
};

IN_PROC_BROWSER_TEST_F(PageSpecificSiteDataDialogInteractiveUiTest,
                       FirstPartyAllowed) {
  RunTestSequenceInContext(
      context(),
      NavigateAndOpenDialog(kPageSpecificSiteDataDialogFirstPartySection),
      // Name the first row in the first-party section.
      InAnyContext(NameChildView(kPageSpecificSiteDataDialogFirstPartySection,
                                 kFirstPartyAllowedRow, 0)),
      // Verify no empty state label is present.
      EnsureNotPresent(kPageSpecificSiteDataDialogEmptyStateLabel,
                       /* in_any_context =*/true),
      // Verify the row label and open the row menu.
      CheckRowLabel(kFirstPartyAllowedRow,
                    IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_ALLOWED_STATE_SUBTITLE),
      OpenRowMenu(kFirstPartyAllowedRow),
      // Verify that the menu has "Block" and "Clear on exit" menu items.
      InAnyContext(WaitForShow(SiteDataRowView::kBlockMenuItem)),
      InAnyContext(WaitForShow(SiteDataRowView::kClearOnExitMenuItem)),
      // Verify that "Allow" is not present as it is already allowed.
      EnsureNotPresent(SiteDataRowView::kAllowMenuItem,
                       /* in_any_context =*/true),
      // Verify that the site can be deleted.
      DeleteRow(kFirstPartyAllowedRow),
      // Verify that UI has updated as a result of clicking on a menu item and
      // the correct histogram was logged.
      AfterHide(
          kFirstPartyAllowedRow,
          ExpectActionCount(PageSpecificSiteDataDialogAction::kSiteDeleted, 1)),
      // Verify that after deleting the last (and only) row in a section, a
      // label explaining the empty state is shown.
      InAnyContext(CheckViewProperty(
          kPageSpecificSiteDataDialogEmptyStateLabel, &views::Label::GetText,
          l10n_util::GetStringUTF16(
              IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_EMPTY_STATE_LABEL))));
}

IN_PROC_BROWSER_TEST_F(PageSpecificSiteDataDialogInteractiveUiTest,
                       ThirdPartyBlocked) {
  RunTestSequenceInContext(
      context(),
      NavigateAndOpenDialog(kPageSpecificSiteDataDialogThirdPartySection),
      // Name the third-party cookies row.
      InAnyContext(NameChildView(kPageSpecificSiteDataDialogThirdPartySection,
                                 kThirdPartyBlockedRow, 2)),
      CheckRowLabel(kThirdPartyBlockedRow,
                    IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_BLOCKED_STATE_SUBTITLE),
      OpenRowMenu(kThirdPartyBlockedRow),
      // Verify that the menu has "Clear on exit" and "Allow" menu items.
      InAnyContext(WaitForShow(SiteDataRowView::kClearOnExitMenuItem)),
      InAnyContext(WaitForShow(SiteDataRowView::kAllowMenuItem)),
      // Verify that the menu doesn't have the "Block" or "Delete" menu items
      // because it is already blocked.
      EnsureNotPresent(SiteDataRowView::kBlockMenuItem,
                       /* in_any_context =*/true),
      InAnyContext(SelectMenuItem(SiteDataRowView::kAllowMenuItem)),
      // Wait until custom event happens (triggered when any menu item
      // callback is called). Menu item is accepted on Mac async, after
      // closure animation finished.
      WaitForEvent(kThirdPartyBlockedRow, kSiteRowMenuItemClicked),
      CheckRowLabel(kThirdPartyBlockedRow,
                    IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_ALLOWED_STATE_SUBTITLE),
      Do(ExpectActionCount(PageSpecificSiteDataDialogAction::kSiteAllowed, 1)),
      // Verify that after allowing a site, it can be deleted.
      DeleteRow(kThirdPartyBlockedRow),
      // Verify that UI has updated as a result of clicking on the delete
      // button and the correct histogram was logged.
      AfterHide(kThirdPartyBlockedRow,
                ExpectActionCount(
                    PageSpecificSiteDataDialogAction::kSiteDeleted, 1)));
}

IN_PROC_BROWSER_TEST_F(PageSpecificSiteDataDialogInteractiveUiTest,
                       OnlyPartitionedBlockedThirdPartyCookies) {
  RunTestSequenceInContext(
      context(),
      NavigateAndOpenDialog(kPageSpecificSiteDataDialogThirdPartySection),
      // Find the third party section and name the row with partitioned only
      // access (b.test).
      InAnyContext(NameChildView(kPageSpecificSiteDataDialogThirdPartySection,
                                 kOnlyPartitionedRow, 0)),
      CheckRowLabel(
          kOnlyPartitionedRow,
          IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_PARTITIONED_STATE_SUBTITLE),
      OpenRowMenu(kOnlyPartitionedRow),
      // Verify that the menu has "Clear on exit", "Allow" and "Block" menu
      // items. Even though the website didn't try to access third party
      // cookies, the allow option is still offered for consistency.
      InAnyContext(WaitForShow(SiteDataRowView::kClearOnExitMenuItem)),
      InAnyContext(WaitForShow(SiteDataRowView::kAllowMenuItem)),
      // Block the site.
      InAnyContext(SelectMenuItem(SiteDataRowView::kBlockMenuItem)),
      // Wait until custom event happens (triggered when any menu item callback
      // is called). Menu item is accepted on Mac async, after closure
      // animation finished. Also check the blocked histogram.
      WaitForEvent(kOnlyPartitionedRow, kSiteRowMenuItemClicked),

      CheckRowLabel(kOnlyPartitionedRow,
                    IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_BLOCKED_STATE_SUBTITLE),
      Do(ExpectActionCount(PageSpecificSiteDataDialogAction::kSiteBlocked, 1)));
}

IN_PROC_BROWSER_TEST_F(PageSpecificSiteDataDialogInteractiveUiTest,
                       MixedPartitionedBlockedThirdPartyCookies) {
  RunTestSequenceInContext(
      context(),
      NavigateAndOpenDialog(kPageSpecificSiteDataDialogThirdPartySection),
      // Find the third party section and name the row with mixed storage
      // access (c.test).
      InAnyContext(NameChildView(kPageSpecificSiteDataDialogThirdPartySection,
                                 kMixedPartitionedRow, 1)),
      CheckRowLabel(
          kMixedPartitionedRow,
          IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_PARTITIONED_STATE_SUBTITLE),
      OpenRowMenu(kMixedPartitionedRow),
      // Verify that the menu has "Clear on exit", "Allow" and "Block" menu
      // items.
      InAnyContext(WaitForShow(SiteDataRowView::kClearOnExitMenuItem)),
      InAnyContext(WaitForShow(SiteDataRowView::kBlockMenuItem)),
      // "Allow" menu item is shown because the site has access 3PC and they
      // were blocked. "Allow" menu item is here to allow 3PC access. It also
      // has special string that specifies allowing 3PC.
      InAnyContext(CheckViewProperty(
          SiteDataRowView::kAllowMenuItem, &views::MenuItemView::title,
          l10n_util::GetStringUTF16(
              IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_ALLOW_THIRD_PARTY_MENU_ITEM))),
      InAnyContext(SelectMenuItem(SiteDataRowView::kAllowMenuItem)),
      // Wait until custom event happens (triggered when any menu item callback
      // is called). Menu item is accepted on Mac async, after closure animation
      // finished.
      WaitForEvent(kMixedPartitionedRow, kSiteRowMenuItemClicked),
      // Verify that UI has updated as a result of clicking on a menu
      // item and the correct histogram was logged.
      CheckRowLabel(kMixedPartitionedRow,
                    IDS_PAGE_SPECIFIC_SITE_DATA_DIALOG_ALLOWED_STATE_SUBTITLE),
      Do(ExpectActionCount(PageSpecificSiteDataDialogAction::kSiteAllowed, 1)));
}
