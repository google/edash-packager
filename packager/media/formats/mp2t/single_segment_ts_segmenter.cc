// Copyright 2021 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/single_segment_ts_segmenter.h"

#include <memory>

#include "packager/file/file_util.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/status.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {
namespace mp2t {

SingleSegmentTsSegmenter::SingleSegmentTsSegmenter(const MuxerOptions& options,
                                                   MuxerListener* listener)
    : TsSegmenter(options, listener) {}

SingleSegmentTsSegmenter::~SingleSegmentTsSegmenter() {}

Status SingleSegmentTsSegmenter::Initialize(const StreamInfo& stream_info) {
  output_file_.reset(
      File::Open(options().output_file_name.c_str(), "w"));

  return TsSegmenter::Initialize(stream_info);
}

Status SingleSegmentTsSegmenter::Finalize() {
  if (output_file_) {
    if (!output_file_.release()->Close()) {
      return Status(
          error::FILE_FAILURE,
          "Cannot close file " + options().output_file_name +
              ", possibly file permission issue or running out of disk space.");
    }
  }
  return Status::OK;
}

Status SingleSegmentTsSegmenter::FinalizeSegment(uint64_t start_timestamp,
                                                 uint64_t duration) {
  if (TsSegmenter::FinalizeSegment(start_timestamp, duration) != Status::OK)
    return Status(error::MUXER_FAILURE, "Failed to flush PesPacketGenerator.");

  if (!get_segment_started())
    return Status::OK;

  const int64_t file_size = getSegmentBuffer().Size();

  // Add to range vector
  uint64_t start_range = end_range_;
  end_range_ += getSegmentBuffer().Size();
  Range r;
  r.start = start_range;
  r.end = end_range_ - 1;
  add_to_range(r);

  RETURN_IF_ERROR(getSegmentBuffer().WriteToFile(output_file_.get()));

  if (get_muxer_listener()) {
    get_muxer_listener()->OnNewSegment(
        options().output_file_name,
        start_timestamp * timescale() + transport_stream_timestamp_offset(),
        duration * timescale(), file_size);
  }

  set_segment_started(false);

  return Status::OK;
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
