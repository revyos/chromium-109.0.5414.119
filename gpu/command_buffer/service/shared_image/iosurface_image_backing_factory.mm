// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/shared_image/iosurface_image_backing_factory.h"

#include "base/mac/scoped_cftyperef.h"
#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "components/viz/common/gpu/metal_context_provider.h"
#include "components/viz/common/resources/resource_format_utils.h"
#include "components/viz/common/resources/resource_sizes.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/command_buffer/service/shared_image/iosurface_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_backing.h"
#include "gpu/command_buffer/service/shared_image/shared_image_representation.h"
#include "gpu/command_buffer/service/skia_utils.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/skia/include/core/SkPromiseImageTexture.h"
#include "ui/gfx/mac/io_surface.h"
#include "ui/gl/buildflags.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_image_io_surface.h"
#include "ui/gl/gl_implementation.h"

#import <Metal/Metal.h>

// Usage of BUILDFLAG(USE_DAWN) needs to be after the include for
// ui/gl/buildflags.h
#if BUILDFLAG(USE_DAWN)
#include <dawn/native/MetalBackend.h>
#endif  // BUILDFLAG(USE_DAWN)

namespace gpu {

namespace {

base::scoped_nsprotocol<id<MTLTexture>> CreateMetalTexture(
    id<MTLDevice> mtl_device,
    IOSurfaceRef io_surface,
    const gfx::Size& size,
    viz::SharedImageFormat format) {
  TRACE_EVENT0("gpu", "IOSurfaceImageBackingFactory::CreateMetalTexture");
  base::scoped_nsprotocol<id<MTLTexture>> mtl_texture;
  MTLPixelFormat mtl_pixel_format =
      static_cast<MTLPixelFormat>(viz::ToMTLPixelFormat(format));
  if (mtl_pixel_format == MTLPixelFormatInvalid)
    return mtl_texture;

  base::scoped_nsobject<MTLTextureDescriptor> mtl_tex_desc(
      [MTLTextureDescriptor new]);
  [mtl_tex_desc setTextureType:MTLTextureType2D];
  [mtl_tex_desc
      setUsage:MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget];
  [mtl_tex_desc setPixelFormat:mtl_pixel_format];
  [mtl_tex_desc setWidth:size.width()];
  [mtl_tex_desc setHeight:size.height()];
  [mtl_tex_desc setDepth:1];
  [mtl_tex_desc setMipmapLevelCount:1];
  [mtl_tex_desc setArrayLength:1];
  [mtl_tex_desc setSampleCount:1];
  // TODO(https://crbug.com/952063): For zero-copy resources that are populated
  // on the CPU (e.g, video frames), it may be that MTLStorageModeManaged will
  // be more appropriate.
  [mtl_tex_desc setStorageMode:MTLStorageModePrivate];
  mtl_texture.reset([mtl_device newTextureWithDescriptor:mtl_tex_desc
                                               iosurface:io_surface
                                                   plane:0]);
  DCHECK(mtl_texture);
  return mtl_texture;
}

base::ScopedCFTypeRef<IOSurfaceRef> GetIOSurfaceFromImage(
    scoped_refptr<gl::GLImage> image) {
  base::ScopedCFTypeRef<IOSurfaceRef> result;
  if (image->GetType() == gl::GLImage::Type::IOSURFACE)
    result = static_cast<gl::GLImageIOSurface*>(image.get())->io_surface();
  return result;
}

}  // anonymous namespace

// Representation of a SharedImageBackingIOSurface as a Dawn Texture.
#if BUILDFLAG(USE_DAWN)
class DawnIOSurfaceRepresentation : public DawnImageRepresentation {
 public:
  DawnIOSurfaceRepresentation(SharedImageManager* manager,
                              SharedImageBacking* backing,
                              MemoryTypeTracker* tracker,
                              WGPUDevice device,
                              base::ScopedCFTypeRef<IOSurfaceRef> io_surface,
                              WGPUTextureFormat wgpu_format)
      : DawnImageRepresentation(manager, backing, tracker),
        io_surface_(std::move(io_surface)),
        device_(device),
        wgpu_format_(wgpu_format),
        dawn_procs_(dawn::native::GetProcs()) {
    DCHECK(device_);
    DCHECK(io_surface_);

    // Keep a reference to the device so that it stays valid (it might become
    // lost in which case operations will be noops).
    dawn_procs_.deviceReference(device_);
  }

  ~DawnIOSurfaceRepresentation() override {
    EndAccess();
    dawn_procs_.deviceRelease(device_);
  }

  WGPUTexture BeginAccess(WGPUTextureUsage usage) final {
    WGPUTextureDescriptor texture_descriptor = {};
    texture_descriptor.format = wgpu_format_;
    texture_descriptor.usage = usage;
    texture_descriptor.dimension = WGPUTextureDimension_2D;
    texture_descriptor.size = {static_cast<uint32_t>(size().width()),
                               static_cast<uint32_t>(size().height()), 1};
    texture_descriptor.mipLevelCount = 1;
    texture_descriptor.sampleCount = 1;

    // We need to have internal usages of CopySrc for copies. If texture is not
    // for video frame import, which has bi-planar format, we also need
    // RenderAttachment usage for clears, and TextureBinding for
    // copyTextureForBrowser.
    WGPUDawnTextureInternalUsageDescriptor internalDesc = {};
    internalDesc.chain.sType = WGPUSType_DawnTextureInternalUsageDescriptor;
    internalDesc.internalUsage =
        WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    if (wgpu_format_ != WGPUTextureFormat_R8BG8Biplanar420Unorm) {
      internalDesc.internalUsage |= WGPUTextureUsage_RenderAttachment;
    }

    texture_descriptor.nextInChain =
        reinterpret_cast<WGPUChainedStruct*>(&internalDesc);

    dawn::native::metal::ExternalImageDescriptorIOSurface descriptor;
    descriptor.cTextureDescriptor = &texture_descriptor;
    descriptor.isInitialized = IsCleared();
    descriptor.ioSurface = io_surface_.get();
    descriptor.plane = 0;

    // If the backing is compatible - essentially, a GLImageIOSurface -
    // then synchronize with all of the MTLSharedEvents which have been
    // stored in it as a consequence of earlier BeginAccess/EndAccess calls
    // against other representations.
    if (gl::GetANGLEImplementation() == gl::ANGLEImplementation::kMetal) {
      if (@available(macOS 10.14, *)) {
        SharedImageBacking* backing = this->backing();
        // Not possible to reach this with any other type of backing.
        DCHECK_EQ(backing->GetType(), SharedImageBackingType::kIOSurface);
        IOSurfaceImageBacking* iosurface_backing =
            static_cast<IOSurfaceImageBacking*>(backing);
        std::vector<std::unique_ptr<SharedEventAndSignalValue>> signals =
            iosurface_backing->TakeSharedEvents();
        for (const auto& signal : signals) {
          dawn::native::metal::ExternalImageMTLSharedEventDescriptor
              external_desc;
          external_desc.sharedEvent =
              static_cast<id<MTLSharedEvent>>(signal->shared_event());
          external_desc.signaledValue = signal->signaled_value();
          descriptor.waitEvents.push_back(external_desc);
        }
      }
    }

    texture_ = dawn::native::metal::WrapIOSurface(device_, &descriptor);
    return texture_;
  }

  void EndAccess() final {
    if (!texture_) {
      return;
    }

    dawn::native::metal::ExternalImageIOSurfaceEndAccessDescriptor descriptor;
    dawn::native::metal::IOSurfaceEndAccess(texture_, &descriptor);

    if (descriptor.isInitialized) {
      SetCleared();
    }

    if (gl::GetANGLEImplementation() == gl::ANGLEImplementation::kMetal) {
      if (@available(macOS 10.14, *)) {
        SharedImageBacking* backing = this->backing();
        // Not possible to reach this with any other type of backing.
        DCHECK_EQ(backing->GetType(), SharedImageBackingType::kIOSurface);
        IOSurfaceImageBacking* iosurface_backing =
            static_cast<IOSurfaceImageBacking*>(backing);
        // Dawn's Metal backend has enqueued a MTLSharedEvent which
        // consumers of the IOSurface must wait upon before attempting to
        // use that IOSurface on another MTLDevice. Store this event in
        // the underlying SharedImageBacking.
        iosurface_backing->AddSharedEventAndSignalValue(
            descriptor.sharedEvent, descriptor.signaledValue);
      }
    }

    // All further operations on the textures are errors (they would be racy
    // with other backings).
    dawn_procs_.textureDestroy(texture_);

    // TODO(b/252731382): the following WaitForCommandsToBeScheduled call should
    // no longer be necessary, but for some reason it is. Removing it
    // reintroduces intermittent renders of black frames to the WebGPU canvas.
    // This points to another synchronization bug not resolved by the use of
    // MTLSharedEvent between Dawn and ANGLE's Metal backend.
    //
    // macOS has a global GPU command queue so synchronization between APIs and
    // devices is automatic. However on Metal, wgpuQueueSubmit "commits" the
    // Metal command buffers but they aren't "scheduled" in the global queue
    // immediately. (that work seems offloaded to a different thread?)
    // Wait for all the previous submitted commands to be scheduled to have
    // scheduling races between commands using the IOSurface on different APIs.
    // This is a blocking call but should be almost instant.
    TRACE_EVENT0("gpu", "DawnIOSurfaceRepresentation::EndAccess");
    dawn::native::metal::WaitForCommandsToBeScheduled(device_);

    dawn_procs_.textureRelease(texture_);
    texture_ = nullptr;
  }

 private:
  base::ScopedCFTypeRef<IOSurfaceRef> io_surface_;
  WGPUDevice device_;
  WGPUTexture texture_ = nullptr;
  WGPUTextureFormat wgpu_format_;

  // TODO(cwallez@chromium.org): Load procs only once when the factory is
  // created and pass a pointer to them around?
  DawnProcTable dawn_procs_;
};
#endif  // BUILDFLAG(USE_DAWN)

// static
sk_sp<SkPromiseImageTexture>
IOSurfaceImageBackingFactory::ProduceSkiaPromiseTextureMetal(
    SharedImageBacking* backing,
    scoped_refptr<SharedContextState> context_state,
    scoped_refptr<gl::GLImage> image) {
  DCHECK(context_state->GrContextIsMetal());

  base::ScopedCFTypeRef<IOSurfaceRef> io_surface =
      static_cast<gl::GLImageIOSurface*>(image.get())->io_surface();

  id<MTLDevice> mtl_device =
      context_state->metal_context_provider()->GetMTLDevice();
  auto mtl_texture = CreateMetalTexture(mtl_device, io_surface.get(),
                                        backing->size(), backing->format());
  DCHECK(mtl_texture);

  GrMtlTextureInfo info;
  info.fTexture.retain(mtl_texture.get());
  auto gr_backend_texture =
      GrBackendTexture(backing->size().width(), backing->size().height(),
                       GrMipMapped::kNo, info);
  return SkPromiseImageTexture::Make(gr_backend_texture);
}

// static
std::unique_ptr<DawnImageRepresentation>
IOSurfaceImageBackingFactory::ProduceDawn(SharedImageManager* manager,
                                          SharedImageBacking* backing,
                                          MemoryTypeTracker* tracker,
                                          WGPUDevice device,
                                          scoped_refptr<gl::GLImage> image) {
#if BUILDFLAG(USE_DAWN)
  // See comments in IOSurfaceImageBackingFactory::CreateSharedImage
  // regarding RGBA versus BGRA.
  viz::ResourceFormat actual_format = (backing->format()).resource_format();
  if (actual_format == viz::RGBA_8888)
    actual_format = viz::BGRA_8888;

  auto io_surface = GetIOSurfaceFromImage(image);
  if (!io_surface)
    return nullptr;

  // TODO(crbug.com/1293514): Remove this if condition after using single
  // multiplanar mailbox and actual_format could report multiplanar format
  // correctly.
  if (IOSurfaceGetPixelFormat(io_surface) == '420v')
    actual_format = viz::YUV_420_BIPLANAR;

  absl::optional<WGPUTextureFormat> wgpu_format =
      viz::ToWGPUFormat(actual_format);
  if (wgpu_format.value() == WGPUTextureFormat_Undefined)
    return nullptr;

  return std::make_unique<DawnIOSurfaceRepresentation>(
      manager, backing, tracker, device, io_surface, wgpu_format.value());
#else   // BUILDFLAG(USE_DAWN)
  return nullptr;
#endif  // BUILDFLAG(USE_DAWN)
}

// static
bool IOSurfaceImageBackingFactory::InitializePixels(
    SharedImageBacking* backing,
    scoped_refptr<gl::GLImage> image,
    const uint8_t* src_data) {
  auto io_surface = GetIOSurfaceFromImage(image);
  if (!io_surface)
    return false;

  IOReturn r = IOSurfaceLock(io_surface, kIOSurfaceLockAvoidSync, nullptr);
  DCHECK_EQ(kIOReturnSuccess, r);

  uint8_t* dst_data =
      reinterpret_cast<uint8_t*>(IOSurfaceGetBaseAddress(io_surface));
  size_t dst_stride = IOSurfaceGetBytesPerRow(io_surface);
  const size_t src_stride =
      (BitsPerPixel(backing->format()) / 8) * backing->size().width();

  size_t height = backing->size().height();
  for (size_t y = 0; y < height; ++y) {
    memcpy(dst_data, src_data, src_stride);
    dst_data += dst_stride;
    src_data += src_stride;
  }

  r = IOSurfaceUnlock(io_surface, 0, nullptr);
  DCHECK_EQ(kIOReturnSuccess, r);
  return true;
}

}  // namespace gpu
