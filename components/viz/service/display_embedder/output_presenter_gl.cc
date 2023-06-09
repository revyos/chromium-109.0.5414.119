// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display_embedder/output_presenter_gl.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/notreached.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "components/viz/common/features.h"
#include "components/viz/common/resources/resource_format_utils.h"
#include "components/viz/service/display_embedder/skia_output_surface_dependency.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/ipc/common/gpu_surface_lookup.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/gfx/overlay_plane_data.h"
#include "ui/gfx/overlay_transform.h"
#include "ui/gl/gl_fence.h"
#include "ui/gl/gl_surface.h"

#if BUILDFLAG(IS_ANDROID)
#include "ui/gl/gl_surface_egl_surface_control.h"
#endif

#if BUILDFLAG(IS_OZONE)
#include "ui/base/ui_base_features.h"
#endif

namespace viz {

namespace {

// Helper function for moving a GpuFence from a fence handle to a unique_ptr.
std::unique_ptr<gfx::GpuFence> TakeGpuFence(gfx::GpuFenceHandle fence) {
  return fence.is_null() ? nullptr
                         : std::make_unique<gfx::GpuFence>(std::move(fence));
}

class PresenterImageGL : public OutputPresenter::Image {
 public:
  PresenterImageGL(
      gpu::SharedImageFactory* factory,
      gpu::SharedImageRepresentationFactory* representation_factory,
      SkiaOutputSurfaceDependency* deps)
      : Image(factory, representation_factory, deps) {}
  ~PresenterImageGL() override = default;

  void BeginPresent() final;
  void EndPresent(gfx::GpuFenceHandle release_fence) final;
  int GetPresentCount() const final;
  void OnContextLost() final;

  gl::OverlayImage GetOverlayImage(std::unique_ptr<gfx::GpuFence>* fence);

  const gfx::ColorSpace& color_space() {
    DCHECK(overlay_representation_);
    return overlay_representation_->color_space();
  }
};

void PresenterImageGL::BeginPresent() {
  if (++present_count_ != 1) {
    DCHECK(scoped_overlay_read_access_);
    return;
  }

  DCHECK(!sk_surface());
  DCHECK(!scoped_overlay_read_access_);

  scoped_overlay_read_access_ =
      overlay_representation_->BeginScopedReadAccess();
  DCHECK(scoped_overlay_read_access_);
}

void PresenterImageGL::EndPresent(gfx::GpuFenceHandle release_fence) {
  DCHECK(present_count_);
  if (--present_count_)
    return;

  scoped_overlay_read_access_->SetReleaseFence(std::move(release_fence));

  scoped_overlay_read_access_.reset();
}

int PresenterImageGL::GetPresentCount() const {
  return present_count_;
}

void PresenterImageGL::OnContextLost() {
  if (overlay_representation_)
    overlay_representation_->OnContextLost();
}

gl::OverlayImage PresenterImageGL::GetOverlayImage(
    std::unique_ptr<gfx::GpuFence>* fence) {
  DCHECK(scoped_overlay_read_access_);
  if (fence) {
    *fence = TakeGpuFence(scoped_overlay_read_access_->TakeAcquireFence());
  }
#if BUILDFLAG(IS_OZONE)
  return scoped_overlay_read_access_->GetNativePixmap();
#elif BUILDFLAG(IS_MAC)
  return scoped_overlay_read_access_->GetIOSurface();
#elif BUILDFLAG(IS_ANDROID)
  return scoped_overlay_read_access_->GetAHardwareBufferFenceSync();
#else
  LOG(FATAL) << "GetOverlayImage() is not implemented on this platform".
#endif
}

}  // namespace

// static
const uint32_t OutputPresenterGL::kDefaultSharedImageUsage =
    gpu::SHARED_IMAGE_USAGE_SCANOUT | gpu::SHARED_IMAGE_USAGE_DISPLAY_READ |
    gpu::SHARED_IMAGE_USAGE_DISPLAY_WRITE |
    gpu::SHARED_IMAGE_USAGE_GLES2_FRAMEBUFFER_HINT;

// static
std::unique_ptr<OutputPresenterGL> OutputPresenterGL::Create(
    SkiaOutputSurfaceDependency* deps,
    gpu::SharedImageFactory* factory,
    gpu::SharedImageRepresentationFactory* representation_factory) {
#if BUILDFLAG(IS_ANDROID)
  if (deps->GetGpuFeatureInfo()
          .status_values[gpu::GPU_FEATURE_TYPE_ANDROID_SURFACE_CONTROL] !=
      gpu::kGpuFeatureStatusEnabled) {
    return nullptr;
  }

  bool can_be_used_with_surface_control = false;
  ANativeWindow* window =
      gpu::GpuSurfaceLookup::GetInstance()->AcquireNativeWidget(
          deps->GetSurfaceHandle(), &can_be_used_with_surface_control);
  base::ScopedClosureRunner release_runner(base::BindOnce(
      [](gfx::AcceleratedWidget widget) {
        if (widget)
          ANativeWindow_release(widget);
      },
      window));
  if (!window || !can_be_used_with_surface_control)
    return nullptr;
  // TODO(https://crbug.com/1012401): don't depend on GL.
  auto gl_surface = base::MakeRefCounted<gl::GLSurfaceEGLSurfaceControl>(
      deps->GetSharedContextState()->display()->GetAs<gl::GLDisplayEGL>(),
      window, base::ThreadTaskRunnerHandle::Get());
  if (!gl_surface->Initialize(gl::GLSurfaceFormat())) {
    LOG(ERROR) << "Failed to initialize GLSurfaceEGLSurfaceControl.";
    return nullptr;
  }

  if (!deps->GetSharedContextState()->MakeCurrent(gl_surface.get(),
                                                  true /* needs_gl*/)) {
    LOG(ERROR) << "MakeCurrent failed.";
    return nullptr;
  }

  return std::make_unique<OutputPresenterGL>(std::move(gl_surface), deps,
                                             factory, representation_factory,
                                             kDefaultSharedImageUsage);
#else
  return nullptr;
#endif
}

OutputPresenterGL::OutputPresenterGL(
    scoped_refptr<gl::GLSurface> gl_surface,
    SkiaOutputSurfaceDependency* deps,
    gpu::SharedImageFactory* factory,
    gpu::SharedImageRepresentationFactory* representation_factory,
    uint32_t shared_image_usage)
    : gl_surface_(gl_surface),
      dependency_(deps),
      supports_async_swap_(gl_surface_->SupportsAsyncSwap()),
      shared_image_factory_(factory),
      shared_image_representation_factory_(representation_factory),
      shared_image_usage_(shared_image_usage) {
  // GL is origin is at bottom left normally, all Surfaceless implementations
  // are flipped.
  DCHECK_EQ(gl_surface_->GetOrigin(), gfx::SurfaceOrigin::kTopLeft);
}

OutputPresenterGL::~OutputPresenterGL() = default;

void OutputPresenterGL::InitializeCapabilities(
    OutputSurface::Capabilities* capabilities) {
  capabilities->android_surface_control_feature_enabled = true;
  capabilities->supports_post_sub_buffer = gl_surface_->SupportsPostSubBuffer();
  capabilities->supports_commit_overlay_planes =
      gl_surface_->SupportsCommitOverlayPlanes();
  capabilities->supports_viewporter = gl_surface_->SupportsViewporter();

  // Set supports_surfaceless to enable overlays.
  capabilities->supports_surfaceless = true;
  // We expect origin of buffers is at top left.
  capabilities->output_surface_origin = gfx::SurfaceOrigin::kTopLeft;
  // Set resize_based_on_root_surface to omit platform proposed size.
  capabilities->resize_based_on_root_surface =
      gl_surface_->SupportsOverridePlatformSize();
#if BUILDFLAG(IS_ANDROID)
  capabilities->supports_dynamic_frame_buffer_allocation = true;
#endif

  // TODO(https://crbug.com/1108406): only add supported formats base on
  // platform, driver, etc.
  capabilities->sk_color_types[static_cast<int>(gfx::BufferFormat::BGR_565)] =
      kRGB_565_SkColorType;
  capabilities->sk_color_types[static_cast<int>(gfx::BufferFormat::RGBA_4444)] =
      kARGB_4444_SkColorType;
  capabilities->sk_color_types[static_cast<int>(gfx::BufferFormat::RGBX_8888)] =
      kRGB_888x_SkColorType;
  capabilities->sk_color_types[static_cast<int>(gfx::BufferFormat::RGBA_8888)] =
      kRGBA_8888_SkColorType;
  capabilities->sk_color_types[static_cast<int>(gfx::BufferFormat::BGRX_8888)] =
      kBGRA_8888_SkColorType;
  capabilities->sk_color_types[static_cast<int>(gfx::BufferFormat::BGRA_8888)] =
      kBGRA_8888_SkColorType;
  capabilities
      ->sk_color_types[static_cast<int>(gfx::BufferFormat::BGRA_1010102)] =
      kBGRA_1010102_SkColorType;
  capabilities
      ->sk_color_types[static_cast<int>(gfx::BufferFormat::RGBA_1010102)] =
      kRGBA_1010102_SkColorType;
  capabilities->sk_color_types[static_cast<int>(gfx::BufferFormat::RGBA_F16)] =
      kRGBA_F16_SkColorType;
}

bool OutputPresenterGL::Reshape(
    const SkSurfaceCharacterization& characterization,
    const gfx::ColorSpace& color_space,
    float device_scale_factor,
    gfx::OverlayTransform transform) {
  const gfx::Size size = gfx::SkISizeToSize(characterization.dimensions());
  image_format_ = SkColorTypeToResourceFormat(characterization.colorType());
  const bool has_alpha =
      !SkAlphaTypeIsOpaque(characterization.imageInfo().alphaType());
  return gl_surface_->Resize(size, device_scale_factor, color_space, has_alpha);
}

std::vector<std::unique_ptr<OutputPresenter::Image>>
OutputPresenterGL::AllocateImages(gfx::ColorSpace color_space,
                                  gfx::Size image_size,
                                  size_t num_images) {
  std::vector<std::unique_ptr<Image>> images;
  for (size_t i = 0; i < num_images; ++i) {
    auto image = std::make_unique<PresenterImageGL>(
        shared_image_factory_, shared_image_representation_factory_,
        dependency_);
    if (!image->Initialize(image_size, color_space,
                           SharedImageFormat::SinglePlane(image_format_),
                           shared_image_usage_)) {
      DLOG(ERROR) << "Failed to initialize image.";
      return {};
    }
    images.push_back(std::move(image));
  }

  return images;
}

std::unique_ptr<OutputPresenter::Image> OutputPresenterGL::AllocateSingleImage(
    gfx::ColorSpace color_space,
    gfx::Size image_size) {
  auto image = std::make_unique<PresenterImageGL>(
      shared_image_factory_, shared_image_representation_factory_, dependency_);
  if (!image->Initialize(image_size, color_space,
                         SharedImageFormat::SinglePlane(image_format_),
                         shared_image_usage_)) {
    DLOG(ERROR) << "Failed to initialize image.";
    return nullptr;
  }
  return image;
}

void OutputPresenterGL::SwapBuffers(
    SwapCompletionCallback completion_callback,
    BufferPresentedCallback presentation_callback,
    gl::FrameData data) {
  if (supports_async_swap_) {
    gl_surface_->SwapBuffersAsync(std::move(completion_callback),
                                  std::move(presentation_callback),
                                  std::move(data));
  } else {
    auto result = gl_surface_->SwapBuffers(std::move(presentation_callback),
                                           std::move(data));
    std::move(completion_callback).Run(gfx::SwapCompletionResult(result));
  }
}

void OutputPresenterGL::PostSubBuffer(
    const gfx::Rect& rect,
    SwapCompletionCallback completion_callback,
    BufferPresentedCallback presentation_callback,
    gl::FrameData data) {
#if BUILDFLAG(IS_MAC)
  gl_surface_->SetCALayerErrorCode(ca_layer_error_code_);
#endif

  if (supports_async_swap_) {
    gl_surface_->PostSubBufferAsync(
        rect.x(), rect.y(), rect.width(), rect.height(),
        std::move(completion_callback), std::move(presentation_callback),
        std::move(data));
  } else {
    auto result = gl_surface_->PostSubBuffer(
        rect.x(), rect.y(), rect.width(), rect.height(),
        std::move(presentation_callback), std::move(data));
    std::move(completion_callback).Run(gfx::SwapCompletionResult(result));
  }
}

void OutputPresenterGL::SchedulePrimaryPlane(
    const OverlayProcessorInterface::OutputSurfaceOverlayPlane& plane,
    Image* image,
    bool is_submitted) {
  std::unique_ptr<gfx::GpuFence> fence;
  auto* presenter_image = static_cast<PresenterImageGL*>(image);
  // If the submitted_image() is being scheduled, we don't new a new fence.
  gl::OverlayImage overlay_image = presenter_image->GetOverlayImage(
      (is_submitted || !gl_surface_->SupportsPlaneGpuFences()) ? nullptr
                                                               : &fence);

  // Output surface is also z-order 0.
  constexpr int kPlaneZOrder = 0;
  // TODO(edcourtney): We pass a full damage rect - actual damage is passed via
  // PostSubBuffer. As part of unifying the handling of the primary plane and
  // overlays, damage should be added to OutputSurfaceOverlayPlane and passed in
  // here.
  gl_surface_->ScheduleOverlayPlane(
      std::move(overlay_image), std::move(fence),
      gfx::OverlayPlaneData(
          kPlaneZOrder, plane.transform, plane.display_rect, plane.uv_rect,
          plane.enable_blending,
          plane.damage_rect.value_or(gfx::Rect(plane.resource_size)),
          plane.opacity, plane.priority_hint, plane.rounded_corners,
          presenter_image->color_space(),
          /*hdr_metadata=*/absl::nullopt));
}

void OutputPresenterGL::CommitOverlayPlanes(
    SwapCompletionCallback completion_callback,
    BufferPresentedCallback presentation_callback,
    gl::FrameData data) {
  if (supports_async_swap_) {
    gl_surface_->CommitOverlayPlanesAsync(std::move(completion_callback),
                                          std::move(presentation_callback),
                                          std::move(data));
  } else {
    auto result = gl_surface_->CommitOverlayPlanes(
        std::move(presentation_callback), std::move(data));
    std::move(completion_callback).Run(gfx::SwapCompletionResult(result));
  }
}

void OutputPresenterGL::ScheduleOverlayPlane(
    const OutputPresenter::OverlayPlaneCandidate& overlay_plane_candidate,
    ScopedOverlayAccess* access,
    std::unique_ptr<gfx::GpuFence> acquire_fence) {
  // Note that |overlay_plane_candidate| has different types on different
  // platforms. On Android and Ozone it is an OverlayCandidate, on Windows it is
  // a DCLayerOverlay, and on macOS it is a CALayeroverlay.
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_OZONE)
#if BUILDFLAG(IS_OZONE)
  // TODO(crbug.com/1366808): Add ScopedOverlayAccess::GetOverlayImage() that
  // works on all platforms.
  gl::OverlayImage overlay_image = access ? access->GetNativePixmap() : nullptr;
#elif BUILDFLAG(IS_ANDROID)
  gl::OverlayImage overlay_image =
      access ? access->GetAHardwareBufferFenceSync() : nullptr;
#endif
  // TODO(msisov): Once shared image factory allows creating a non backed
  // images and ScheduleOverlayPlane does not rely on GLImage, remove the if
  // condition that checks if this is a solid color overlay plane.
  //
  // Solid color overlays can be non-backed and are delegated for processing
  // to underlying backend. The only backend that uses them is Wayland - it
  // may have a protocol that asks Wayland compositor to create a solid color
  // buffer for a client. OverlayProcessorDelegated decides if a solid color
  // overlay is an overlay candidate and should be scheduled.
  if (overlay_image || overlay_plane_candidate.is_solid_color) {
#if DCHECK_IS_ON()
    if (overlay_plane_candidate.is_solid_color) {
      LOG_IF(FATAL, !overlay_plane_candidate.color.has_value())
          << "Solid color quads must have color set.";
    }

    if (acquire_fence && !acquire_fence->GetGpuFenceHandle().is_null()) {
      CHECK(access);
      CHECK_EQ(gpu::GrContextType::kGL, dependency_->gr_context_type());
      CHECK(features::IsDelegatedCompositingEnabled());
      CHECK(access->representation()->usage() &
            gpu::SHARED_IMAGE_USAGE_RASTER_DELEGATED_COMPOSITING);
    }
#endif

    // Access fence takes priority over composite fence iff it exists.
    if (access) {
      auto access_fence = TakeGpuFence(access->TakeAcquireFence());
      if (access_fence) {
        DCHECK(!acquire_fence);
        acquire_fence = std::move(access_fence);
      }
    }

    gl_surface_->ScheduleOverlayPlane(
        std::move(overlay_image), std::move(acquire_fence),
        gfx::OverlayPlaneData(
            overlay_plane_candidate.plane_z_order,
            absl::get<gfx::OverlayTransform>(overlay_plane_candidate.transform),
            overlay_plane_candidate.display_rect,
            overlay_plane_candidate.uv_rect, !overlay_plane_candidate.is_opaque,
            ToEnclosingRect(overlay_plane_candidate.damage_rect),
            overlay_plane_candidate.opacity,
            overlay_plane_candidate.priority_hint,
            overlay_plane_candidate.rounded_corners,
            overlay_plane_candidate.color_space,
            overlay_plane_candidate.hdr_metadata, overlay_plane_candidate.color,
            overlay_plane_candidate.is_solid_color,
            overlay_plane_candidate.clip_rect));
  }
#elif BUILDFLAG(IS_APPLE)
  gl_surface_->ScheduleCALayer(ui::CARendererLayerParams(
      overlay_plane_candidate.shared_state->is_clipped,
      gfx::ToEnclosingRect(overlay_plane_candidate.shared_state->clip_rect),
      overlay_plane_candidate.shared_state->rounded_corner_bounds,
      overlay_plane_candidate.shared_state->sorting_context_id,
      gfx::Transform(overlay_plane_candidate.shared_state->transform),
      access ? access->GetIOSurface() : gfx::ScopedIOSurface(),
      access ? access->representation()->color_space() : gfx::ColorSpace(),
      overlay_plane_candidate.contents_rect,
      gfx::ToEnclosingRect(overlay_plane_candidate.bounds_rect),
      overlay_plane_candidate.background_color,
      overlay_plane_candidate.edge_aa_mask, overlay_plane_candidate.opacity,
      overlay_plane_candidate.filter, overlay_plane_candidate.hdr_mode,
      overlay_plane_candidate.hdr_metadata,
      overlay_plane_candidate.protected_video_type));
#endif
}

#if BUILDFLAG(IS_MAC)
void OutputPresenterGL::SetCALayerErrorCode(
    gfx::CALayerResult ca_layer_error_code) {
  ca_layer_error_code_ = ca_layer_error_code;
}
#endif

}  // namespace viz
