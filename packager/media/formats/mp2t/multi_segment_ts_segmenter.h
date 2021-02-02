// Copyright 2021 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP2T_MULTI_SEGMENT_TS_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_MULTI_SEGMENT_TS_SEGMENTER_H_

#include "packager/media/formats/mp2t/ts_segmenter.h"
namespace shaka {
namespace media {

class KeySource;
class MuxerListener;

namespace mp2t {

class MultiSegmentTsSegmenter : public TsSegmenter {
 public:
  MultiSegmentTsSegmenter(const MuxerOptions& options, MuxerListener* listener);
  ~MultiSegmentTsSegmenter() override;

  /// @name TsSegmenter implementation overrides.
  /// @{
  Status Initialize(const StreamInfo& stream_info) override;
  Status FinalizeSegment(uint64_t start_timestamp, uint64_t duration) override;
  /// @}

 private:
  uint64_t segment_number_ = 0;

  DISALLOW_COPY_AND_ASSIGN(MultiSegmentTsSegmenter);
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
#endif  // PACKAGER_MEDIA_FORMATS_MP2T_MULTI_SEGMENT_TS_SEGMENTER_H_
