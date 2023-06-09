// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/net_export_helper.h"

#include <memory>
#include <utility>

#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/preloading/prefetch/no_state_prefetch/no_state_prefetch_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_manager.h"
#include "extensions/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/webui/extensions/extension_basic_info.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension_set.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/net/service_providers_win.h"
#endif

namespace chrome_browser_net {

std::unique_ptr<base::DictionaryValue> GetPrerenderInfo(Profile* profile) {
  std::unique_ptr<base::DictionaryValue> value;
  prerender::NoStatePrefetchManager* no_state_prefetch_manager =
      prerender::NoStatePrefetchManagerFactory::GetForBrowserContext(profile);
  if (no_state_prefetch_manager) {
    value = no_state_prefetch_manager->CopyAsValue();
  } else {
    value = std::make_unique<base::DictionaryValue>();
    value->SetBoolean("enabled", false);
    value->SetBoolean("omnibox_enabled", false);
  }
  return value;
}

std::unique_ptr<base::ListValue> GetExtensionInfo(Profile* profile) {
  auto extension_list = std::make_unique<base::ListValue>();
#if BUILDFLAG(ENABLE_EXTENSIONS)
  extensions::ExtensionSystem* extension_system =
      extensions::ExtensionSystem::Get(profile);
  if (extension_system) {
    extensions::ExtensionService* extension_service =
        extension_system->extension_service();
    if (extension_service) {
      std::unique_ptr<const extensions::ExtensionSet> extensions(
          extensions::ExtensionRegistry::Get(profile)
              ->GenerateInstalledExtensionsSet());
      for (const auto& extension : *extensions) {
        base::Value::Dict extension_info;
        bool enabled = extension_service->IsExtensionEnabled(extension->id());
        extensions::GetExtensionBasicInfo(extension.get(), enabled,
                                          &extension_info);
        extension_list->Append(base::Value(std::move(extension_info)));
      }
    }
  }
#endif
  return extension_list;
}

#if BUILDFLAG(IS_WIN)
std::unique_ptr<base::DictionaryValue> GetWindowsServiceProviders() {
  auto service_providers = std::make_unique<base::DictionaryValue>();

  WinsockLayeredServiceProviderList layered_providers;
  GetWinsockLayeredServiceProviders(&layered_providers);
  base::Value::List layered_provider_list;
  for (size_t i = 0; i < layered_providers.size(); ++i) {
    base::Value::Dict service_dict;
    service_dict.Set("name", base::AsString16(layered_providers[i].name));
    service_dict.Set("version", layered_providers[i].version);
    service_dict.Set("chain_length", layered_providers[i].chain_length);
    service_dict.Set("socket_type", layered_providers[i].socket_type);
    service_dict.Set("socket_protocol", layered_providers[i].socket_protocol);
    service_dict.Set("path", base::WideToUTF8(layered_providers[i].path));

    layered_provider_list.Append(std::move(service_dict));
  }
  service_providers->GetDict().Set("service_providers",
                                   std::move(layered_provider_list));

  WinsockNamespaceProviderList namespace_providers;
  GetWinsockNamespaceProviders(&namespace_providers);
  base::Value::List namespace_list;
  for (size_t i = 0; i < namespace_providers.size(); ++i) {
    base::Value::Dict namespace_dict;
    namespace_dict.Set("name", base::AsString16(namespace_providers[i].name));
    namespace_dict.Set("active", namespace_providers[i].active);
    namespace_dict.Set("version", namespace_providers[i].version);
    namespace_dict.Set("type", namespace_providers[i].type);

    namespace_list.Append(std::move(namespace_dict));
  }
  service_providers->GetDict().Set("namespace_providers",
                                   std::move(namespace_list));

  return service_providers;
}
#endif

}  // namespace chrome_browser_net
