// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_DISPLAY_OUTPUT_SURFACE_CLIENT_H_
#define COMPONENTS_VIZ_SERVICE_DISPLAY_OUTPUT_SURFACE_CLIENT_H_

#include <vector>

#include "base/callback.h"
#include "components/viz/common/gpu/context_provider.h"
#include "components/viz/common/resources/returned_resource.h"
#include "components/viz/service/viz_service_export.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/ipc/common/surface_handle.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/gpu_fence_handle.h"
#include "ui/latency/latency_info.h"

namespace gfx {
struct CALayerParams;
struct PresentationFeedback;
struct SwapTimings;
}  // namespace gfx

namespace viz {

class VIZ_SERVICE_EXPORT OutputSurfaceClient {
 public:
  // A notification that the swap of the backbuffer to the hardware is complete
  // and is now visible to the user, along with timing information on when the
  // swapping of the backbuffer started and completed.
  virtual void DidReceiveSwapBuffersAck(const gfx::SwapTimings& timings,
                                        gfx::GpuFenceHandle release_fence) = 0;

  // For surfaceless/ozone implementations to create damage for the next frame.
  virtual void SetNeedsRedrawRect(const gfx::Rect& damage_rect) = 0;

  // For displaying a swapped frame's contents on macOS.
  virtual void DidReceiveCALayerParams(
      const gfx::CALayerParams& ca_layer_params) = 0;

  // For sending swap sizes back to the browser process. Currently only used on
  // Android.
  virtual void DidSwapWithSize(const gfx::Size& pixel_size) = 0;

  // See |gfx::PresentationFeedback| for detail.
  virtual void DidReceivePresentationFeedback(
      const gfx::PresentationFeedback& feedback) = 0;

  // For synchronizing IOSurface use with the macOS WindowServer with
  // SkiaRenderer.
  virtual void DidReceiveReleasedOverlays(
      const std::vector<gpu::Mailbox>& released_overlays) = 0;

  // Sends the created child window to the browser process so that it can be
  // parented to the browser process window.
  virtual void AddChildWindowToBrowser(gpu::SurfaceHandle child_window) = 0;

 protected:
  virtual ~OutputSurfaceClient() {}
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_DISPLAY_OUTPUT_SURFACE_CLIENT_H_
