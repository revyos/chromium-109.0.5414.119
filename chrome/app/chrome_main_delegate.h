// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_APP_CHROME_MAIN_DELEGATE_H_
#define CHROME_APP_CHROME_MAIN_DELEGATE_H_

#include <memory>
#include <vector>

#include "base/time/time.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/startup_data.h"
#include "chrome/common/chrome_content_client.h"
#include "content/public/app/content_main_delegate.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
class CommandLine;
}

namespace chromeos {
class LacrosService;
}

namespace heap_profiling {
class HeapProfilerController;
}

namespace tracing {
class TracingSamplerProfiler;
}

class ChromeContentBrowserClient;
class ChromeContentUtilityClient;

// Chrome implementation of ContentMainDelegate.
class ChromeMainDelegate : public content::ContentMainDelegate {
 public:
  static const char* const kNonWildcardDomainNonPortSchemes[];
  static const size_t kNonWildcardDomainNonPortSchemesSize;

  ChromeMainDelegate();

  // |exe_entry_point_ticks| is the time at which the main function of the
  // executable was entered, or null if not available.
  explicit ChromeMainDelegate(base::TimeTicks exe_entry_point_ticks);

  ChromeMainDelegate(const ChromeMainDelegate&) = delete;
  ChromeMainDelegate& operator=(const ChromeMainDelegate&) = delete;

  ~ChromeMainDelegate() override;

 protected:
  // content::ContentMainDelegate:
  absl::optional<int> BasicStartupComplete() override;
  void PreSandboxStartup() override;
  void SandboxInitialized(const std::string& process_type) override;
  absl::variant<int, content::MainFunctionParams> RunProcess(
      const std::string& process_type,
      content::MainFunctionParams main_function_params) override;
  void ProcessExiting(const std::string& process_type) override;
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  void ZygoteStarting(std::vector<std::unique_ptr<content::ZygoteForkDelegate>>*
                          delegates) override;
  void ZygoteForked() override;
#endif
  absl::optional<int> PreBrowserMain() override;
  absl::optional<int> PostEarlyInitialization(InvokedIn invoked_in) override;
  bool ShouldCreateFeatureList(InvokedIn invoked_in) override;
  bool ShouldInitializeMojo(InvokedIn invoked_in) override;
#if BUILDFLAG(IS_WIN)
  bool ShouldHandleConsoleControlEvents() override;
#endif

  content::ContentClient* CreateContentClient() override;
  content::ContentBrowserClient* CreateContentBrowserClient() override;
  content::ContentGpuClient* CreateContentGpuClient() override;
  content::ContentRendererClient* CreateContentRendererClient() override;
  content::ContentUtilityClient* CreateContentUtilityClient() override;

  // Initialization that happens in all process types.
  void CommonEarlyInitialization();

#if BUILDFLAG(IS_MAC)
  void InitMacCrashReporter(const base::CommandLine& command_line,
                            const std::string& process_type);
  void SetUpInstallerPreferences(const base::CommandLine& command_line);
#endif  // BUILDFLAG(IS_MAC)

  ChromeContentClient chrome_content_client_;

  std::unique_ptr<ChromeContentBrowserClient> chrome_content_browser_client_;
  std::unique_ptr<ChromeContentUtilityClient> chrome_content_utility_client_;

  std::unique_ptr<tracing::TracingSamplerProfiler> tracing_sampler_profiler_;

  // The controller schedules UMA heap profiles collections and forwarding down
  // the reporting pipeline.
  std::unique_ptr<heap_profiling::HeapProfilerController>
      heap_profiler_controller_;

#if BUILDFLAG(IS_CHROMEOS_LACROS)
  std::unique_ptr<chromeos::LacrosService> lacros_service_;
#endif
};

#endif  // CHROME_APP_CHROME_MAIN_DELEGATE_H_
