// Copyright 2021 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP2T_SINGLE_SEGMENT_TS_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_SINGLE_SEGMENT_TS_SEGMENTER_H_

#include "packager/media/formats/mp2t/ts_segmenter.h"
namespace shaka {
namespace media {

class KeySource;
class MuxerListener;

namespace mp2t {

/// A single MPEG2TS file with a single segment is created.
class SingleSegmentTsSegmenter : public TsSegmenter {
 public:
  SingleSegmentTsSegmenter(const MuxerOptions& options,
                           MuxerListener* listener);
  ~SingleSegmentTsSegmenter() override;

  /// @name TsSegmenter implementation overrides.
  /// @{
  Status Initialize(const StreamInfo& stream_info) override;
  Status Finalize() override;
  Status FinalizeSegment(uint64_t start_timestamp, uint64_t duration) override;
  /// @}

 private:

  uint64_t end_range_ = 0;
  std::unique_ptr<File, FileCloser> output_file_;

  DISALLOW_COPY_AND_ASSIGN(SingleSegmentTsSegmenter);
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
#endif  // PACKAGER_MEDIA_FORMATS_MP2T_SINGLE_SEGMENT_TS_SEGMENTER_H_
