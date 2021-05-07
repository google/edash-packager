// Copyright 2020 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/base/text_muxer.h"

#include "packager/media/base/muxer_util.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {

TextMuxer::TextMuxer(const MuxerOptions& options) : Muxer(options) {}
TextMuxer::~TextMuxer() {}

Status TextMuxer::InitializeMuxer() {
  if (streams().size() != 1 || streams()[0]->stream_type() != kStreamText) {
    return Status(error::MUXER_FAILURE,
                  "Incorrect streams given to WebVTT muxer");
  }

  auto copy = streams()[0]->Clone();
  RETURN_IF_ERROR(InitializeStream(static_cast<TextStreamInfo*>(copy.get())));

  muxer_listener()->OnMediaStart(options(), *copy, copy->time_scale(),
                                 MuxerListener::kContainerText);

  last_cue_ms_ = 0;
  return Status::OK;
}

Status TextMuxer::Finalize() {
  const float duration_ms = static_cast<float>(total_duration_ms_);
  float duration_seconds = duration_ms / 1000;

  // If we haven't seen any segments, this is a single-file.  In this case,
  // flush the single segment.
  MuxerListener::MediaRanges ranges;
  if (duration_seconds == 0 && last_cue_ms_ != 0) {
    DCHECK(options().segment_template.empty());
    duration_seconds = static_cast<float>(last_cue_ms_) / 1000;

    uint64_t size;
    RETURN_IF_ERROR(WriteToFile(options().output_file_name, &size));
    // Insert a dummy value so the HLS generator will generate a segment list.
    ranges.subsegment_ranges.emplace_back();

    // The segment number does not matter for single segment output.
    const uint32_t kArbitrarySegmentNumber = 0;

    muxer_listener()->OnNewSegment(
        options().output_file_name, 0,
        duration_seconds * streams()[0]->time_scale(), size,
        kArbitrarySegmentNumber);
  }

  muxer_listener()->OnMediaEnd(ranges, duration_seconds);

  return Status::OK;
}

Status TextMuxer::AddTextSample(size_t stream_id, const TextSample& sample) {
  // Ignore sync samples.
  if (sample.body().is_empty()) {
    return Status::OK;
  }

  RETURN_IF_ERROR(AddTextSampleInternal(sample));

  last_cue_ms_ = sample.EndTime();
  return Status::OK;
}

Status TextMuxer::FinalizeSegment(size_t stream_id,
                                  const SegmentInfo& segment_info) {
  total_duration_ms_ += segment_info.duration;

  const std::string& segment_template = options().segment_template;
  DCHECK(!segment_template.empty());
  const uint32_t segment_number = segment_info.segment_number;
  const uint64_t start = segment_info.start_timestamp;
  const uint64_t duration = segment_info.duration;
  const uint32_t bandwidth = options().bandwidth;

  const std::string filename =
      GetSegmentName(segment_template, start, segment_number, bandwidth);
  uint64_t size;
  RETURN_IF_ERROR(WriteToFile(filename, &size));

  muxer_listener()->OnNewSegment(filename, start, duration, size,
                                 segment_number);
  return Status::OK;
}

}  // namespace media
}  // namespace shaka
