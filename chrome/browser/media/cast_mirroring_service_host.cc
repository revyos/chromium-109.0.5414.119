// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/cast_mirroring_service_host.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/ref_counted.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/media/cast_remoting_connector.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/webrtc/media_stream_capture_indicator.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "components/mirroring/browser/single_client_video_capture_host.h"
#include "components/mirroring/mojom/cast_message_channel.mojom.h"
#include "components/mirroring/mojom/mirroring_service.mojom.h"
#include "components/mirroring/mojom/session_observer.mojom.h"
#include "components/mirroring/mojom/session_parameters.mojom.h"
#include "content/public/browser/audio_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/desktop_streams_registry.h"
#include "content/public/browser/gpu_client.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/video_capture_device_launcher.h"
#include "content/public/browser/web_contents.h"
#include "media/audio/audio_device_description.h"
#include "media/mojo/mojom/audio_data_pipe.mojom.h"
#include "media/mojo/mojom/audio_input_stream.mojom.h"
#include "media/mojo/mojom/audio_processing.mojom.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "services/viz/public/mojom/gpu.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "url/origin.h"

using content::BrowserThread;

namespace mirroring {

namespace {

using media::mojom::AudioInputStream;
using media::mojom::AudioInputStreamClient;
using media::mojom::AudioInputStreamObserver;

// Default resolution constraint.
constexpr gfx::Size kMaxResolution(1920, 1080);

void CreateVideoCaptureHostOnIO(
    const std::string& device_id,
    blink::mojom::MediaStreamType type,
    mojo::PendingReceiver<media::mojom::VideoCaptureHost> receiver) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  scoped_refptr<base::SingleThreadTaskRunner> device_task_runner =
      base::ThreadPool::CreateSingleThreadTaskRunner(
          {base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
          base::SingleThreadTaskRunnerThreadMode::DEDICATED);
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<SingleClientVideoCaptureHost>(
          device_id, type,
          base::BindRepeating(&content::VideoCaptureDeviceLauncher::
                                  CreateInProcessVideoCaptureDeviceLauncher,
                              std::move(device_task_runner))),
      std::move(receiver));
}

blink::mojom::MediaStreamType ConvertVideoStreamType(
    content::DesktopMediaID::Type type) {
  switch (type) {
    case content::DesktopMediaID::TYPE_NONE:
      return blink::mojom::MediaStreamType::NO_SERVICE;
    case content::DesktopMediaID::TYPE_WEB_CONTENTS:
      return blink::mojom::MediaStreamType::GUM_TAB_VIDEO_CAPTURE;
    case content::DesktopMediaID::TYPE_SCREEN:
    case content::DesktopMediaID::TYPE_WINDOW:
      return blink::mojom::MediaStreamType::GUM_DESKTOP_VIDEO_CAPTURE;
  }

  // To suppress compiler warning on Windows.
  return blink::mojom::MediaStreamType::NO_SERVICE;
}

// Get the content::WebContents associated with the given |id|.
content::WebContents* GetContents(
    const content::WebContentsMediaCaptureId& id) {
  return content::WebContents::FromRenderFrameHost(
      content::RenderFrameHost::FromID(id.render_process_id,
                                       id.main_render_frame_id));
}

content::DesktopMediaID BuildMediaIdForWebContents(
    content::WebContents* contents) {
  content::DesktopMediaID media_id;
  if (!contents)
    return media_id;
  media_id.type = content::DesktopMediaID::TYPE_WEB_CONTENTS;
  media_id.web_contents_id = content::WebContentsMediaCaptureId(
      contents->GetPrimaryMainFrame()->GetProcess()->GetID(),
      contents->GetPrimaryMainFrame()->GetRoutingID(),
      true /* disable_local_echo */);
  return media_id;
}

// Returns the size of the primary display in pixels, or absl::nullopt if it
// cannot be determined.
absl::optional<gfx::Size> GetScreenResolution() {
  display::Screen* screen = display::Screen::GetScreen();
  if (!screen) {
    DVLOG(1) << "Cannot get the Screen object.";
    return absl::nullopt;
  }
  return screen->GetPrimaryDisplay().GetSizeInPixel();
}

}  // namespace

// static
void CastMirroringServiceHost::GetForTab(
    content::WebContents* target_contents,
    mojo::PendingReceiver<mojom::MirroringServiceHost> receiver) {
  if (target_contents) {
    const content::DesktopMediaID media_id =
        BuildMediaIdForWebContents(target_contents);
    mojo::MakeSelfOwnedReceiver(
        std::make_unique<CastMirroringServiceHost>(media_id),
        std::move(receiver));
  }
}

// static
void CastMirroringServiceHost::GetForDesktop(
    const content::DesktopMediaID& media_id,
    mojo::PendingReceiver<mojom::MirroringServiceHost> receiver) {
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<CastMirroringServiceHost>(media_id),
      std::move(receiver));
}

// static
void CastMirroringServiceHost::GetForOffscreenTab(
    content::BrowserContext* context,
    const GURL& presentation_url,
    const std::string& presentation_id,
    mojo::PendingReceiver<mojom::MirroringServiceHost> receiver) {
#if BUILDFLAG(ENABLE_EXTENSIONS)
  auto host =
      std::make_unique<CastMirroringServiceHost>(content::DesktopMediaID());
  host->OpenOffscreenTab(context, presentation_url, presentation_id);
  mojo::MakeSelfOwnedReceiver(std::move(host), std::move(receiver));
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
}

CastMirroringServiceHost::CastMirroringServiceHost(
    content::DesktopMediaID source_media_id)
    : source_media_id_(source_media_id),
      gpu_client_(nullptr, base::OnTaskRunnerDeleter(nullptr)) {
  // Observe the target WebContents for Tab mirroring.
  if (source_media_id_.type == content::DesktopMediaID::TYPE_WEB_CONTENTS)
    Observe(GetContents(source_media_id_.web_contents_id));
}

CastMirroringServiceHost::~CastMirroringServiceHost() {}

void CastMirroringServiceHost::Start(
    mojom::SessionParametersPtr session_params,
    mojo::PendingRemote<mojom::SessionObserver> observer,
    mojo::PendingRemote<mojom::CastMessageChannel> outbound_channel,
    mojo::PendingReceiver<mojom::CastMessageChannel> inbound_channel) {
  // Start() should not be called in the middle of a mirroring session.
  if (mirroring_service_) {
    LOG(WARNING) << "Unexpected Start() call during an active"
                 << "mirroring session";
    return;
  }

  // Launch and connect to the Mirroring Service. The process will run until
  // |mirroring_service_| is reset.
  content::ServiceProcessHost::Launch(
      mirroring_service_.BindNewPipeAndPassReceiver(),
      content::ServiceProcessHost::Options()
          .WithDisplayName("Mirroring Service")
          .Pass());
  mojo::PendingRemote<mojom::ResourceProvider> provider;
  resource_provider_receiver_.Bind(provider.InitWithNewPipeAndPassReceiver());
  mirroring_service_->Start(
      std::move(session_params), GetCaptureResolutionConstraint(),
      std::move(observer), std::move(provider), std::move(outbound_channel),
      std::move(inbound_channel));

  ShowCaptureIndicator();
}

// static
gfx::Size CastMirroringServiceHost::GetCaptureResolutionConstraint() {
  absl::optional<gfx::Size> screen_resolution = GetScreenResolution();
  if (screen_resolution) {
    return GetClampedResolution(screen_resolution.value());
  } else {
    return kMaxResolution;
  }
}

// static
gfx::Size CastMirroringServiceHost::GetClampedResolution(
    gfx::Size screen_resolution) {
  // Use landscape mode dimensions for screens in portrait mode.
  if (screen_resolution.height() > screen_resolution.width()) {
    screen_resolution =
        gfx::Size(screen_resolution.height(), screen_resolution.width());
  }
  const int width_step = 160;
  const int height_step = 90;
  int clamped_width = 0;
  int clamped_height = 0;
  if (kMaxResolution.height() * screen_resolution.width() <
      kMaxResolution.width() * screen_resolution.height()) {
    clamped_width = std::min(kMaxResolution.width(), screen_resolution.width());
    clamped_width = clamped_width - (clamped_width % width_step);
    clamped_height = clamped_width * height_step / width_step;
  } else {
    clamped_height =
        std::min(kMaxResolution.height(), screen_resolution.height());
    clamped_height = clamped_height - (clamped_height % height_step);
    clamped_width = clamped_height * width_step / height_step;
  }

  clamped_width = std::max(clamped_width, width_step);
  clamped_height = std::max(clamped_height, height_step);
  return gfx::Size(clamped_width, clamped_height);
}

void CastMirroringServiceHost::BindGpu(
    mojo::PendingReceiver<viz::mojom::Gpu> receiver) {
  gpu_client_ =
      content::CreateGpuClient(std::move(receiver), base::DoNothing());
}

void CastMirroringServiceHost::GetVideoCaptureHost(
    mojo::PendingReceiver<media::mojom::VideoCaptureHost> receiver) {
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&CreateVideoCaptureHostOnIO, source_media_id_.ToString(),
                     ConvertVideoStreamType(source_media_id_.type),
                     std::move(receiver)));
}

void CastMirroringServiceHost::GetNetworkContext(
    mojo::PendingReceiver<network::mojom::NetworkContext> receiver) {
  network::mojom::NetworkContextParamsPtr network_context_params =
      g_browser_process->system_network_context_manager()
          ->CreateDefaultNetworkContextParams();
  content::CreateNetworkContextInNetworkService(
      std::move(receiver), std::move(network_context_params));
}

void CastMirroringServiceHost::CreateAudioStream(
    mojo::PendingRemote<mojom::AudioStreamCreatorClient> requestor,
    const media::AudioParameters& params,
    uint32_t total_segments) {
  if (!audio_stream_factory_) {
    content::GetAudioService().BindStreamFactory(
        audio_stream_factory_.BindNewPipeAndPassReceiver());
  }

  if (source_media_id_.type == content::DesktopMediaID::TYPE_WEB_CONTENTS) {
    content::WebContents* const contents = web_contents();
    if (!contents) {
      VLOG(1) << "Failed to create audio stream: Invalid source.";
      return;
    }
    const base::UnguessableToken group_id = contents->GetAudioGroupId();

    // Fix for regression: https://crbug.com/1111026
    //
    // Muting of the browser tab's local audio output starts when the first
    // WebContents loopback capture stream is requested. The mute is held so
    // that switching audio capture on/off (between mirroring and remoting
    // modes) does not cause ~1 second blips of audio to bother the user. When
    // this CastMirroringServiceHost is destroyed, the "Muter" will go away,
    // restoring local audio output.
    //
    // There may be other browser features that also mute the same tab (before
    // or during mirroring). The Audio Service allows multiple Muters for the
    // same tab, and so the mute state will remain in-place if requested by
    // those other features.
    if (!web_contents_audio_muter_) {
      audio_stream_factory_->BindMuter(
          web_contents_audio_muter_.BindNewEndpointAndPassReceiver(), group_id);
    }

    CreateAudioStreamForTab(std::move(requestor), params, total_segments,
                            group_id);
  } else {
    CreateAudioStreamForDesktop(std::move(requestor), params, total_segments);
  }
}

void CastMirroringServiceHost::CreateAudioStreamForTab(
    mojo::PendingRemote<mojom::AudioStreamCreatorClient> requestor,
    const media::AudioParameters& params,
    uint32_t total_segments,
    const base::UnguessableToken& group_id) {
  // Stream control message pipes. The pipe endpoints will end up at the Audio
  // Service and the Mirroring Service, not here.
  mojo::MessagePipe pipe_to_audio_service;
  mojo::MessagePipe pipe_to_mirroring_service;

  // The Audio Service's CreateLoopbackStream() API requires an observer, but
  // CastMirroringServiceHost does not care about any of the events. Also, the
  // Audio Service requires that something has to be bound to the receive end of
  // the message pipe or it will kill the stream. Thus, a dummy is provided
  // here.
  class DummyObserver final : public AudioInputStreamObserver {
    void DidStartRecording() final {}
  };
  mojo::MessagePipe observer_pipe;
  mojo::MakeSelfOwnedReceiver(std::make_unique<DummyObserver>(),
                              mojo::PendingReceiver<AudioInputStreamObserver>(
                                  std::move(observer_pipe.handle1)));

  // The following insane glob of code asks the Audio Service to create a
  // loopback stream using the |group_id| as the selector for the tab's audio
  // outputs. One end of the message pipes is passed to the Audio Service via
  // the CreateLoopbackStream() call. Then, when the reply comes back, the other
  // end of the message pipes is passed to the Mirroring Service (the
  // |requestor|), along with the audio data pipe.
  audio_stream_factory_->CreateLoopbackStream(
      mojo::PendingReceiver<AudioInputStream>(
          std::move(pipe_to_audio_service.handle1)),
      mojo::PendingRemote<AudioInputStreamClient>(
          std::move(pipe_to_mirroring_service.handle0), 0),
      mojo::PendingRemote<AudioInputStreamObserver>(
          std::move(observer_pipe.handle0), 0),
      params, total_segments, group_id,
      base::BindOnce(
          [](mojo::PendingRemote<mojom::AudioStreamCreatorClient> requestor,
             mojo::PendingRemote<AudioInputStream> stream,
             mojo::PendingReceiver<AudioInputStreamClient> client,
             media::mojom::ReadOnlyAudioDataPipePtr data_pipe) {
            mojo::Remote<mojom::AudioStreamCreatorClient>(std::move(requestor))
                ->StreamCreated(std::move(stream), std::move(client),
                                std::move(data_pipe));
          },
          std::move(requestor),
          mojo::PendingRemote<AudioInputStream>(
              std::move(pipe_to_audio_service.handle0), 0),
          mojo::PendingReceiver<AudioInputStreamClient>(
              std::move(pipe_to_mirroring_service.handle1))));
}

void CastMirroringServiceHost::CreateAudioStreamForDesktop(
    mojo::PendingRemote<mojom::AudioStreamCreatorClient> requestor,
    const media::AudioParameters& params,
    uint32_t total_segments) {
  // Stream control message pipes. The pipe endpoints will end up at the Audio
  // Service and the Mirroring Service, not here.
  mojo::MessagePipe pipe_to_audio_service;
  mojo::MessagePipe pipe_to_mirroring_service;

  // This does the mostly the same thing as the similar insane glob of code in
  // the CreateAudioStreamForTab() method. Here, system-wide audio is requested
  // from the platform, and so the CreateInputStream() API is used instead of
  // CreateLoopbackStream(). CreateInputStream() is more complex, having a
  // number of optional parameters that people seem to just keep adding more of
  // over time, with little consideration for maintainable code structure, and
  // add to the fun we're having here.
  //
  // See if you can spot all 7 unused fields! :P
  audio_stream_factory_->CreateInputStream(
      mojo::PendingReceiver<AudioInputStream>(
          std::move(pipe_to_audio_service.handle1)),
      mojo::PendingRemote<AudioInputStreamClient>(
          std::move(pipe_to_mirroring_service.handle0), 0),
      mojo::NullRemote(), mojo::NullRemote(),
      media::AudioDeviceDescription::kLoopbackWithMuteDeviceId, params,
      total_segments, false, base::ReadOnlySharedMemoryRegion(), nullptr,
      base::BindOnce(
          [](mojo::PendingRemote<mojom::AudioStreamCreatorClient> requestor,
             mojo::PendingRemote<AudioInputStream> stream,
             mojo::PendingReceiver<AudioInputStreamClient> client,
             media::mojom::ReadOnlyAudioDataPipePtr data_pipe, bool,
             const absl::optional<base::UnguessableToken>&) {
            mojo::Remote<mojom::AudioStreamCreatorClient>(std::move(requestor))
                ->StreamCreated(std::move(stream), std::move(client),
                                std::move(data_pipe));
          },
          std::move(requestor),
          mojo::PendingRemote<AudioInputStream>(
              std::move(pipe_to_audio_service.handle0), 0),
          mojo::PendingReceiver<AudioInputStreamClient>(
              std::move(pipe_to_mirroring_service.handle1))));
}

void CastMirroringServiceHost::ConnectToRemotingSource(
    mojo::PendingRemote<media::mojom::Remoter> remoter,
    mojo::PendingReceiver<media::mojom::RemotingSource> receiver) {
  if (source_media_id_.type == content::DesktopMediaID::TYPE_WEB_CONTENTS) {
    content::WebContents* source_contents = web_contents();
    if (source_contents) {
      CastRemotingConnector::Get(source_contents)
          ->ConnectWithMediaRemoter(std::move(remoter), std::move(receiver));
    }
  }
}

void CastMirroringServiceHost::WebContentsDestroyed() {
  mirroring_service_.reset();
  web_contents_audio_muter_.reset();
  audio_stream_factory_.reset();
  gpu_client_.reset();
}

void CastMirroringServiceHost::ShowCaptureIndicator() {
  if (source_media_id_.type != content::DesktopMediaID::TYPE_WEB_CONTENTS ||
      !web_contents()) {
    return;
  }

  blink::mojom::StreamDevices devices;
  const blink::mojom::MediaStreamType stream_type =
      ConvertVideoStreamType(source_media_id_.type);
  blink::MediaStreamDevice device = blink::MediaStreamDevice(
      stream_type, source_media_id_.ToString(), /* name */ std::string());
  if (blink::IsAudioInputMediaType(stream_type))
    devices.audio_device = device;
  else if (blink::IsVideoInputMediaType(stream_type))
    devices.video_device = device;
  DCHECK(devices.audio_device.has_value() || devices.video_device.has_value());
  media_stream_ui_ = MediaCaptureDevicesDispatcher::GetInstance()
                         ->GetMediaStreamCaptureIndicator()
                         ->RegisterMediaStream(web_contents(), devices);
  media_stream_ui_->OnStarted(
      base::RepeatingClosure(), content::MediaStreamUI::SourceCallback(),
      /*label=*/std::string(), /*screen_capture_ids=*/{},
      content::MediaStreamUI::StateChangeCallback());
}

#if BUILDFLAG(ENABLE_EXTENSIONS)
void CastMirroringServiceHost::DestroyTab(OffscreenTab* tab) {
  if (offscreen_tab_ && (offscreen_tab_.get() == tab))
    offscreen_tab_.reset();
}

void CastMirroringServiceHost::OpenOffscreenTab(
    content::BrowserContext* context,
    const GURL& presentation_url,
    const std::string& presentation_id) {
  DCHECK(!offscreen_tab_);
  offscreen_tab_ = std::make_unique<OffscreenTab>(this, context);
  offscreen_tab_->Start(presentation_url, GetCaptureResolutionConstraint(),
                        presentation_id);
  source_media_id_ = BuildMediaIdForWebContents(offscreen_tab_->web_contents());
  DCHECK_EQ(content::DesktopMediaID::TYPE_WEB_CONTENTS, source_media_id_.type);
  Observe(offscreen_tab_->web_contents());
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

}  // namespace mirroring
