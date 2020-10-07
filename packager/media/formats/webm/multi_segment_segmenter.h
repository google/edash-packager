// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_WEBM_MULTI_SEGMENT_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_WEBM_MULTI_SEGMENT_SEGMENTER_H_

#include <memory>

#include "packager/media/formats/webm/mkv_writer.h"
#include "packager/media/formats/webm/segmenter.h"
#include "packager/status.h"

namespace shaka {
namespace media {

struct MuxerOptions;

namespace webm {

/// An implementation of a Segmenter for a multi-segment.  Since this does not
/// use seeking, it does not matter if the underlying files support seeking.
class MultiSegmentSegmenter : public Segmenter {
 public:
  explicit MultiSegmentSegmenter(const MuxerOptions& options);
  ~MultiSegmentSegmenter() override;

  /// @name Segmenter implementation overrides.
  /// @{
  Status FinalizeSegment(uint64_t start_timestamp,
                         uint64_t duration_timestamp,
                         bool is_subsegment,
                         int64_t segment_index) override;
  bool GetInitRangeStartAndEnd(uint64_t* start, uint64_t* end) override;
  bool GetIndexRangeStartAndEnd(uint64_t* start, uint64_t* end) override;
  std::vector<Range> GetSegmentRanges() override;
  /// @}

 protected:
  // Segmenter implementation overrides.
  Status DoInitialize() override;
  Status DoFinalize() override;

 private:
  // Segmenter implementation overrides.
  Status NewSegment(uint64_t start_timestamp, bool is_subsegment) override;

  std::unique_ptr<MkvWriter> writer_;
  uint32_t num_segment_;
  std::string temp_file_name_;

  DISALLOW_COPY_AND_ASSIGN(MultiSegmentSegmenter);
};

}  // namespace webm
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_WEBM_MULTI_SEGMENT_SEGMENTER_H_
