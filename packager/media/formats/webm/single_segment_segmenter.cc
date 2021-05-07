// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/webm/single_segment_segmenter.h"

#include "packager/media/base/muxer_options.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/third_party/libwebm/src/mkvmuxer.hpp"

namespace shaka {
namespace media {
namespace webm {

SingleSegmentSegmenter::SingleSegmentSegmenter(const MuxerOptions& options)
    : Segmenter(options), init_end_(0), index_start_(0) {}

SingleSegmentSegmenter::~SingleSegmentSegmenter() {}

Status SingleSegmentSegmenter::FinalizeSegment(uint64_t start_timestamp,
                                               uint64_t duration_timestamp,
                                               bool is_subsegment,
                                               int64_t segment_number) {
  Status status = Segmenter::FinalizeSegment(
      start_timestamp, duration_timestamp, is_subsegment, segment_number);
  if (!status.ok())
    return status;
  // No-op for subsegment in single segment mode.
  if (is_subsegment)
    return Status::OK;
  CHECK(cluster());
  if (!cluster()->Finalize())
    return Status(error::FILE_FAILURE, "Error finalizing cluster.");
  if (muxer_listener()) {
    const uint64_t size = cluster()->Size();
    muxer_listener()->OnNewSegment(options().output_file_name, start_timestamp,
                                   duration_timestamp, size, segment_number);
  }
  return Status::OK;
}

bool SingleSegmentSegmenter::GetInitRangeStartAndEnd(uint64_t* start,
                                                     uint64_t* end) {
  *start = 0;
  *end = init_end_;
  return true;
}

bool SingleSegmentSegmenter::GetIndexRangeStartAndEnd(uint64_t* start,
                                                      uint64_t* end) {
  *start = index_start_;
  *end = index_end_;
  return true;
}

std::vector<Range> SingleSegmentSegmenter::GetSegmentRanges() {
  std::vector<Range> ranges;
  if (cues()->cue_entries_size() == 0) {
    return ranges;
  }
  for (int32_t i = 0; i < cues()->cue_entries_size() - 1; ++i) {
    const mkvmuxer::CuePoint* cue_point = cues()->GetCueByIndex(i);
    Range r;
    // Cue point cluster position is relative to segment payload pos.
    r.start = segment_payload_pos() + cue_point->cluster_pos();
    r.end =
        segment_payload_pos() + cues()->GetCueByIndex(i + 1)->cluster_pos() - 1;
    ranges.push_back(r);
  }

  Range last_range;
  const mkvmuxer::CuePoint* last_cue_point =
      cues()->GetCueByIndex(cues()->cue_entries_size() - 1);
  last_range.start = segment_payload_pos() + last_cue_point->cluster_pos();
  last_range.end = last_range.start + cluster()->Size() - 1;
  ranges.push_back(last_range);
  return ranges;
}

Status SingleSegmentSegmenter::DoInitialize() {
  if (!writer_) {
    std::unique_ptr<MkvWriter> writer(new MkvWriter);
    Status status = writer->Open(options().output_file_name);
    if (!status.ok())
      return status;
    writer_ = std::move(writer);
  }

  Status ret = WriteSegmentHeader(0, writer_.get());
  init_end_ = writer_->Position() - 1;
  seek_head()->set_cluster_pos(init_end_ + 1 - segment_payload_pos());
  return ret;
}

Status SingleSegmentSegmenter::DoFinalize() {
  // Write the Cues to the end of the file.
  index_start_ = writer_->Position();
  seek_head()->set_cues_pos(index_start_ - segment_payload_pos());
  if (!cues()->Write(writer_.get()))
    return Status(error::FILE_FAILURE, "Error writing Cues data.");

  // The WebM index is at the end of the file.
  index_end_ = writer_->Position() - 1;
  writer_->Position(0);

  Status status = WriteSegmentHeader(index_end_ + 1, writer_.get());
  status.Update(writer_->Close());
  return status;
}

Status SingleSegmentSegmenter::NewSegment(uint64_t start_timestamp,
                                          bool is_subsegment) {
  // No-op for subsegment in single segment mode.
  if (is_subsegment)
    return Status::OK;
  // Create a new Cue point.
  uint64_t position = writer_->Position();
  uint64_t start_timecode = FromBmffTimestamp(start_timestamp);

  mkvmuxer::CuePoint* cue_point = new mkvmuxer::CuePoint;
  cue_point->set_time(start_timecode);
  cue_point->set_track(track_id());
  cue_point->set_cluster_pos(position - segment_payload_pos());
  if (!cues()->AddCue(cue_point))
    return Status(error::INTERNAL_ERROR, "Error adding CuePoint.");

  return SetCluster(start_timecode, position, writer_.get());
}

}  // namespace webm
}  // namespace media
}  // namespace shaka
