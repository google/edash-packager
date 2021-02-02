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
#include "packager/media/formats/mp2t/single_segment_ts_segmenter.h"
#include "packager/media/formats/mp2t/multi_segment_ts_segmenter.h"

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

  void FireOnMediaStartEvent();
  void FireOnMediaEndEvent();

  std::unique_ptr<TsSegmenter> segmenter_;
  int64_t sample_durations_[2];
  int64_t num_samples_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TsMuxer);
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_MP2T_TS_MUXER_H_
