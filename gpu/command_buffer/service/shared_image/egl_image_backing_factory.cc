// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/shared_image/egl_image_backing_factory.h"

#include <algorithm>

#include "components/viz/common/resources/resource_format_utils.h"
#include "components/viz/common/resources/resource_sizes.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/service_utils.h"
#include "gpu/command_buffer/service/shared_image/egl_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_factory.h"
#include "gpu/config/gpu_preferences.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_gl_api_implementation.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/shared_gl_fence_egl.h"

namespace gpu {

///////////////////////////////////////////////////////////////////////////////
// EGLImageBackingFactory

EGLImageBackingFactory::EGLImageBackingFactory(
    const GpuPreferences& gpu_preferences,
    const GpuDriverBugWorkarounds& workarounds,
    const gles2::FeatureInfo* feature_info)
    : GLCommonImageBackingFactory(gpu_preferences,
                                  workarounds,
                                  feature_info,
                                  /*progress_reporter=*/nullptr) {}

EGLImageBackingFactory::~EGLImageBackingFactory() = default;

std::unique_ptr<SharedImageBacking> EGLImageBackingFactory::CreateSharedImage(
    const Mailbox& mailbox,
    viz::SharedImageFormat format,
    SurfaceHandle surface_handle,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    GrSurfaceOrigin surface_origin,
    SkAlphaType alpha_type,
    uint32_t usage,
    bool is_thread_safe) {
  return MakeEglImageBacking(mailbox, format, size, color_space, surface_origin,
                             alpha_type, usage, base::span<const uint8_t>());
}

std::unique_ptr<SharedImageBacking> EGLImageBackingFactory::CreateSharedImage(
    const Mailbox& mailbox,
    viz::SharedImageFormat format,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    GrSurfaceOrigin surface_origin,
    SkAlphaType alpha_type,
    uint32_t usage,
    base::span<const uint8_t> pixel_data) {
  return MakeEglImageBacking(mailbox, format, size, color_space, surface_origin,
                             alpha_type, usage, pixel_data);
}

std::unique_ptr<SharedImageBacking> EGLImageBackingFactory::CreateSharedImage(
    const Mailbox& mailbox,
    int client_id,
    gfx::GpuMemoryBufferHandle handle,
    gfx::BufferFormat buffer_format,
    gfx::BufferPlane plane,
    SurfaceHandle surface_handle,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    GrSurfaceOrigin surface_origin,
    SkAlphaType alpha_type,
    uint32_t usage) {
  NOTIMPLEMENTED_LOG_ONCE();
  return nullptr;
}

bool EGLImageBackingFactory::IsSupported(uint32_t usage,
                                         viz::SharedImageFormat format,
                                         const gfx::Size& size,
                                         bool thread_safe,
                                         gfx::GpuMemoryBufferType gmb_type,
                                         GrContextType gr_context_type,
                                         base::span<const uint8_t> pixel_data) {
  if (!pixel_data.empty() && gr_context_type != GrContextType::kGL) {
    return false;
  }

  // Doesn't support gmb for now
  if (gmb_type != gfx::EMPTY_BUFFER) {
    return false;
  }

  // Doesn't support contexts other than GL for OOPR Canvas
  if (gr_context_type != GrContextType::kGL &&
      ((usage & SHARED_IMAGE_USAGE_DISPLAY_READ) ||
       (usage & SHARED_IMAGE_USAGE_DISPLAY_WRITE) ||
       (usage & SHARED_IMAGE_USAGE_RASTER))) {
    return false;
  }
  constexpr uint32_t kInvalidUsage =
      SHARED_IMAGE_USAGE_WEBGPU | SHARED_IMAGE_USAGE_VIDEO_DECODE |
      SHARED_IMAGE_USAGE_SCANOUT | SHARED_IMAGE_USAGE_CPU_UPLOAD;
  if (usage & kInvalidUsage) {
    return false;
  }

  return CanCreateSharedImage(size, pixel_data, GetFormatInfo(format),
                              GL_TEXTURE_2D);
}

std::unique_ptr<SharedImageBacking> EGLImageBackingFactory::MakeEglImageBacking(
    const Mailbox& mailbox,
    viz::SharedImageFormat format,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    GrSurfaceOrigin surface_origin,
    SkAlphaType alpha_type,
    uint32_t usage,
    base::span<const uint8_t> pixel_data) {
  DCHECK(!(usage & SHARED_IMAGE_USAGE_SCANOUT));

  // Calculate SharedImage size in bytes.
  size_t estimated_size;
  if (!viz::ResourceSizes::MaybeSizeInBytes(size, format, &estimated_size)) {
    DLOG(ERROR) << "MakeEglImageBacking: Failed to calculate SharedImage size";
    return nullptr;
  }

  return std::make_unique<EGLImageBacking>(
      mailbox, format, size, color_space, surface_origin, alpha_type, usage,
      estimated_size, GetFormatInfo(format), workarounds_, use_passthrough_,
      pixel_data);
}

}  // namespace gpu
