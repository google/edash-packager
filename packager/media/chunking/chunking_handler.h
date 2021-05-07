// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_CHUNKING_CHUNKING_HANDLER_
#define PACKAGER_MEDIA_CHUNKING_CHUNKING_HANDLER_

#include <atomic>
#include <queue>

#include "packager/base/logging.h"
#include "packager/base/optional.h"
#include "packager/media/base/media_handler.h"
#include "packager/media/public/chunking_params.h"

namespace shaka {
namespace media {

/// ChunkingHandler splits the samples into segments / subsegments based on the
/// specified chunking params.
/// This handler is a one-in one-out handler.
/// There can be multiple chunking handler running in different threads or even
/// different processes, we use the "consistent chunking algorithm" to make sure
/// the chunks in different streams are aligned without explicit communcating
/// with each other - which is not efficient and often difficult.
///
/// Consistent Chunking Algorithm:
///  1. Find the consistent chunkable boundary
///  Let the timestamps for video frames be (t1, t2, t3, ...). Then a
///  consistent chunkable boundary is simply the first chunkable boundary after
///  (tk / N) != (tk-1 / N), where '/' denotes integer division, and N is the
///  intended chunk duration.
///  2. Chunk only at the consistent chunkable boundary
///
/// This algorithm will make sure the chunks from different video streams are
/// aligned if they have aligned GoPs.
class ChunkingHandler : public MediaHandler {
 public:
  explicit ChunkingHandler(const ChunkingParams& chunking_params);
  ~ChunkingHandler() override = default;

 protected:
  /// @name MediaHandler implementation overrides.
  /// @{
  Status InitializeInternal() override;
  Status Process(std::unique_ptr<StreamData> stream_data) override;
  Status OnFlushRequest(size_t input_stream_index) override;
  /// @}

 private:
  friend class ChunkingHandlerTest;

  ChunkingHandler(const ChunkingHandler&) = delete;
  ChunkingHandler& operator=(const ChunkingHandler&) = delete;

  Status OnStreamInfo(std::shared_ptr<const StreamInfo> info);
  Status OnCueEvent(std::shared_ptr<const CueEvent> event);
  Status OnMediaSample(std::shared_ptr<const MediaSample> sample);

  Status EndSegmentIfStarted();
  Status EndSubsegmentIfStarted() const;

  bool IsSubsegmentEnabled() {
    return subsegment_duration_ > 0 &&
           subsegment_duration_ != segment_duration_;
  }

  const ChunkingParams chunking_params_;

  // Segment and subsegment duration in stream's time scale.
  int64_t segment_duration_ = 0;
  int64_t subsegment_duration_ = 0;

  // Segment number that keeps monotically increasing.
  // Set to start_segment_number in constructor.
  int64_t segment_number_ = 1;

  // Current segment index, useful to determine where to do chunking.
  int64_t current_segment_index_ = -1;

  // Current subsegment index, useful to determine where to do chunking.
  int64_t current_subsegment_index_ = -1;

  base::Optional<int64_t> segment_start_time_;
  base::Optional<int64_t> subsegment_start_time_;
  int64_t max_segment_time_ = 0;
  uint32_t time_scale_ = 0;

  // The offset is applied to sample timestamps so a full segment is generated
  // after cue points.
  int64_t cue_offset_ = 0;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_CHUNKING_CHUNKING_HANDLER_
