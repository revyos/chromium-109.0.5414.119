// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/media/webrtc/chrome_screen_enumerator.h"
#include "chrome/test/base/chrome_ash_test_base.h"
#include "content/public/common/content_features.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/window.h"
#include "ui/base/cursor/cursor.h"
#include "ui/gfx/native_widget_types.h"

class ChromeScreenEnumeratorTest : public ChromeAshTestBase {
 public:
  ChromeScreenEnumeratorTest() {}

  explicit ChromeScreenEnumeratorTest(const ChromeScreenEnumerator&) = delete;
  ChromeScreenEnumeratorTest& operator=(const ChromeScreenEnumerator&) = delete;

  ~ChromeScreenEnumeratorTest() override = default;

  void SetUp() override {
    ChromeAshTestBase::SetUp();
    enumerator_ = std::make_unique<ChromeScreenEnumerator>();
    scoped_feature_list_.InitFromCommandLine(
        /*enable_features=*/
        "GetDisplayMediaSet,GetDisplayMediaSetAutoSelectAllScreens",
        /*disable_features=*/"");
  }

  std::vector<aura::Window*> GenerateScreensList(
      const std::vector<gfx::Rect>& screens_bounds) {
    screens_.clear();
    window_delegates_.clear();
    std::vector<aura::Window*> screens;
    for (const gfx::Rect& bounds : screens_bounds) {
      auto window_delegate = std::make_unique<aura::test::TestWindowDelegate>();
      auto screen = std::make_unique<aura::Window>(window_delegate.get());
      screen->Init(ui::LayerType::LAYER_NOT_DRAWN);
      screen->SetBounds(bounds);
      screens.push_back(screen.get());
      screens_.emplace_back(std::move(screen));
      window_delegates_.emplace_back(std::move(window_delegate));
    }
    return screens;
  }

 protected:
  std::vector<std::unique_ptr<aura::test::TestWindowDelegate>>
      window_delegates_;
  std::vector<std::unique_ptr<aura::Window>> screens_;
  std::unique_ptr<ChromeScreenEnumerator> enumerator_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_F(ChromeScreenEnumeratorTest, NoScreen) {
  std::vector<aura::Window*> screens_list;
  SetRootWindowsForTesting(&screens_list);
  base::RunLoop run_loop;
  blink::mojom::StreamDevicesSetPtr actual_stream_devices_set;
  blink::mojom::MediaStreamRequestResult actual_result;
  enumerator_->EnumerateScreens(
      blink::mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE_SET,
      base::BindLambdaForTesting(
          [&run_loop, &actual_stream_devices_set, &actual_result](
              const blink::mojom::StreamDevicesSet& stream_devices_set,
              blink::mojom::MediaStreamRequestResult result) {
            actual_stream_devices_set = stream_devices_set.Clone();
            actual_result = result;
            run_loop.Quit();
          }));
  run_loop.Run();
  EXPECT_EQ(0u, actual_stream_devices_set->stream_devices.size());
  EXPECT_EQ(blink::mojom::MediaStreamRequestResult::OK, actual_result);
}

TEST_F(ChromeScreenEnumeratorTest, MultiScreenSorting) {
  std::vector<int> expected_ids_order = {3, 1, 5, 4, 2, 6};
  std::vector<aura::Window*> screens_list = GenerateScreensList({
      gfx::Rect(10, 20, 1024, 768),  // Expected as 3rd.
      gfx::Rect(20, 30, 1024, 768),  // Expected as 1st.
      gfx::Rect(5, 32, 1024, 768),   // Expected as 5th.
      gfx::Rect(20, 25, 1024, 768),  // Expected as 4th.
      gfx::Rect(20, 22, 1024, 768),  // Expected as 2nd.
      gfx::Rect(50, 1, 1024, 768),   // Expected as 6th.
  });
  SetRootWindowsForTesting(&screens_list);

  base::RunLoop run_loop;
  blink::mojom::StreamDevicesSetPtr actual_stream_devices_set;
  blink::mojom::MediaStreamRequestResult actual_result;
  enumerator_->EnumerateScreens(
      blink::mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE_SET,
      base::BindLambdaForTesting(
          [&run_loop, &actual_stream_devices_set, &actual_result](
              const blink::mojom::StreamDevicesSet& stream_devices_set,
              blink::mojom::MediaStreamRequestResult result) {
            actual_stream_devices_set = stream_devices_set.Clone();
            actual_result = result;
            run_loop.Quit();
          }));
  run_loop.Run();

  for (size_t screen_index = 0;
       screen_index < actual_stream_devices_set->stream_devices.size();
       ++screen_index) {
    EXPECT_EQ(
        base::StrCat({"screen:0:",
                      base::NumberToString(expected_ids_order[screen_index])}),
        actual_stream_devices_set->stream_devices[screen_index]
            ->video_device.value()
            .id);
  }
  EXPECT_EQ(blink::mojom::MediaStreamRequestResult::OK, actual_result);
}

TEST_F(ChromeScreenEnumeratorTest, SingleScreen) {
  std::vector<aura::Window*> screens_list =
      GenerateScreensList({gfx::Rect(20, 10, 1024, 768)});
  SetRootWindowsForTesting(&screens_list);

  base::RunLoop run_loop;
  blink::mojom::StreamDevicesSetPtr actual_stream_devices_set;
  blink::mojom::MediaStreamRequestResult actual_result;
  enumerator_->EnumerateScreens(
      blink::mojom::MediaStreamType::DISPLAY_VIDEO_CAPTURE_SET,
      base::BindLambdaForTesting(
          [&run_loop, &actual_stream_devices_set, &actual_result](
              const blink::mojom::StreamDevicesSet& stream_devices_set,
              blink::mojom::MediaStreamRequestResult result) {
            actual_stream_devices_set = stream_devices_set.Clone();
            actual_result = result;
            run_loop.Quit();
          }));
  run_loop.Run();
  EXPECT_EQ(1u, actual_stream_devices_set->stream_devices.size());
  EXPECT_EQ(blink::mojom::MediaStreamRequestResult::OK, actual_result);
}
