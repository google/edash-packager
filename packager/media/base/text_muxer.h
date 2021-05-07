// Copyright 2020 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_BASE_TEXT_MUXER_H_
#define PACKAGER_MEDIA_BASE_TEXT_MUXER_H_

#include "packager/media/base/muxer.h"
#include "packager/media/base/text_sample.h"
#include "packager/media/base/text_stream_info.h"

namespace shaka {
namespace media {

/// Defines a base class for text format (i.e. not MP4) muxers.  This handles
/// separating the single-segment and multi-segment modes.  Derived classes are
/// expected to buffer cues (or text) and write them out in WriteToFile.
class TextMuxer : public Muxer {
 public:
  explicit TextMuxer(const MuxerOptions& options);
  ~TextMuxer() override;

 private:
  // Muxer implementation overrides.
  Status InitializeMuxer() override;
  Status Finalize() override;
  Status AddTextSample(size_t stream_id, const TextSample& sample) override;
  Status FinalizeSegment(size_t stream_id,
                         const SegmentInfo& segment_info) override;

  virtual Status InitializeStream(TextStreamInfo* stream) = 0;
  virtual Status AddTextSampleInternal(const TextSample& sample) = 0;
  /// Writes the buffered samples to the file with the given name.  This should
  /// also clear any buffered samples.
  virtual Status WriteToFile(const std::string& filename, uint64_t* size) = 0;

  uint64_t total_duration_ms_ = 0;
  uint64_t last_cue_ms_ = 0;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_BASE_TEXT_MUXER_H_
