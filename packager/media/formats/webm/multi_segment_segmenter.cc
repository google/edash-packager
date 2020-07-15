// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/webm/multi_segment_segmenter.h"

#include "packager/media/base/muxer_options.h"
#include "packager/media/base/muxer_util.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/status_macros.h"
#include "packager/third_party/libwebm/src/mkvmuxer.hpp"

namespace shaka {
namespace media {
namespace webm {

MultiSegmentSegmenter::MultiSegmentSegmenter(const MuxerOptions& options)
    : Segmenter(options), num_segment_(0) {}

MultiSegmentSegmenter::~MultiSegmentSegmenter() {}

Status MultiSegmentSegmenter::FinalizeSegment(uint64_t start_timestamp,
                                              uint64_t duration_timestamp,
                                              bool is_subsegment) {
  CHECK(cluster());
  RETURN_IF_ERROR(Segmenter::FinalizeSegment(
      start_timestamp, duration_timestamp, is_subsegment));
  
  if (!cluster()->Finalize())
    return Status(error::FILE_FAILURE, "Error finalizing segment.");

  if (!is_subsegment) {
    std::string segment_name =
        GetSegmentName(options().segment_template, start_timestamp,
                       num_segment_++, options().bandwidth);

    RETURN_IF_ERROR(writer_->WriteToFile(segment_name));

    if (muxer_listener()) {
      const uint64_t size = cluster()->Size();
      muxer_listener()->OnNewSegment(segment_name, start_timestamp,
                                     duration_timestamp, size);
    }
    VLOG(1) << "WEBM file '" << segment_name << "' finalized.";
  }
  return Status::OK;
}

bool MultiSegmentSegmenter::GetInitRangeStartAndEnd(uint64_t* start,
                                                    uint64_t* end) {
  return false;
}

bool MultiSegmentSegmenter::GetIndexRangeStartAndEnd(uint64_t* start,
                                                     uint64_t* end) {
  return false;
}

std::vector<Range> MultiSegmentSegmenter::GetSegmentRanges() {
  return std::vector<Range>();
}

Status MultiSegmentSegmenter::DoInitialize() {
  std::unique_ptr<BufferMkvWriter> writer(new BufferMkvWriter);
  writer_ = std::move(writer);
  Status status = WriteSegmentHeader(0, writer_.get());
  if (!status.ok())
    return status;
  
  return writer_->WriteToFile(options().output_file_name);
}

Status MultiSegmentSegmenter::DoFinalize() {
  return Status::OK;
}

Status MultiSegmentSegmenter::NewSegment(uint64_t start_timestamp,
                                         bool is_subsegment) {
 if (!is_subsegment) {
    writer_.reset(new BufferMkvWriter);
  }

  const uint64_t start_timecode = FromBmffTimestamp(start_timestamp);
  return SetCluster(start_timecode, 0, writer_.get());
}

}  // namespace webm
}  // namespace media
}  // namespace shaka
