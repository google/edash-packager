// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/webm/webm_muxer.h"

#include "packager/media/base/fourccs.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/formats/webm/mkv_writer.h"
#include "packager/media/formats/webm/multi_segment_segmenter.h"
#include "packager/media/formats/webm/single_segment_segmenter.h"
#include "packager/media/formats/webm/two_pass_single_segment_segmenter.h"

namespace shaka {
namespace media {
namespace webm {

WebMMuxer::WebMMuxer(const MuxerOptions& options) : Muxer(options) {}
WebMMuxer::~WebMMuxer() {}

Status WebMMuxer::InitializeMuxer() {
  CHECK_EQ(streams().size(), 1U);

  if (streams()[0]->is_encrypted() &&
      streams()[0]->encryption_config().protection_scheme != FOURCC_cenc) {
    LOG(ERROR) << "WebM does not support protection scheme other than 'cenc'.";
    return Status(error::INVALID_ARGUMENT,
                  "WebM does not support protection scheme other than 'cenc'.");
  }

  if (!options().segment_template.empty()) {
    segmenter_.reset(new MultiSegmentSegmenter(options()));
  } else {
    segmenter_.reset(new TwoPassSingleSegmentSegmenter(options()));
  }

  Status initialized = segmenter_->Initialize(
      *streams()[0], progress_listener(), muxer_listener());
  if (!initialized.ok())
    return initialized;

  FireOnMediaStartEvent();
  return Status::OK;
}

Status WebMMuxer::Finalize() {
  if (!segmenter_)
    return Status::OK;
  Status segmenter_finalized = segmenter_->Finalize();

  if (!segmenter_finalized.ok())
    return segmenter_finalized;

  FireOnMediaEndEvent();
  LOG(INFO) << "WEBM file '" << options().output_file_name << "' finalized.";
  return Status::OK;
}

Status WebMMuxer::AddMediaSample(size_t stream_id, const MediaSample& sample) {
  DCHECK(segmenter_);
  DCHECK_EQ(stream_id, 0u);
  if (sample.pts() < 0) {
    LOG(ERROR) << "Seeing negative timestamp " << sample.pts();
    return Status(error::MUXER_FAILURE, "Unsupported negative timestamp.");
  }
  return segmenter_->AddSample(sample);
}

Status WebMMuxer::FinalizeSegment(size_t stream_id,
                                  const SegmentInfo& segment_info) {
  DCHECK(segmenter_);
  DCHECK_EQ(stream_id, 0u);

  if (segment_info.key_rotation_encryption_config) {
    NOTIMPLEMENTED() << "Key rotation is not implemented for WebM.";
    return Status(error::UNIMPLEMENTED,
                  "Key rotation is not implemented for WebM");
  }
  return segmenter_->FinalizeSegment(
      segment_info.start_timestamp, segment_info.duration,
      segment_info.is_subsegment, segment_info.segment_number);
}

void WebMMuxer::FireOnMediaStartEvent() {
  if (!muxer_listener())
    return;

  DCHECK(!streams().empty()) << "Media started without a stream.";

  const uint32_t timescale = streams().front()->time_scale();
  muxer_listener()->OnMediaStart(options(), *streams().front(), timescale,
                                 MuxerListener::kContainerWebM);
}

void WebMMuxer::FireOnMediaEndEvent() {
  if (!muxer_listener())
    return;

  MuxerListener::MediaRanges media_range;

  uint64_t init_range_start = 0;
  uint64_t init_range_end = 0;
  const bool has_init_range =
      segmenter_->GetInitRangeStartAndEnd(&init_range_start, &init_range_end);
  if (has_init_range) {
    Range r;
    r.start = init_range_start;
    r.end = init_range_end;
    media_range.init_range = r;
  }

  uint64_t index_range_start = 0;
  uint64_t index_range_end = 0;
  const bool has_index_range = segmenter_->GetIndexRangeStartAndEnd(
      &index_range_start, &index_range_end);
  if (has_index_range) {
    Range r;
    r.start = index_range_start;
    r.end = index_range_end;
    media_range.index_range = r;
  }

  media_range.subsegment_ranges = segmenter_->GetSegmentRanges();

  const float duration_seconds = segmenter_->GetDurationInSeconds();
  muxer_listener()->OnMediaEnd(media_range, duration_seconds);
}

}  // namespace webm
}  // namespace media
}  // namespace shaka
