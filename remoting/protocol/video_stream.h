// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_VIDEO_STREAM_H_
#define REMOTING_PROTOCOL_VIDEO_STREAM_H_

#include "remoting/protocol/input_event_timestamps.h"
#include "third_party/webrtc/modules/desktop_capture/desktop_capture_types.h"

namespace webrtc {
class DesktopSize;
class DesktopVector;
class MouseCursor;
}  // namespace webrtc

namespace remoting::protocol {

class VideoStream {
 public:
  class Observer {
   public:
    // Called to notify about screen size changes. The size is specified in
    // physical pixels.
    virtual void OnVideoSizeChanged(VideoStream* stream,
                                    const webrtc::DesktopSize& size,
                                    const webrtc::DesktopVector& dpi) = 0;
  };

  VideoStream() = default;
  virtual ~VideoStream() = default;

  // Sets event timestamps source to be used for the video stream.
  virtual void SetEventTimestampsSource(
      scoped_refptr<InputEventTimestampsSource> event_timestamps_source) = 0;

  // Pauses or resumes scheduling of frame captures. Pausing/resuming captures
  // only affects capture scheduling and does not stop/start the capturer.
  virtual void Pause(bool pause) = 0;

  // Sets whether the video encoder should be requested to encode losslessly,
  // or to use a lossless color space (typically requiring higher bandwidth).
  virtual void SetLosslessEncode(bool want_lossless) = 0;
  virtual void SetLosslessColor(bool want_lossless) = 0;

  // Control mouse cursor compositing in the video stream.
  virtual void SetComposeEnabled(bool enabled) = 0;
  virtual void SetMouseCursor(
      std::unique_ptr<webrtc::MouseCursor> mouse_cursor) = 0;
  virtual void SetMouseCursorPosition(
      const webrtc::DesktopVector& position) = 0;

  // Sets stream observer.
  virtual void SetObserver(Observer* observer) = 0;

  // Selects the current desktop display (if multiple displays).
  virtual void SelectSource(webrtc::ScreenId id) = 0;
};

}  // namespace remoting::protocol

#endif  // REMOTING_PROTOCOL_VIDEO_STREAM_H_
