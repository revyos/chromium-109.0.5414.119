// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/banners/test_app_banner_manager_desktop.h"

#include <memory>
#include <utility>

#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/webapps/browser/installable/installable_data.h"
#include "content/public/browser/web_contents.h"

namespace webapps {

TestAppBannerManagerDesktop::TestAppBannerManagerDesktop(
    content::WebContents* web_contents)
    : AppBannerManagerDesktop(web_contents) {
  // Ensure no real instance exists. This must be the only instance to avoid
  // observers of AppBannerManager left observing the wrong one.
  DCHECK_EQ(AppBannerManagerDesktop::FromWebContents(web_contents), nullptr);
}

TestAppBannerManagerDesktop::~TestAppBannerManagerDesktop() = default;

static std::unique_ptr<AppBannerManagerDesktop> CreateTestAppBannerManager(
    content::WebContents* web_contents) {
  return std::make_unique<TestAppBannerManagerDesktop>(web_contents);
}

void TestAppBannerManagerDesktop::SetUp() {
  AppBannerManagerDesktop::override_app_banner_manager_desktop_for_testing_ =
      CreateTestAppBannerManager;
}

TestAppBannerManagerDesktop* TestAppBannerManagerDesktop::FromWebContents(
    content::WebContents* web_contents) {
  DCHECK(web_contents);
  // Must call TestAppBannerManagerDesktop::SetUp() first.
  DCHECK_EQ(
      AppBannerManagerDesktop::override_app_banner_manager_desktop_for_testing_,
      CreateTestAppBannerManager);
  auto* manager = AppBannerManagerDesktop::FromWebContents(web_contents);
  DCHECK(manager);
  DCHECK(manager->AsTestAppBannerManagerDesktopForTesting());
  return manager->AsTestAppBannerManagerDesktopForTesting();
}

void TestAppBannerManagerDesktop::WaitForInstallableCheckTearDown() {
  base::RunLoop run_loop;
  tear_down_quit_closure_ = run_loop.QuitClosure();
  run_loop.Run();
}

bool TestAppBannerManagerDesktop::WaitForInstallableCheck() {
  if (!installable_.has_value()) {
    base::RunLoop run_loop;
    installable_quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }
  // Only wait for worker check if it has started after the installable check.
  if (waiting_for_worker_) {
    base::RunLoop run_loop;
    promotable_quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }
  return *installable_ && promotable_;
}

void TestAppBannerManagerDesktop::PrepareDone(base::OnceClosure on_done) {
  on_done_ = std::move(on_done);
}

AppBannerManager::State TestAppBannerManagerDesktop::state() {
  return AppBannerManager::state();
}

void TestAppBannerManagerDesktop::AwaitAppInstall() {
  base::RunLoop loop;
  on_install_ = loop.QuitClosure();
  loop.Run();
}

void TestAppBannerManagerDesktop::OnDidGetManifest(
    const InstallableData& result) {
  AppBannerManagerDesktop::OnDidGetManifest(result);

  // AppBannerManagerDesktop does not call |OnDidPerformInstallableCheck| to
  // complete the installability check in this case, instead it early exits
  // with failure.
  if (!result.NoBlockingErrors())
    SetInstallable(false);
}
void TestAppBannerManagerDesktop::OnDidPerformInstallableWebAppCheck(
    const InstallableData& result) {
  AppBannerManagerDesktop::OnDidPerformInstallableWebAppCheck(result);
  SetInstallable(result.NoBlockingErrors());
}

void TestAppBannerManagerDesktop::PerformServiceWorkerCheck() {
  waiting_for_worker_ = true;
  AppBannerManagerDesktop::PerformServiceWorkerCheck();
}

void TestAppBannerManagerDesktop::OnDidPerformWorkerCheck(
    const InstallableData& result) {
  AppBannerManagerDesktop::OnDidPerformWorkerCheck(result);
  SetPromotable(result.NoBlockingErrors());
}

void TestAppBannerManagerDesktop::ResetCurrentPageData() {
  AppBannerManagerDesktop::ResetCurrentPageData();
  installable_.reset();
  promotable_ = false;
  waiting_for_worker_ = false;
  if (tear_down_quit_closure_)
    std::move(tear_down_quit_closure_).Run();
}

TestAppBannerManagerDesktop*
TestAppBannerManagerDesktop::AsTestAppBannerManagerDesktopForTesting() {
  return this;
}

void TestAppBannerManagerDesktop::OnInstall(blink::mojom::DisplayMode display) {
  AppBannerManager::OnInstall(display);
  if (on_install_)
    std::move(on_install_).Run();
}

void TestAppBannerManagerDesktop::DidFinishCreatingWebApp(
    const web_app::AppId& app_id,
    webapps::InstallResultCode code) {
  AppBannerManagerDesktop::DidFinishCreatingWebApp(app_id, code);
  OnFinished();
}

void TestAppBannerManagerDesktop::DidFinishLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url) {
  if (ShouldIgnore(render_frame_host, validated_url)) {
    SetInstallable(false);
    return;
  }

  AppBannerManagerDesktop::DidFinishLoad(render_frame_host, validated_url);
}

void TestAppBannerManagerDesktop::UpdateState(AppBannerManager::State state) {
  AppBannerManager::UpdateState(state);

  if (state == AppBannerManager::State::PENDING_ENGAGEMENT ||
      state == AppBannerManager::State::PENDING_PROMPT_CANCELED ||
      state == AppBannerManager::State::PENDING_PROMPT_NOT_CANCELED ||
      state == AppBannerManager::State::COMPLETE) {
    OnFinished();
  }
}

void TestAppBannerManagerDesktop::SetInstallable(bool installable) {
  DCHECK(!installable_.has_value() || installable_ == installable);
  installable_ = installable;
  if (installable_quit_closure_)
    std::move(installable_quit_closure_).Run();
}

void TestAppBannerManagerDesktop::SetPromotable(bool promotable) {
  DCHECK(waiting_for_worker_);
  waiting_for_worker_ = false;
  promotable_ = promotable;
  if (promotable_quit_closure_)
    std::move(promotable_quit_closure_).Run();
}

void TestAppBannerManagerDesktop::OnFinished() {
  if (on_done_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                  std::move(on_done_));
  }
}

}  // namespace webapps
