// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp4/multi_segment_segmenter.h"

#include <algorithm>

#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/string_util.h"
#include "packager/file/file.h"
#include "packager/file/file_closer.h"
#include "packager/media/base/buffer_writer.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/muxer_util.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/formats/mp4/box_definitions.h"
#include "packager/media/formats/mp4/key_frame_info.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {
namespace mp4 {

MultiSegmentSegmenter::MultiSegmentSegmenter(const MuxerOptions& options,
                                             std::unique_ptr<FileType> ftyp,
                                             std::unique_ptr<Movie> moov)
    : Segmenter(options, std::move(ftyp), std::move(moov)),
      styp_(new SegmentType) {
  // Use the same brands for styp as ftyp.
  styp_->major_brand = Segmenter::ftyp()->major_brand;
  styp_->compatible_brands = Segmenter::ftyp()->compatible_brands;
  // Replace 'cmfc' with 'cmfs' for CMAF segments compatibility.
  std::replace(styp_->compatible_brands.begin(), styp_->compatible_brands.end(),
               FOURCC_cmfc, FOURCC_cmfs);
}

MultiSegmentSegmenter::~MultiSegmentSegmenter() {}

bool MultiSegmentSegmenter::GetInitRange(size_t* offset, size_t* size) {
  VLOG(1) << "MultiSegmentSegmenter outputs init segment: "
          << options().output_file_name;
  return false;
}

bool MultiSegmentSegmenter::GetIndexRange(size_t* offset, size_t* size) {
  VLOG(1) << "MultiSegmentSegmenter does not have index range.";
  return false;
}

std::vector<Range> MultiSegmentSegmenter::GetSegmentRanges() {
  VLOG(1) << "MultiSegmentSegmenter does not have media segment ranges.";
  return std::vector<Range>();
}

Status MultiSegmentSegmenter::DoInitialize() {
  return WriteInitSegment();
}

Status MultiSegmentSegmenter::DoFinalize() {
  // Update init segment with media duration set.
  RETURN_IF_ERROR(WriteInitSegment());
  SetComplete();
  return Status::OK;
}

Status MultiSegmentSegmenter::DoFinalizeSegment(int64_t segment_number) {
  return WriteSegment(segment_number);
}

Status MultiSegmentSegmenter::WriteInitSegment() {
  DCHECK(ftyp());
  DCHECK(moov());
  // Generate the output file with init segment.
  std::unique_ptr<File, FileCloser> file(
      File::Open(options().output_file_name.c_str(), "w"));
  if (!file) {
    return Status(error::FILE_FAILURE,
                  "Cannot open file for write " + options().output_file_name);
  }
  std::unique_ptr<BufferWriter> buffer(new BufferWriter);
  ftyp()->Write(buffer.get());
  moov()->Write(buffer.get());
  return buffer->WriteToFile(file.get());
}

Status MultiSegmentSegmenter::WriteSegment(int64_t segment_number) {
  DCHECK(sidx());
  DCHECK(fragment_buffer());
  DCHECK(styp_);

  DCHECK(!sidx()->references.empty());
  // earliest_presentation_time is the earliest presentation time of any access
  // unit in the reference stream in the first subsegment.
  sidx()->earliest_presentation_time =
      sidx()->references[0].earliest_presentation_time;

  std::unique_ptr<BufferWriter> buffer(new BufferWriter());
  std::unique_ptr<File, FileCloser> file;
  std::string file_name;
  if (options().segment_template.empty()) {
    // Append the segment to output file if segment template is not specified.
    file_name = options().output_file_name.c_str();
    file.reset(File::Open(file_name.c_str(), "a"));
    if (!file) {
      return Status(error::FILE_FAILURE, "Cannot open file for append " +
                                             options().output_file_name);
    }
  } else {
    file_name = GetSegmentName(options().segment_template,
                               sidx()->earliest_presentation_time,
                               segment_number, options().bandwidth);
    file.reset(File::Open(file_name.c_str(), "w"));
    if (!file) {
      return Status(error::FILE_FAILURE,
                    "Cannot open file for write " + file_name);
    }
    styp_->Write(buffer.get());
  }

  if (options().mp4_params.generate_sidx_in_media_segments)
    sidx()->Write(buffer.get());

  const size_t segment_header_size = buffer->Size();
  const size_t segment_size = segment_header_size + fragment_buffer()->Size();
  DCHECK_NE(segment_size, 0u);

  RETURN_IF_ERROR(buffer->WriteToFile(file.get()));
  if (muxer_listener()) {
    for (const KeyFrameInfo& key_frame_info : key_frame_infos()) {
      muxer_listener()->OnKeyFrame(
          key_frame_info.timestamp,
          segment_header_size + key_frame_info.start_byte_offset,
          key_frame_info.size);
    }
  }
  RETURN_IF_ERROR(fragment_buffer()->WriteToFile(file.get()));

  // Close the file, which also does flushing, to make sure the file is written
  // before manifest is updated.
  if (!file.release()->Close()) {
    return Status(
        error::FILE_FAILURE,
        "Cannot close file " + file_name +
            ", possibly file permission issue or running out of disk space.");
  }

  uint64_t segment_duration = 0;
  // ISO/IEC 23009-1:2012: the value shall be identical to sum of the the
  // values of all Subsegment_duration fields in the first ‘sidx’ box.
  for (size_t i = 0; i < sidx()->references.size(); ++i)
    segment_duration += sidx()->references[i].subsegment_duration;

  UpdateProgress(segment_duration);
  if (muxer_listener()) {
    muxer_listener()->OnSampleDurationReady(sample_duration());
    muxer_listener()->OnNewSegment(
        file_name, sidx()->earliest_presentation_time, segment_duration,
        segment_size, segment_number);
  }

  return Status::OK;
}

}  // namespace mp4
}  // namespace media
}  // namespace shaka
