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
                                              bool is_subsegment,
                                              int64_t segment_number) {
  CHECK(cluster());
  RETURN_IF_ERROR(Segmenter::FinalizeSegment(
      start_timestamp, duration_timestamp, is_subsegment, segment_number));
  if (!cluster()->Finalize())
    return Status(error::FILE_FAILURE, "Error finalizing segment.");

  if (!is_subsegment) {
    std::string segment_name =
        GetSegmentName(options().segment_template, start_timestamp,
                       segment_number, options().bandwidth);

    // Close the file, which also does flushing, to make sure the file is
    // written before manifest is updated.
    RETURN_IF_ERROR(writer_->Close());

    if (!File::Copy(temp_file_name_.c_str(), segment_name.c_str()))
      return Status(error::FILE_FAILURE, "Failure to copy memory file.");

    if (!File::Delete(temp_file_name_.c_str()))
      return Status(error::FILE_FAILURE, "Failure to delete memory file.");

    num_segment_++;

    if (muxer_listener()) {
      const uint64_t size = cluster()->Size();
      muxer_listener()->OnNewSegment(segment_name, start_timestamp,
                                     duration_timestamp, size, segment_number);
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
  std::unique_ptr<MkvWriter> writer(new MkvWriter);
  Status status = writer->Open(options().output_file_name);
  if (!status.ok())
    return status;
  writer_ = std::move(writer);
  return WriteSegmentHeader(0, writer_.get());
}

Status MultiSegmentSegmenter::DoFinalize() {
  return Status::OK;
}

Status MultiSegmentSegmenter::NewSegment(uint64_t start_timestamp,
                                         bool is_subsegment) {
  if (!is_subsegment) {
    temp_file_name_ =
        "memory://" + GetSegmentName(options().segment_template,
                                     start_timestamp, num_segment_,
                                     options().bandwidth);

    writer_.reset(new MkvWriter);
    Status status = writer_->Open(temp_file_name_);

    if (!status.ok())
      return status;
  }

  const uint64_t start_timecode = FromBmffTimestamp(start_timestamp);
  return SetCluster(start_timecode, 0, writer_.get());
}

}  // namespace webm
}  // namespace media
}  // namespace shaka
