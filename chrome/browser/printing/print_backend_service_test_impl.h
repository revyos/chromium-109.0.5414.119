// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRINTING_PRINT_BACKEND_SERVICE_TEST_IMPL_H_
#define CHROME_BROWSER_PRINTING_PRINT_BACKEND_SERVICE_TEST_IMPL_H_

#include <memory>
#include <string>

#include "base/memory/scoped_refptr.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/services/printing/print_backend_service_impl.h"
#include "chrome/services/printing/public/mojom/print_backend_service.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "printing/backend/test_print_backend.h"

#if BUILDFLAG(IS_WIN)
#include "base/containers/queue.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "printing/mojom/print.mojom.h"
#endif

#if BUILDFLAG(IS_WIN)
namespace gfx {
class Rect;
class Size;
}  // namespace gfx
#endif

namespace printing {

#if BUILDFLAG(IS_WIN)
struct RenderPrintedPageData;
#endif

// `PrintBackendServiceTestImpl` uses a `TestPrintBackend` to enable testing
// of the `PrintBackendService` without relying upon the presence of real
// printer drivers.
class PrintBackendServiceTestImpl : public PrintBackendServiceImpl {
 public:
  // Launch the service in-process for testing using the provided backend.
  // `sandboxed` identifies if this service is potentially subject to
  // experiencing access-denied errors on some commands.
  static std::unique_ptr<PrintBackendServiceTestImpl> LaunchForTesting(
      mojo::Remote<mojom::PrintBackendService>& remote,
      scoped_refptr<TestPrintBackend> backend,
      bool sandboxed);

  PrintBackendServiceTestImpl(const PrintBackendServiceTestImpl&) = delete;
  PrintBackendServiceTestImpl& operator=(const PrintBackendServiceTestImpl&) =
      delete;
  ~PrintBackendServiceTestImpl() override;

  // Override which needs special handling for using `test_print_backend_`.
  void Init(const std::string& locale) override;

  // Overrides to support testing service termination scenarios.
  void EnumeratePrinters(
      mojom::PrintBackendService::EnumeratePrintersCallback callback) override;
  void GetDefaultPrinterName(
      mojom::PrintBackendService::GetDefaultPrinterNameCallback callback)
      override;
  void GetPrinterSemanticCapsAndDefaults(
      const std::string& printer_name,
      mojom::PrintBackendService::GetPrinterSemanticCapsAndDefaultsCallback
          callback) override;
  void FetchCapabilities(
      const std::string& printer_name,
      mojom::PrintBackendService::FetchCapabilitiesCallback callback) override;
  void UpdatePrintSettings(
      base::Value::Dict job_settings,
      mojom::PrintBackendService::UpdatePrintSettingsCallback callback)
      override;
#if BUILDFLAG(IS_WIN)
  void RenderPrintedPage(
      int32_t document_cookie,
      uint32_t page_index,
      mojom::MetafileDataType page_data_type,
      base::ReadOnlySharedMemoryRegion serialized_page,
      const gfx::Size& page_size,
      const gfx::Rect& page_content_rect,
      float shrink_factor,
      mojom::PrintBackendService::RenderPrintedPageCallback callback) override;
#endif  // BUILDFLAG(IS_WIN)

  // Cause the service to terminate on the next interaction it receives.  Once
  // terminated no further Mojo calls will be possible since there will not be
  // a receiver to handle them.
  void SetTerminateReceiverOnNextInteraction() { terminate_receiver_ = true; }

#if BUILDFLAG(IS_WIN)
  // Set the page number for which rendering should be delayed until.  Pages
  // are held in queue until this page number is seen, after which the pages
  // are released in sequence for rendering.
  void set_rendering_delayed_until_page(uint32_t page_number) {
    rendering_delayed_until_page_number_ = page_number;
  }
#endif

 private:
  // Use LaunchForTesting().
  PrintBackendServiceTestImpl(
      mojo::PendingReceiver<mojom::PrintBackendService> receiver,
      scoped_refptr<TestPrintBackend> backend);

  void OnDidGetDefaultPrinterName(
      mojom::PrintBackendService::GetDefaultPrinterNameCallback callback,
      mojom::DefaultPrinterNameResultPtr printer_name);

  void TerminateConnection();

  // When pretending to be sandboxed, have the possibility of getting access
  // denied errors.
  bool is_sandboxed_ = false;

  // Marker to signal service should terminate on next interaction.
  bool terminate_receiver_ = false;

#if BUILDFLAG(IS_WIN)
  // Marker to signal that rendering should be delayed until the page with this
  // index is reached.  This provides a mechanism for the print pipeline to get
  // multiple pages queued up.
  uint32_t rendering_delayed_until_page_number_ = 0;

  // The queue of pages whose rendering processing is being delayed.
  base::queue<std::unique_ptr<RenderPrintedPageData>> delayed_rendering_pages_;
#endif

  scoped_refptr<TestPrintBackend> test_print_backend_;
};

}  // namespace printing

#endif  // CHROME_BROWSER_PRINTING_PRINT_BACKEND_SERVICE_TEST_IMPL_H_
