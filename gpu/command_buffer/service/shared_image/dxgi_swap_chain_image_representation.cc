// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/shared_image/dxgi_swap_chain_image_representation.h"
#include <memory>

#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/command_buffer/service/shared_image/dxgi_swap_chain_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_representation.h"
#include "gpu/command_buffer/service/skia_utils.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "third_party/skia/include/core/SkPromiseImageTexture.h"
#include "third_party/skia/include/gpu/GrContextThreadSafeProxy.h"
#include "ui/gl/gl_image.h"
#include "ui/gl/scoped_restore_texture.h"

namespace gpu {

DXGISwapChainOverlayImageRepresentation::
    DXGISwapChainOverlayImageRepresentation(SharedImageManager* manager,
                                            SharedImageBacking* backing,
                                            MemoryTypeTracker* tracker)
    : OverlayImageRepresentation(manager, backing, tracker) {}

DXGISwapChainOverlayImageRepresentation::
    ~DXGISwapChainOverlayImageRepresentation() = default;

OverlayImageRepresentation::DCompLayerContent
DXGISwapChainOverlayImageRepresentation::GetDCompLayerContent() const {
  return static_cast<DXGISwapChainImageBacking*>(backing())
      ->GetDCompLayerContent();
}

bool DXGISwapChainOverlayImageRepresentation::BeginReadAccess(
    gfx::GpuFenceHandle& acquire_fence) {
  // For the time being, let's use present interval 0.
  const bool should_synchronize_present_with_vblank = false;

  bool success = static_cast<DXGISwapChainImageBacking*>(backing())->Present(
      should_synchronize_present_with_vblank);

  return success;
}

void DXGISwapChainOverlayImageRepresentation::EndReadAccess(
    gfx::GpuFenceHandle release_fence) {}

gl::GLImage* DXGISwapChainOverlayImageRepresentation::GetGLImage() {
  NOTIMPLEMENTED();
  return nullptr;
}

GLTexturePassthroughDXGISwapChainBufferRepresentation::
    GLTexturePassthroughDXGISwapChainBufferRepresentation(
        SharedImageManager* manager,
        SharedImageBacking* backing,
        MemoryTypeTracker* tracker,
        scoped_refptr<gles2::TexturePassthrough> texture)
    : GLTexturePassthroughImageRepresentation(manager, backing, tracker),
      texture_(std::move(texture)) {}

const scoped_refptr<gles2::TexturePassthrough>&
GLTexturePassthroughDXGISwapChainBufferRepresentation::GetTexturePassthrough(
    int plane_index) {
  DCHECK_EQ(plane_index, 0);
  return texture_;
}

GLTexturePassthroughDXGISwapChainBufferRepresentation::
    ~GLTexturePassthroughDXGISwapChainBufferRepresentation() = default;

bool GLTexturePassthroughDXGISwapChainBufferRepresentation::BeginAccess(
    GLenum mode) {
  // Assume that BindTexImage has already been called for us.
  auto texture =
      GLTexturePassthroughImageRepresentation::GetTexturePassthrough();
  DCHECK(!texture->is_bind_pending());

  return true;
}

void GLTexturePassthroughDXGISwapChainBufferRepresentation::EndAccess() {}

// static
std::unique_ptr<SkiaGLImageRepresentationDXGISwapChain>
SkiaGLImageRepresentationDXGISwapChain::Create(
    std::unique_ptr<GLTextureImageRepresentationBase> gl_representation,
    scoped_refptr<SharedContextState> context_state,
    SharedImageManager* manager,
    SharedImageBacking* backing,
    MemoryTypeTracker* tracker) {
  GrBackendTexture backend_texture;
  if (!GetGrBackendTexture(
          context_state->feature_info(),
          gl_representation->GetTextureBase()->target(), backing->size(),
          gl_representation->GetTextureBase()->service_id(),
          (backing->format()).resource_format(),
          context_state->gr_context()->threadSafeProxy(), &backend_texture)) {
    return nullptr;
  }
  auto promise_texture = SkPromiseImageTexture::Make(backend_texture);
  if (!promise_texture)
    return nullptr;
  return base::WrapUnique(new SkiaGLImageRepresentationDXGISwapChain(
      std::move(gl_representation), std::move(promise_texture),
      std::move(context_state), manager, backing, tracker));
}

SkiaGLImageRepresentationDXGISwapChain::SkiaGLImageRepresentationDXGISwapChain(
    std::unique_ptr<GLTextureImageRepresentationBase> gl_representation,
    sk_sp<SkPromiseImageTexture> promise_texture,
    scoped_refptr<SharedContextState> context_state,
    SharedImageManager* manager,
    SharedImageBacking* backing,
    MemoryTypeTracker* tracker)
    : SkiaGLImageRepresentation(std::move(gl_representation),
                                std::move(promise_texture),
                                std::move(context_state),
                                manager,
                                backing,
                                tracker) {}

SkiaGLImageRepresentationDXGISwapChain::
    ~SkiaGLImageRepresentationDXGISwapChain() = default;

std::vector<sk_sp<SkSurface>>
SkiaGLImageRepresentationDXGISwapChain::BeginWriteAccess(
    int final_msaa_count,
    const SkSurfaceProps& surface_props,
    const gfx::Rect& update_rect,
    std::vector<GrBackendSemaphore>* begin_semaphores,
    std::vector<GrBackendSemaphore>* end_semaphores,
    std::unique_ptr<GrBackendSurfaceMutableState>* end_state) {
  std::vector<sk_sp<SkSurface>> surfaces =
      SkiaGLImageRepresentation::BeginWriteAccess(
          final_msaa_count, surface_props, update_rect, begin_semaphores,
          end_semaphores, end_state);

  if (!surfaces.empty()) {
    static_cast<DXGISwapChainImageBacking*>(backing())->set_swap_rect(
        update_rect);
  }

  return surfaces;
}

}  // namespace gpu