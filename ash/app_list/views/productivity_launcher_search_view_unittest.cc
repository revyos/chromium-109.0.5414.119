// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/app_list/views/productivity_launcher_search_view.h"

#include <tuple>
#include <utility>

#include "ash/app_list/app_list_controller_impl.h"
#include "ash/app_list/app_list_model_provider.h"
#include "ash/app_list/model/app_list_test_model.h"
#include "ash/app_list/model/search/test_search_result.h"
#include "ash/app_list/test/app_list_test_helper.h"
#include "ash/app_list/views/app_list_bubble_search_page.h"
#include "ash/app_list/views/result_selection_controller.h"
#include "ash/app_list/views/search_box_view.h"
#include "ash/app_list/views/search_result_image_list_view.h"
#include "ash/app_list/views/search_result_image_view_delegate.h"
#include "ash/app_list/views/search_result_list_view.h"
#include "ash/app_list/views/search_result_page_view.h"
#include "ash/constants/ash_features.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/compositor/test/layer_animation_stopped_waiter.h"
#include "ui/compositor/test/test_utils.h"
#include "ui/events/keycodes/keyboard_codes_posix.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/test/ax_event_counter.h"
#include "ui/views/view_utils.h"

namespace {

int kDefaultSearchItems = 3;
const uint64_t kSearchResultImageViewResultCount = 4;
// SearchResultListViewType is 0 indexed so we need to add 1 here.
const int kResultContainersCount =
    static_cast<int>(
        ash::SearchResultListView::SearchResultListType::kMaxValue) +
    1;

}  // namespace

namespace ash {

// Parameterized based on whether the search view is shown within the clamshell
// or tablet mode launcher UI.
class ProductivityLauncherSearchViewTest
    : public AshTestBase,
      public testing::WithParamInterface<bool> {
 public:
  ProductivityLauncherSearchViewTest()
      : AshTestBase((base::test::TaskEnvironment::TimeSource::MOCK_TIME)),
        test_under_tablet_(GetParam()) {}
  ProductivityLauncherSearchViewTest(
      const ProductivityLauncherSearchViewTest&) = delete;
  ProductivityLauncherSearchViewTest& operator=(
      const ProductivityLauncherSearchViewTest&) = delete;
  ~ProductivityLauncherSearchViewTest() override = default;

  void SetUp() override {
    AshTestBase::SetUp();

    if (test_under_tablet_)
      Shell::Get()->tablet_mode_controller()->SetEnabledForTest(true);
  }

  bool tablet_mode() const { return test_under_tablet_; }

  void SetUpSearchResults(SearchModel::SearchResults* results,
                          int init_id,
                          int new_result_count,
                          int display_score,
                          bool best_match,
                          SearchResult::Category category) {
    for (int i = 0; i < new_result_count; ++i) {
      std::unique_ptr<TestSearchResult> result =
          std::make_unique<TestSearchResult>();
      result->set_result_id(base::NumberToString(init_id + i));
      result->set_display_type(ash::SearchResultDisplayType::kList);
      result->SetTitle(
          base::UTF8ToUTF16(base::StringPrintf("Result %d", init_id + i)));
      result->set_display_score(display_score);
      result->SetDetails(u"Detail");
      result->set_best_match(best_match);
      result->set_category(category);
      results->Add(std::move(result));
    }
  }

  void SetUpAnswerCardResult(SearchModel::SearchResults* results,
                             int init_id,
                             int new_result_count) {
    std::unique_ptr<TestSearchResult> result =
        std::make_unique<TestSearchResult>();
    result->set_result_id(base::NumberToString(init_id));
    result->set_display_type(ash::SearchResultDisplayType::kAnswerCard);
    result->SetTitle(base::UTF8ToUTF16(base::StringPrintf("Answer Card")));
    result->set_display_score(1000);
    result->SetDetails(u"Answer Card Details");
    result->set_best_match(false);
    results->Add(std::move(result));
  }

  SearchResultListView::SearchResultListType GetListType(
      SearchResultContainerView* result_container_view) {
    return static_cast<SearchResultListView*>(result_container_view)
        ->list_type_for_test();
  }

  std::u16string GetListLabel(
      SearchResultContainerView* result_container_view) {
    return static_cast<SearchResultListView*>(result_container_view)
        ->title_label_for_test()
        ->GetText();
  }

  ProductivityLauncherSearchView* GetProductivityLauncherSearchView() {
    if (tablet_mode()) {
      return GetAppListTestHelper()
          ->GetFullscreenSearchResultPageView()
          ->productivity_launcher_search_view_for_test();
    }
    return GetAppListTestHelper()->GetProductivityLauncherSearchView();
  }

  views::View* GetSearchPage() {
    if (tablet_mode()) {
      return GetAppListTestHelper()->GetFullscreenSearchResultPageView();
    }
    return GetAppListTestHelper()->GetBubbleSearchPage();
  }

  // Returns the layer that is used to animate search results page hide/show.
  ui::Layer* GetSearchPageAnimationLayer() {
    if (tablet_mode())
      return GetSearchPage()->layer();
    return GetProductivityLauncherSearchView()->GetPageAnimationLayer();
  }

  bool IsSearchResultPageVisible() { return GetSearchPage()->GetVisible(); }

  std::vector<size_t> GetVisibleResultContainers() {
    std::vector<SearchResultContainerView*> result_containers =
        GetProductivityLauncherSearchView()->result_container_views_for_test();
    std::vector<size_t> visible_result_containers = {};
    for (size_t i = 0; i < result_containers.size(); i++) {
      if (result_containers[i]->GetVisible())
        visible_result_containers.push_back(i);
    }
    return visible_result_containers;
  }

  SearchBoxView* GetSearchBoxView() {
    if (tablet_mode())
      return GetAppListTestHelper()->GetSearchBoxView();
    return GetAppListTestHelper()->GetBubbleSearchBoxView();
  }

  SearchResultView* GetSearchResultView(size_t container_index,
                                        int view_index) {
    std::vector<SearchResultContainerView*> result_containers =
        GetProductivityLauncherSearchView()->result_container_views_for_test();
    if (container_index >= result_containers.size()) {
      ADD_FAILURE() << "Container index out of bounds";
      return nullptr;
    }

    if (view_index >= result_containers[container_index]->num_results()) {
      ADD_FAILURE() << "View index out of bounds";
      return nullptr;
    }

    SearchResultBaseView* result_view =
        result_containers[container_index]->GetResultViewAt(view_index);
    if (!views::IsViewClass<SearchResultView>(result_view)) {
      ADD_FAILURE() << "Not a list result view";
      return nullptr;
    }

    return static_cast<SearchResultView*>(result_view);
  }

 private:
  const bool test_under_tablet_;
};

// An extension of ProductivityLauncherSearchViewTest to test launcher image
// search.
class SearchResultImageViewTest : public ProductivityLauncherSearchViewTest {
 public:
  SearchResultImageViewTest() {
    scoped_feature_list_.InitAndEnableFeature(
        features::kProductivityLauncherImageSearch);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

INSTANTIATE_TEST_SUITE_P(Tablet,
                         ProductivityLauncherSearchViewTest,
                         testing::Bool());

INSTANTIATE_TEST_SUITE_P(Tablet, SearchResultImageViewTest, testing::Bool());

TEST_P(SearchResultImageViewTest, ImageListViewVisible) {
  GetAppListTestHelper()->ShowAppList();

  TestAppListClient* const client = GetAppListTestHelper()->app_list_client();
  client->set_search_callback(
      base::BindLambdaForTesting([&](const std::u16string& query) {
        if (query.empty()) {
          AppListModelProvider::Get()->search_model()->DeleteAllResults();
          return;
        }
        EXPECT_EQ(u"a", query);

        auto* test_helper = GetAppListTestHelper();
        SearchModel::SearchResults* results = test_helper->GetSearchResults();
        SetUpAnswerCardResult(results, 1, 1);
      }));

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  // Check result container visibility.
  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  // Answer card container should be visible.
  EXPECT_TRUE(result_containers[0]->GetVisible());
  // Best Match container should not be visible.
  EXPECT_FALSE(result_containers[1]->GetVisible());
  // SearchResultImageListView container should be visible.
  EXPECT_TRUE(result_containers[2]->GetVisible());

  std::vector<SearchResultImageView*> search_result_image_views =
      static_cast<SearchResultImageListView*>(result_containers[2])
          ->GetSearchResultImageViews();

  // The SearchResultImageListView should have four visible result views.
  EXPECT_EQ(kSearchResultImageViewResultCount,
            search_result_image_views.size());
  for (auto* search_result_image_view : search_result_image_views)
    EXPECT_TRUE(search_result_image_view->GetVisible());

  client->set_search_callback(TestAppListClient::SearchCallback());
}

TEST_P(SearchResultImageViewTest, ShowContextMenu) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  SearchModel::SearchResults* results = test_helper->GetSearchResults();
  SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                     SearchResult::Category::kApps);

  // Check result container visibility.
  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // SearchResultImageListView container should be visible.
  ASSERT_TRUE(
      views::IsViewClass<SearchResultImageListView>(result_containers[2]));
  EXPECT_TRUE(result_containers[2]->GetVisible());
  auto* search_result_image_view = result_containers[2]->GetResultViewAt(2);
  ASSERT_TRUE(search_result_image_view->GetVisible());
  ASSERT_TRUE(
      views::IsViewClass<SearchResultImageView>(search_result_image_view));

  // Perform a long tap on `search_result_image_view`.
  search_result_image_view->GetWidget()->LayoutRootViewIfNecessary();
  auto image_view_center_point =
      search_result_image_view->GetBoundsInScreen().CenterPoint();
  auto* event_generator = GetEventGenerator();
  ui::GestureEvent long_tap(image_view_center_point.x(),
                            image_view_center_point.y(), 0, base::TimeTicks(),
                            ui::GestureEventDetails(ui::ET_GESTURE_LONG_TAP));
  event_generator->Dispatch(&long_tap);

  // The `SearchResultImageViewDelegate` should be showing a context menu.
  EXPECT_TRUE(SearchResultImageViewDelegate::Get()->HasActiveContextMenu());
}

TEST_P(ProductivityLauncherSearchViewTest, AnimateSearchResultView) {
  // Enable animations.
  ui::ScopedAnimationDurationScaleMode duration(
      ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);

  GetAppListTestHelper()->ShowAppList();

  TestAppListClient* const client = GetAppListTestHelper()->app_list_client();
  client->set_search_callback(
      base::BindLambdaForTesting([&](const std::u16string& query) {
        if (query.empty()) {
          AppListModelProvider::Get()->search_model()->DeleteAllResults();
          return;
        }
        EXPECT_EQ(u"a", query);

        auto* test_helper = GetAppListTestHelper();
        SearchModel::SearchResults* results = test_helper->GetSearchResults();
        // Create categorized results and order categories as {kApps, kWeb}.
        std::vector<AppListSearchResultCategory>* ordered_categories =
            test_helper->GetOrderedResultCategories();
        ordered_categories->push_back(AppListSearchResultCategory::kApps);
        ordered_categories->push_back(AppListSearchResultCategory::kWeb);
        SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                           SearchResult::Category::kApps);
        SetUpSearchResults(results, 1 + kDefaultSearchItems,
                           kDefaultSearchItems, 1, false,
                           SearchResult::Category::kWeb);
      }));

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  // Verify that search containers have a scheduled update, and ensure they get
  // run.
  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Check result container visibility.
  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);

  EXPECT_TRUE(result_containers[2]->GetVisible());
  SearchResultView* app_result = GetSearchResultView(2, 0);
  ASSERT_TRUE(app_result);
  EXPECT_EQ(app_result->layer()->opacity(), 0.0f);
  EXPECT_EQ(app_result->layer()->GetTargetOpacity(), 1.0f);

  EXPECT_TRUE(result_containers[3]->GetVisible());
  SearchResultView* web_result = GetSearchResultView(3, 0);
  ASSERT_TRUE(web_result);
  EXPECT_EQ(web_result->layer()->opacity(), 0.0f);
  EXPECT_EQ(web_result->layer()->GetTargetOpacity(), 1.0f);

  ui::LayerAnimationStoppedWaiter().Wait(app_result->layer());
  ui::LayerAnimationStoppedWaiter().Wait(web_result->layer());

  EXPECT_TRUE(result_containers[2]->GetVisible());
  app_result = GetSearchResultView(2, 0);
  ASSERT_TRUE(app_result);
  EXPECT_EQ(app_result->layer()->opacity(), 1.0f);
  EXPECT_EQ(app_result->layer()->GetTargetOpacity(), 1.0f);

  EXPECT_TRUE(result_containers[3]->GetVisible());
  web_result = GetSearchResultView(3, 0);
  ASSERT_TRUE(web_result);
  EXPECT_EQ(web_result->layer()->opacity(), 1.0f);
  EXPECT_EQ(web_result->layer()->GetTargetOpacity(), 1.0f);

  client->set_search_callback(TestAppListClient::SearchCallback());
}

TEST_P(ProductivityLauncherSearchViewTest, ResultContainerIsVisible) {
  GetAppListTestHelper()->ShowAppList();

  TestAppListClient* const client = GetAppListTestHelper()->app_list_client();
  client->set_search_callback(
      base::BindLambdaForTesting([&](const std::u16string& query) {
        if (query.empty()) {
          AppListModelProvider::Get()->search_model()->DeleteAllResults();
          return;
        }
        EXPECT_EQ(u"a", query);

        auto* test_helper = GetAppListTestHelper();
        SearchModel::SearchResults* results = test_helper->GetSearchResults();
        SetUpAnswerCardResult(results, 1, 1);
      }));

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Check result container visibility.
  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  EXPECT_TRUE(result_containers[0]->GetVisible());

  // Clear search, and verify result containers get hidden.
  PressAndReleaseKey(ui::VKEY_ESCAPE);

  result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_FALSE(container->UpdateScheduled());

  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  EXPECT_FALSE(result_containers[0]->GetVisible());

  client->set_search_callback(TestAppListClient::SearchCallback());
}

TEST_P(ProductivityLauncherSearchViewTest,
       SearchResultsAreVisibleDuringHidePageAnimation) {
  auto* helper = GetAppListTestHelper();
  helper->ShowAppList();

  TestAppListClient* const client = GetAppListTestHelper()->app_list_client();
  client->set_search_callback(
      base::BindLambdaForTesting([&](const std::u16string& query) {
        if (query.empty()) {
          AppListModelProvider::Get()->search_model()->DeleteAllResults();
          return;
        }
        EXPECT_EQ(u"a", query);

        auto* test_helper = GetAppListTestHelper();
        SearchModel::SearchResults* results = test_helper->GetSearchResults();
        // Create categorized results and order categories as {kApps, kWeb}.
        std::vector<AppListSearchResultCategory>* ordered_categories =
            test_helper->GetOrderedResultCategories();
        ordered_categories->push_back(AppListSearchResultCategory::kApps);
        ordered_categories->push_back(AppListSearchResultCategory::kWeb);
        SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                           SearchResult::Category::kApps);
        SetUpSearchResults(results, 1 + kDefaultSearchItems,
                           kDefaultSearchItems, 1, false,
                           SearchResult::Category::kWeb);
      }));

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  SearchResultView* app_result = GetSearchResultView(2, 0);
  ASSERT_TRUE(app_result);

  SearchResultView* web_result = GetSearchResultView(3, 0);
  ASSERT_TRUE(app_result);

  // Enable animations.
  ui::ScopedAnimationDurationScaleMode duration(
      ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);

  // Press backspace to delete the query and switch back to the apps page.
  PressAndReleaseKey(ui::VKEY_BACK);

  // Verify that clearing search results did not schedule a container update,
  // and that result view text has not been cleared.
  for (auto* container : result_containers)
    EXPECT_FALSE(container->UpdateScheduled());

  // Verify results are visible while animating out.
  EXPECT_TRUE(result_containers[2]->GetVisible());
  app_result = GetSearchResultView(2, 0);
  ASSERT_TRUE(app_result);
  EXPECT_EQ(app_result->layer()->GetTargetOpacity(), 1.0f);
  EXPECT_TRUE(app_result->get_title_container_for_test()->GetVisible());

  EXPECT_TRUE(result_containers[3]->GetVisible());
  web_result = GetSearchResultView(3, 0);
  ASSERT_TRUE(web_result);
  EXPECT_EQ(web_result->layer()->GetTargetOpacity(), 1.0f);
  EXPECT_TRUE(web_result->get_title_container_for_test()->GetVisible());

  // Wait for search page to finish animating, and verify the containers have
  // been cleared and hidden.
  ui::LayerAnimationStoppedWaiter().Wait(GetSearchPageAnimationLayer());

  EXPECT_FALSE(result_containers[2]->GetVisible());
  EXPECT_EQ(0, result_containers[2]->num_results());
  EXPECT_FALSE(app_result->get_title_container_for_test()->GetVisible());

  EXPECT_FALSE(result_containers[3]->GetVisible());
  EXPECT_EQ(0, result_containers[3]->num_results());
  EXPECT_FALSE(web_result->get_title_container_for_test()->GetVisible());

  client->set_search_callback(TestAppListClient::SearchCallback());
}

// Tests that attempts to change selection during results hide animation are
// handed gracefully.
TEST_P(ProductivityLauncherSearchViewTest, SelectionChangeDuringHide) {
  auto* helper = GetAppListTestHelper();
  helper->ShowAppList();

  TestAppListClient* const client = GetAppListTestHelper()->app_list_client();
  client->set_search_callback(
      base::BindLambdaForTesting([&](const std::u16string& query) {
        if (query.empty()) {
          AppListModelProvider::Get()->search_model()->DeleteAllResults();
          return;
        }
        EXPECT_EQ(u"a", query);

        auto* test_helper = GetAppListTestHelper();
        SearchModel::SearchResults* results = test_helper->GetSearchResults();
        // Create categorized results and order categories as {kApps, kWeb}.
        std::vector<AppListSearchResultCategory>* ordered_categories =
            test_helper->GetOrderedResultCategories();
        ordered_categories->push_back(AppListSearchResultCategory::kApps);
        ordered_categories->push_back(AppListSearchResultCategory::kWeb);
        SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                           SearchResult::Category::kApps);
        SetUpSearchResults(results, 1 + kDefaultSearchItems,
                           kDefaultSearchItems, 1, false,
                           SearchResult::Category::kWeb);
      }));

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Enable animations.
  ui::ScopedAnimationDurationScaleMode duration(
      ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);

  // Press backspace to delete the query and switch back to the apps page.
  PressAndReleaseKey(ui::VKEY_BACK);

  // Verify that clearing search results did not schedule a container update,
  // and that result view text has not been cleared.
  for (auto* container : result_containers)
    EXPECT_FALSE(container->UpdateScheduled());

  // Simulate user trying to update selection during animation - verify this
  // does not cause crash, and that the selection gets cleared once the search
  // results UI hides.
  PressAndReleaseKey(ui::VKEY_DOWN);

  // Wait for search page to finish animating, and verify the containers have
  // been cleared and hidden.
  ui::LayerAnimationStoppedWaiter().Wait(GetSearchPageAnimationLayer());

  EXPECT_FALSE(GetProductivityLauncherSearchView()
                   ->result_selection_controller_for_test()
                   ->selected_result());

  client->set_search_callback(TestAppListClient::SearchCallback());
}

// Tests that key traversal correctly cycles between the list of results and
// search box close button.
TEST_P(ProductivityLauncherSearchViewTest, ResultSelectionCycle) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();
  EXPECT_FALSE(GetProductivityLauncherSearchView()->CanSelectSearchResults());

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);
  SearchModel::SearchResults* results = test_helper->GetSearchResults();

  // Create categorized results and order categories as {kApps, kWeb}.
  std::vector<AppListSearchResultCategory>* ordered_categories =
      test_helper->GetOrderedResultCategories();
  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  ordered_categories->push_back(AppListSearchResultCategory::kApps);
  ordered_categories->push_back(AppListSearchResultCategory::kWeb);
  SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                     SearchResult::Category::kApps);
  SetUpSearchResults(results, 1 + kDefaultSearchItems, kDefaultSearchItems, 1,
                     false, SearchResult::Category::kWeb);

  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Press VKEY_DOWN and check if the first result view is selected.
  EXPECT_TRUE(GetProductivityLauncherSearchView()->CanSelectSearchResults());
  ResultSelectionController* controller =
      GetProductivityLauncherSearchView()
          ->result_selection_controller_for_test();

  // Traverse the first results container.
  for (int i = 0; i < kDefaultSearchItems - 1; ++i) {
    PressAndReleaseKey(ui::VKEY_DOWN);
    ASSERT_TRUE(controller->selected_result()) << i;
    EXPECT_EQ(controller->selected_location_details()->container_index, 2) << i;
    EXPECT_EQ(controller->selected_location_details()->result_index, i + 1);
  }

  // Traverse the second container.
  for (int i = 0; i < kDefaultSearchItems; ++i) {
    PressAndReleaseKey(ui::VKEY_DOWN);
    ASSERT_TRUE(controller->selected_result()) << i;
    EXPECT_EQ(controller->selected_location_details()->container_index, 3) << i;
    EXPECT_EQ(controller->selected_location_details()->result_index, i);
  }

  // Pressing down while the last result is selected moves focus to the close
  // button.
  PressAndReleaseKey(ui::VKEY_DOWN);

  EXPECT_FALSE(controller->selected_result());
  EXPECT_TRUE(GetSearchBoxView()->close_button()->HasFocus());

  // Move focus the the search box, and verify result selection is properly set.
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_TRUE(GetSearchBoxView()->search_box()->HasFocus());

  ASSERT_TRUE(controller->selected_result());
  EXPECT_EQ(controller->selected_location_details()->container_index, 2);
  EXPECT_EQ(controller->selected_location_details()->result_index, 0);

  // Up key should cycle focus to the close button, and then the last search
  // result.
  PressAndReleaseKey(ui::VKEY_UP);
  EXPECT_FALSE(controller->selected_result());
  EXPECT_TRUE(GetSearchBoxView()->close_button()->HasFocus());

  PressAndReleaseKey(ui::VKEY_UP);
  EXPECT_TRUE(GetSearchBoxView()->search_box()->HasFocus());

  ASSERT_TRUE(controller->selected_result());
  EXPECT_EQ(controller->selected_location_details()->container_index, 3);
  EXPECT_EQ(controller->selected_location_details()->result_index,
            kDefaultSearchItems - 1);
}

TEST_P(ProductivityLauncherSearchViewTest, AnswerCardSelection) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();

  EXPECT_FALSE(GetProductivityLauncherSearchView()->CanSelectSearchResults());

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  SearchModel::SearchResults* results = test_helper->GetSearchResults();

  // Create categorized results and order categories as {kApps}.
  std::vector<ash::AppListSearchResultCategory>* ordered_categories =
      test_helper->GetOrderedResultCategories();
  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  ordered_categories->push_back(ash::AppListSearchResultCategory::kApps);
  SetUpSearchResults(results, 1, kDefaultSearchItems, 1, false,
                     SearchResult::Category::kApps);

  // Verify result container ordering.
  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  SetUpAnswerCardResult(results, 1, 1);
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  EXPECT_EQ(GetVisibleResultContainers(), (std::vector<size_t>{0, 2}));

  EXPECT_TRUE(GetProductivityLauncherSearchView()->CanSelectSearchResults());
  ResultSelectionController* controller =
      GetProductivityLauncherSearchView()
          ->result_selection_controller_for_test();
  // Press VKEY_DOWN and check if the next is selected.
  EXPECT_EQ(controller->selected_location_details()->container_index, 0);
  EXPECT_EQ(controller->selected_location_details()->result_index, 0);
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(controller->selected_location_details()->container_index, 2);
  EXPECT_EQ(controller->selected_location_details()->result_index, 0);
  PressAndReleaseKey(ui::VKEY_UP);
  EXPECT_EQ(controller->selected_location_details()->container_index, 0);
  EXPECT_EQ(controller->selected_location_details()->result_index, 0);
}

// Tests that result selection controller can change between  within and between
// result containers.
TEST_P(ProductivityLauncherSearchViewTest, ResultSelection) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();
  EXPECT_FALSE(GetProductivityLauncherSearchView()->CanSelectSearchResults());

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);
  SearchModel::SearchResults* results = test_helper->GetSearchResults();

  // Create categorized results and order categories as {kApps, kWeb}.
  std::vector<AppListSearchResultCategory>* ordered_categories =
      test_helper->GetOrderedResultCategories();
  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  ordered_categories->push_back(AppListSearchResultCategory::kApps);
  ordered_categories->push_back(AppListSearchResultCategory::kWeb);
  SetUpSearchResults(results, 2, kDefaultSearchItems, 100, false,
                     SearchResult::Category::kApps);
  SetUpSearchResults(results, 2 + kDefaultSearchItems, kDefaultSearchItems, 1,
                     false, SearchResult::Category::kWeb);

  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  EXPECT_EQ(GetVisibleResultContainers(), (std::vector<size_t>{2, 3}));

  // Press VKEY_DOWN and check if the first result view is selected.
  EXPECT_TRUE(GetProductivityLauncherSearchView()->CanSelectSearchResults());
  ResultSelectionController* controller =
      GetProductivityLauncherSearchView()
          ->result_selection_controller_for_test();
  // Tests that VKEY_DOWN selects the next result.
  EXPECT_EQ(controller->selected_location_details()->container_index, 2);
  EXPECT_EQ(controller->selected_location_details()->result_index, 0);
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(controller->selected_location_details()->container_index, 2);
  EXPECT_EQ(controller->selected_location_details()->result_index, 1);
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(controller->selected_location_details()->container_index, 2);
  EXPECT_EQ(controller->selected_location_details()->result_index, 2);
  // Tests that VKEY_DOWN while selecting the last result of the current
  // container causes the selection controller to select the next container.
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(controller->selected_location_details()->container_index, 3);
  EXPECT_EQ(controller->selected_location_details()->result_index, 0);
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(controller->selected_location_details()->container_index, 3);
  EXPECT_EQ(controller->selected_location_details()->result_index, 1);
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(controller->selected_location_details()->container_index, 3);
  EXPECT_EQ(controller->selected_location_details()->result_index, 2);
  // Tests that VKEY_UP while selecting the first result of the current
  // container causes the selection controller to select the previous container.
  PressAndReleaseKey(ui::VKEY_UP);
  PressAndReleaseKey(ui::VKEY_UP);
  PressAndReleaseKey(ui::VKEY_UP);
  EXPECT_EQ(controller->selected_location_details()->container_index, 2);
  EXPECT_EQ(controller->selected_location_details()->result_index, 2);
}

TEST_P(ProductivityLauncherSearchViewTest, ResultPageHiddenInZeroSearchState) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();

  // Tap on the search box to activate it.
  GetEventGenerator()->GestureTapAt(
      GetSearchBoxView()->GetBoundsInScreen().CenterPoint());

  EXPECT_TRUE(GetSearchBoxView()->is_search_box_active());
  EXPECT_FALSE(IsSearchResultPageVisible());

  // Set some zero-state results.
  std::vector<AppListSearchResultCategory>* ordered_categories =
      GetAppListTestHelper()->GetOrderedResultCategories();
  SearchModel::SearchResults* results = test_helper->GetSearchResults();
  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  ordered_categories->push_back(AppListSearchResultCategory::kApps);
  SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                     SearchResult::Category::kApps);

  // Verify that containers are not updating if search is not in progress.
  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_FALSE(container->UpdateScheduled());

  // Verify that keyboard traversal does not change the result selection.
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(u"", GetSearchBoxView()->search_box()->GetText());
  EXPECT_FALSE(IsSearchResultPageVisible());

  // Selection should be set if user enters a query.
  PressAndReleaseKey(ui::VKEY_A);

  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  ordered_categories->push_back(AppListSearchResultCategory::kWeb);
  SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                     SearchResult::Category::kWeb);
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  EXPECT_TRUE(GetSearchBoxView()->is_search_box_active());
  EXPECT_EQ(u"a", GetSearchBoxView()->search_box()->GetText());
  EXPECT_TRUE(IsSearchResultPageVisible());
  ResultSelectionController* controller =
      GetProductivityLauncherSearchView()
          ->result_selection_controller_for_test();
  EXPECT_TRUE(controller->selected_result());

  // Backspace should clear selection, and search box content.
  PressAndReleaseKey(ui::VKEY_BACK);

  for (auto* container : result_containers)
    EXPECT_FALSE(container->UpdateScheduled());

  EXPECT_TRUE(GetSearchBoxView()->is_search_box_active());
  EXPECT_EQ(u"", GetSearchBoxView()->search_box()->GetText());
  EXPECT_FALSE(IsSearchResultPageVisible());
}

// Verifies that search result categories are sorted properly.
TEST_P(ProductivityLauncherSearchViewTest, SearchResultCategoricalSort) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  SearchModel::SearchResults* results = test_helper->GetSearchResults();

  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);

  // Create categorized results and order categories as {kApps, kWeb}.
  std::vector<ash::AppListSearchResultCategory>* ordered_categories =
      test_helper->GetOrderedResultCategories();
  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  ordered_categories->push_back(ash::AppListSearchResultCategory::kApps);
  ordered_categories->push_back(ash::AppListSearchResultCategory::kWeb);
  SetUpSearchResults(results, 1, kDefaultSearchItems, 100, false,
                     SearchResult::Category::kApps);
  SetUpSearchResults(results, 1 + kDefaultSearchItems, kDefaultSearchItems, 1,
                     false, SearchResult::Category::kWeb);
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Verify result container visibility.
  EXPECT_EQ(GetVisibleResultContainers(), (std::vector<size_t>{2, 3}));

  // Verify title labels are correctly updated.
  EXPECT_EQ(GetListLabel(result_containers[0]), u"");
  EXPECT_EQ(GetListLabel(result_containers[1]), u"Best Match");
  EXPECT_EQ(GetListLabel(result_containers[2]), u"Apps");
  EXPECT_EQ(GetListLabel(result_containers[3]), u"Websites");

  // Verify result container ordering.
  EXPECT_EQ(GetListType(result_containers[0]),
            SearchResultListView::SearchResultListType::kAnswerCard);
  EXPECT_EQ(GetListType(result_containers[1]),
            SearchResultListView::SearchResultListType::kBestMatch);
  EXPECT_EQ(GetListType(result_containers[2]),
            SearchResultListView::SearchResultListType::kApps);
  EXPECT_EQ(GetListType(result_containers[3]),
            SearchResultListView::SearchResultListType::kWeb);

  // Create categorized results and order categories as {kWeb, kApps}.
  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  ordered_categories->push_back(ash::AppListSearchResultCategory::kWeb);
  ordered_categories->push_back(ash::AppListSearchResultCategory::kApps);
  SetUpSearchResults(results, 1, kDefaultSearchItems, 1, false,
                     SearchResult::Category::kApps);
  SetUpSearchResults(results, 1 + kDefaultSearchItems, kDefaultSearchItems, 100,
                     false, SearchResult::Category::kWeb);
  result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Verify result container visibility.
  EXPECT_EQ(GetVisibleResultContainers(), (std::vector<size_t>{2, 3}));

  EXPECT_EQ(GetListLabel(result_containers[0]), u"");
  EXPECT_EQ(GetListLabel(result_containers[1]), u"Best Match");
  EXPECT_EQ(GetListLabel(result_containers[2]), u"Websites");
  EXPECT_EQ(GetListLabel(result_containers[3]), u"Apps");

  // Verify result container ordering.

  EXPECT_EQ(GetListType(result_containers[0]),
            SearchResultListView::SearchResultListType::kAnswerCard);
  EXPECT_EQ(GetListType(result_containers[1]),
            SearchResultListView::SearchResultListType::kBestMatch);
  EXPECT_EQ(GetListType(result_containers[2]),
            SearchResultListView::SearchResultListType::kWeb);
  EXPECT_EQ(GetListType(result_containers[3]),
            SearchResultListView::SearchResultListType::kApps);

  SetUpAnswerCardResult(results, 1, 1);
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  EXPECT_EQ(GetVisibleResultContainers(), (std::vector<size_t>{0, 2, 3}));

  AppListModelProvider::Get()->search_model()->DeleteAllResults();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());
  EXPECT_EQ(GetVisibleResultContainers(), (std::vector<size_t>{}));
}

TEST_P(ProductivityLauncherSearchViewTest, SearchResultA11y) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  SearchModel::SearchResults* results = test_helper->GetSearchResults();

  // Create |kDefaultSearchItems| new search results for us to cycle through.
  SetUpSearchResults(results, 1, kDefaultSearchItems, 100, true,
                     SearchResult::Category::kApps);
  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Check result container visibility.
  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  EXPECT_TRUE(result_containers[1]->GetVisible());

  views::test::AXEventCounter ax_counter(views::AXEventManager::Get());

  // Pressing down should not generate a selection accessibility event because
  // A11Y announcements are delayed since the results list just changed.
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(0, ax_counter.GetCount(ax::mojom::Event::kActiveDescendantChanged));
  // Advance time to fire the timer to stop ignoring A11Y announcements.
  task_environment()->FastForwardBy(base::Milliseconds(5000));

  // A selection event is generated when the timer fires.
  EXPECT_EQ(1, ax_counter.GetCount(ax::mojom::Event::kActiveDescendantChanged));

  // Successive up/down key presses should generate additional selection events.
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(2, ax_counter.GetCount(ax::mojom::Event::kActiveDescendantChanged));
  PressAndReleaseKey(ui::VKEY_UP);
  EXPECT_EQ(3, ax_counter.GetCount(ax::mojom::Event::kActiveDescendantChanged));
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(4, ax_counter.GetCount(ax::mojom::Event::kActiveDescendantChanged));
  PressAndReleaseKey(ui::VKEY_DOWN);
  EXPECT_EQ(5, ax_counter.GetCount(ax::mojom::Event::kActiveDescendantChanged));
}

TEST_P(ProductivityLauncherSearchViewTest, SearchPageA11y) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  SearchModel::SearchResults* results = test_helper->GetSearchResults();

  // Delete all results and verify the bubble search page's A11yNodeData.
  AppListModelProvider::Get()->search_model()->DeleteAllResults();

  auto* search_view = GetProductivityLauncherSearchView();
  // Check result container visibility.
  std::vector<SearchResultContainerView*> result_containers =
      search_view->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  // Container view should not be shown if no result is present.
  EXPECT_FALSE(result_containers[0]->GetVisible());
  EXPECT_TRUE(search_view->GetVisible());

  ui::AXNodeData data;
  search_view->GetAccessibleNodeData(&data);
  EXPECT_EQ("Displaying 0 results for a",
            data.GetStringAttribute(ax::mojom::StringAttribute::kValue));
  // Create a single search result and and verify A11yNodeData.
  SetUpSearchResults(results, 1, 1, 100, true, SearchResult::Category::kApps);
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());
  search_view->GetAccessibleNodeData(&data);
  EXPECT_EQ("Displaying 1 result for a",
            data.GetStringAttribute(ax::mojom::StringAttribute::kValue));

  // Create new search results and and and verify A11yNodeData.
  SetUpSearchResults(results, 2, kDefaultSearchItems - 1, 100, true,
                     SearchResult::Category::kApps);
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());
  ui::AXNodeData data2;
  search_view->GetAccessibleNodeData(&data);
  EXPECT_EQ("Displaying 3 results for a",
            data.GetStringAttribute(ax::mojom::StringAttribute::kValue));
}

TEST_P(ProductivityLauncherSearchViewTest, SearchClearedOnModelUpdate) {
  auto* test_helper = GetAppListTestHelper();
  test_helper->ShowAppList();

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);

  SearchModel::SearchResults* results = test_helper->GetSearchResults();
  // Create |kDefaultSearchItems| new search results for us to cycle through.
  SetUpSearchResults(results, 1, kDefaultSearchItems, 100, true,
                     SearchResult::Category::kApps);

  std::vector<SearchResultContainerView*> result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  // Check result container visibility.
  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  EXPECT_TRUE(result_containers[1]->GetVisible());

  // Update the app list and search model, and verify the results page gets
  // hidden.
  auto app_list_model_override = std::make_unique<test::AppListTestModel>();
  auto search_model_override = std::make_unique<SearchModel>();
  Shell::Get()->app_list_controller()->SetActiveModel(
      /*profile_id=*/1, app_list_model_override.get(),
      search_model_override.get());

  EXPECT_FALSE(IsSearchResultPageVisible());
  EXPECT_EQ(u"", GetSearchBoxView()->search_box()->GetText());

  // Press a key to start a search.
  PressAndReleaseKey(ui::VKEY_A);
  SetUpSearchResults(search_model_override->results(), 2, 1, 100, true,
                     SearchResult::Category::kApps);
  result_containers =
      GetProductivityLauncherSearchView()->result_container_views_for_test();
  for (auto* container : result_containers)
    EXPECT_TRUE(container->RunScheduledUpdateForTest());

  ASSERT_EQ(static_cast<int>(result_containers.size()), kResultContainersCount);
  EXPECT_TRUE(result_containers[1]->GetVisible());
  EXPECT_EQ(1, result_containers[1]->num_results());
  EXPECT_EQ(u"Result 2", GetSearchResultView(1, 0)->result()->title());

  Shell::Get()->app_list_controller()->ClearActiveModel();
}

}  // namespace ash
