// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/ash/parent_access/parent_access_ui.h"

#include <memory>
#include <string>
#include <utility>

#include "base/strings/stringprintf.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "chrome/browser/ui/webui/ash/parent_access/parent_access_dialog.h"
#include "chrome/browser/ui/webui/ash/parent_access/parent_access_ui.mojom.h"
#include "chrome/browser/ui/webui/ash/parent_access/parent_access_ui_handler_impl.h"
#include "chrome/browser/ui/webui/webui_util.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace ash {

// static
signin::IdentityManager* ParentAccessUI::test_identity_manager_ = nullptr;

ParentAccessUI::ParentAccessUI(content::WebUI* web_ui)
    : ui::MojoWebDialogUI(web_ui) {
  // Set up the basic page framework.
  SetUpResources();
}

ParentAccessUI::~ParentAccessUI() = default;

// static
void ParentAccessUI::SetUpForTest(signin::IdentityManager* identity_manager) {
  test_identity_manager_ = identity_manager;
}

void ParentAccessUI::BindInterface(
    mojo::PendingReceiver<parent_access_ui::mojom::ParentAccessUIHandler>
        receiver) {
  signin::IdentityManager* identity_manager =
      test_identity_manager_
          ? test_identity_manager_
          : IdentityManagerFactory::GetForProfile(Profile::FromWebUI(web_ui()));

  // The dialog instance could be null if the webui's url is entered in the
  // browser address bar.  The handler should handle that scenario.
  mojo_api_handler_ = std::make_unique<ParentAccessUIHandlerImpl>(
      std::move(receiver), identity_manager, ParentAccessDialog::GetInstance());
}

parent_access_ui::mojom::ParentAccessUIHandler*
ParentAccessUI::GetHandlerForTest() {
  return mojo_api_handler_.get();
}

void ParentAccessUI::SetUpResources() {
  Profile* profile = Profile::FromWebUI(web_ui());
  std::unique_ptr<content::WebUIDataSource> source(
      content::WebUIDataSource::Create(chrome::kChromeUIParentAccessHost));

  // The Polymer JS bundle requires this at the moment because it sets innerHTML
  // on an element, which violates the Trusted Types CSP.
  source->DisableTrustedTypesCSP();
  source->EnableReplaceI18nInJS();

  // Forward data to the WebUI.
  source->AddResourcePath("parent_access_controller.js",
                          IDR_PARENT_ACCESS_CONTROLLER_JS);
  source->AddResourcePath("parent_access_app.js", IDR_PARENT_ACCESS_APP_JS);
  source->AddResourcePath("parent_access_ui.js", IDR_PARENT_ACCESS_UI_JS);
  source->AddResourcePath("parent_access_ui_handler.js",
                          IDR_PARENT_ACCESS_UI_HANDLER_JS);
  source->AddResourcePath("parent_access_after.js", IDR_PARENT_ACCESS_AFTER_JS);
  source->AddResourcePath("flows/local_web_approvals_after.js",
                          IDR_LOCAL_WEB_APPROVALS_AFTER_JS);
  source->AddResourcePath("parent_access_ui.mojom-webui.js",
                          IDR_PARENT_ACCESS_UI_MOJOM_WEBUI_JS);
  source->AddResourcePath("webview_manager.js",
                          IDR_PARENT_ACCESS_WEBVIEW_MANAGER_JS);

  source->UseStringsJs();
  source->SetDefaultResource(IDR_PARENT_ACCESS_HTML);

  static constexpr webui::LocalizedString kLocalizedStrings[] = {
      {"pageTitle", IDS_PARENT_ACCESS_PAGE_TITLE},
      {"approveButtonText", IDS_PARENT_ACCESS_AFTER_APPROVE_BUTTON},
      {"denyButtonText", IDS_PARENT_ACCESS_AFTER_DENY_BUTTON},
      {"localWebApprovalsAfterTitle",
       IDS_PARENT_ACCESS_LOCAL_WEB_APPROVALS_AFTER_TITLE},
      {"localWebApprovalsAfterSubtitle",
       IDS_PARENT_ACCESS_LOCAL_WEB_APPROVALS_AFTER_SUBTITLE},
      {"localWebApprovalsAfterDetails",
       IDS_PARENT_ACCESS_LOCAL_WEB_APPROVALS_AFTER_DETAILS},
      {"webviewLoadingMessage", IDS_PARENT_ACCESS_WEBVIEW_LOADING_MESSAGE},
  };
  source->AddLocalizedStrings(kLocalizedStrings);

  // Enables use of test_loader.html
  webui::SetJSModuleDefaults(source.get());

  // Allows loading of local content into an iframe for testing.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc, "frame-src chrome://test/;");

  content::WebUIDataSource::Add(profile, source.release());
}

WEB_UI_CONTROLLER_TYPE_IMPL(ParentAccessUI)

}  // namespace ash
