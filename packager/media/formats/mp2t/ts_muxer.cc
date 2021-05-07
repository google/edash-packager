// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/ts_muxer.h"

namespace shaka {
namespace media {
namespace mp2t {

namespace {
const uint32_t kTsTimescale = 90000;
}  // namespace

TsMuxer::TsMuxer(const MuxerOptions& muxer_options) : Muxer(muxer_options) {}
TsMuxer::~TsMuxer() {}

Status TsMuxer::InitializeMuxer() {
  if (streams().size() > 1u)
    return Status(error::MUXER_FAILURE, "Cannot handle more than one streams.");

  segmenter_.reset(new TsSegmenter(options(), muxer_listener()));
  Status status = segmenter_->Initialize(*streams()[0]);
  FireOnMediaStartEvent();
  return status;
}

Status TsMuxer::Finalize() {
  FireOnMediaEndEvent();
  return segmenter_->Finalize();
}

Status TsMuxer::AddMediaSample(size_t stream_id, const MediaSample& sample) {
  DCHECK_EQ(stream_id, 0u);
  if (num_samples_ < 2) {
    sample_durations_[num_samples_] =
        sample.duration() * kTsTimescale / streams().front()->time_scale();
    if (num_samples_ == 1 && muxer_listener())
      muxer_listener()->OnSampleDurationReady(sample_durations_[num_samples_]);
    num_samples_++;
  }
  return segmenter_->AddSample(sample);
}

Status TsMuxer::FinalizeSegment(size_t stream_id,
                                const SegmentInfo& segment_info) {
  DCHECK_EQ(stream_id, 0u);
  return segment_info.is_subsegment
             ? Status::OK
             : segmenter_->FinalizeSegment(segment_info.start_timestamp,
                                           segment_info.duration,
                                           segment_info.segment_number);
}

void TsMuxer::FireOnMediaStartEvent() {
  if (!muxer_listener())
    return;
  muxer_listener()->OnMediaStart(options(), *streams().front(), kTsTimescale,
                                 MuxerListener::kContainerMpeg2ts);
}

void TsMuxer::FireOnMediaEndEvent() {
  if (!muxer_listener())
    return;

  // For now, there is no single file TS segmenter. So all the values passed
  // here are left empty.
  MuxerListener::MediaRanges range;
  muxer_listener()->OnMediaEnd(range, 0);
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
