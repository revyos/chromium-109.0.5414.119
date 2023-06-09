// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_V4L2_TEST_VP8_DECODER_H_
#define MEDIA_GPU_V4L2_TEST_VP8_DECODER_H_

#include "base/files/memory_mapped_file.h"
#include "media/filters/ivf_parser.h"
#include "media/gpu/v4l2/test/v4l2_ioctl_shim.h"
#include "media/gpu/v4l2/test/video_decoder.h"
#include "media/parsers/vp8_parser.h"

namespace media {
namespace v4l2_test {

class Vp8Decoder : public VideoDecoder {
 public:
  Vp8Decoder(const Vp8Decoder&) = delete;
  Vp8Decoder& operator=(const Vp8Decoder&) = delete;
  ~Vp8Decoder() override;

  // Creates a Vp8Decoder after verifying that the underlying implementation
  // supports VP8 stateless decoding.
  static std::unique_ptr<Vp8Decoder> Create(
      const base::MemoryMappedFile& stream);

  // Parses next frame from IVF stream and decodes the frame. This method will
  // place the Y, U, and V values into the respective vectors and update the
  // size with the display area size of the decoded frame.
  VideoDecoder::Result DecodeNextFrame(std::vector<char>& y_plane,
                                       std::vector<char>& u_plane,
                                       std::vector<char>& v_plane,
                                       gfx::Size& size,
                                       const int frame_number) override;

 private:
  Vp8Decoder(std::unique_ptr<IvfParser> ivf_parser,
             std::unique_ptr<V4L2IoctlShim> v4l2_ioctl,
             std::unique_ptr<V4L2Queue> OUTPUT_queue,
             std::unique_ptr<V4L2Queue> CAPTURE_queue);
  enum ParseResult { kOk, kEOStream, kError };

  ParseResult ReadNextFrame(Vp8FrameHeader& vp8_frame_header);

  // Parser for the IVF stream to decode.
  const std::unique_ptr<IvfParser> ivf_parser_;

  // VP8-specific data.
  const std::unique_ptr<Vp8Parser> vp8_parser_;

  // Reference frames currently in use.
  std::array<scoped_refptr<MmapedBuffer>, kNumVp8ReferenceBuffers> ref_frames_;
};

}  // namespace v4l2_test
}  // namespace media

#endif  // MEDIA_GPU_V4L2_TEST_VP8_DECODER_H_
