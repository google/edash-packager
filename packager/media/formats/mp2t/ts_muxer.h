// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP2T_TS_MUXER_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_TS_MUXER_H_

#include "packager/base/macros.h"
#include "packager/media/base/muxer.h"
#include "packager/media/formats/mp2t/ts_segmenter.h"

namespace shaka {
namespace media {
namespace mp2t {

/// MPEG2 TS muxer.
/// This is a single program, single elementary stream TS muxer.
class TsMuxer : public Muxer {
 public:
  explicit TsMuxer(const MuxerOptions& muxer_options);
  ~TsMuxer() override;

 private:
  // Muxer implementation.
  Status InitializeMuxer() override;
  Status Finalize() override;
  Status AddMediaSample(size_t stream_id, const MediaSample& sample) override;
  Status FinalizeSegment(size_t stream_id,
                         const SegmentInfo& sample) override;

  Status WriteSegment(const std::string& segment_path,
                      BufferWriter* segment_buffer);
  Status CloseFile(std::unique_ptr<File, FileCloser> file);

  void FireOnMediaStartEvent();
  void FireOnMediaEndEvent();

  std::unique_ptr<TsSegmenter> segmenter_;
  int64_t sample_durations_[2];
  int64_t num_samples_ = 0;

  // Used in multi-segment mode for segment template.
  uint64_t segment_number_ = 0;

  // Used in single segment mode.
  std::unique_ptr<File, FileCloser> output_file_;

  // Keeps track of segment ranges in single segment mode.
  MuxerListener::MediaRanges media_ranges_;

  uint64_t total_duration_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TsMuxer);
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_MP2T_TS_MUXER_H_
