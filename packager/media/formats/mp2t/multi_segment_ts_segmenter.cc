// Copyright 2021 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/multi_segment_ts_segmenter.h"

#include <memory>

#include "packager/file/file_util.h"
#include "packager/media/base/muxer_util.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/status.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {
namespace mp2t {

MultiSegmentTsSegmenter::MultiSegmentTsSegmenter(const MuxerOptions& options,
                                                 MuxerListener* listener)
    : TsSegmenter(options, listener) {}

MultiSegmentTsSegmenter::~MultiSegmentTsSegmenter() {}

Status MultiSegmentTsSegmenter::Initialize(const StreamInfo& stream_info) {
  if (options().segment_template.empty())
    return Status(error::MUXER_FAILURE, "Segment template not specified.");
  return TsSegmenter::Initialize(stream_info);
}

Status MultiSegmentTsSegmenter::FinalizeSegment(uint64_t start_timestamp,
                                                uint64_t duration) {
  Status status = TsSegmenter::FinalizeSegment(start_timestamp, duration);
  if (status != Status::OK)
    return status;

  if (!get_segment_started())
    return Status::OK;

  std::string segment_path =
      GetSegmentName(options().segment_template, get_segment_start_time(),
                     segment_number_++, options().bandwidth);

  const int64_t file_size = getSegmentBuffer().Size();
  std::unique_ptr<File, FileCloser> segment_file;
  segment_file.reset(File::Open(segment_path.c_str(), "w"));
  if (!segment_file) {
    return Status(error::FILE_FAILURE,
                  "Cannot open file for write " + segment_path);
  }

  RETURN_IF_ERROR(getSegmentBuffer().WriteToFile(segment_file.get()));

  if (!segment_file.release()->Close()) {
    return Status(
        error::FILE_FAILURE,
        "Cannot close file " + segment_path +
            ", possibly file permission issue or running out of disk space.");
  }

  if (get_muxer_listener()) {
    get_muxer_listener()->OnNewSegment(
        segment_path,
        start_timestamp * timescale() + transport_stream_timestamp_offset(),
        duration * timescale(), file_size);
  }

  set_segment_started(false);

  return Status::OK;
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
