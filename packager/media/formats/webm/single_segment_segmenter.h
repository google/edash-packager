// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_WEBM_SINGLE_SEGMENT_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_WEBM_SINGLE_SEGMENT_SEGMENTER_H_

#include "packager/media/formats/webm/segmenter.h"

#include <memory>
#include "packager/media/formats/webm/mkv_writer.h"
#include "packager/status.h"

namespace shaka {
namespace media {

struct MuxerOptions;

namespace webm {

/// An implementation of a Segmenter for a single-segment.  This assumes that
/// the output file is seekable.  For non-seekable files, use
/// TwoPassSingleSegmentSegmenter.
class SingleSegmentSegmenter : public Segmenter {
 public:
  explicit SingleSegmentSegmenter(const MuxerOptions& options);
  ~SingleSegmentSegmenter() override;

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
  MkvWriter* writer() { return writer_.get(); }
  uint64_t init_end() { return init_end_; }
  void set_init_end(uint64_t end) { init_end_ = end; }
  void set_index_start(uint64_t start) { index_start_ = start; }
  void set_index_end(uint64_t end) { index_end_ = end; }
  void set_writer(std::unique_ptr<MkvWriter> writer) {
    writer_ = std::move(writer);
  }

  // Segmenter implementation overrides.
  Status DoInitialize() override;
  Status DoFinalize() override;

 private:
  // Segmenter implementation overrides.
  Status NewSegment(uint64_t start_timestamp, bool is_subsegment) override;

  std::unique_ptr<MkvWriter> writer_;
  uint64_t init_end_;
  uint64_t index_start_;
  uint64_t index_end_;

  DISALLOW_COPY_AND_ASSIGN(SingleSegmentSegmenter);
};

}  // namespace webm
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_WEBM_SINGLE_SEGMENT_SEGMENTER_H_
