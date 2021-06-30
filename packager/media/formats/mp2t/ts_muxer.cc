// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/ts_muxer.h"

#include "packager/media/base/muxer_util.h"
#include "packager/status_macros.h"

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

  if (options().segment_template.empty()) {
    const std::string& file_name = options().output_file_name;
    DCHECK(!file_name.empty());
    output_file_.reset(File::Open(file_name.c_str(), "w"));
    if (!output_file_) {
      return Status(error::FILE_FAILURE,
                    "Cannot open file for write " + file_name);
    }
  }

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

  if (segment_info.is_subsegment)
    return Status::OK;

  Status s = segmenter_->FinalizeSegment(segment_info.start_timestamp,
                                         segment_info.duration);
  if (!s.ok())
    return s;
  if (!segmenter_->segment_started())
    return Status::OK;

  int64_t segment_start_timestamp = segmenter_->segment_start_timestamp();

  std::string segment_path =
      options().segment_template.empty()
          ? options().output_file_name
          : GetSegmentName(options().segment_template, segment_start_timestamp,
                           segment_number_++, options().bandwidth);

  const int64_t file_size = segmenter_->segment_buffer()->Size();

  RETURN_IF_ERROR(WriteSegment(segment_path, segmenter_->segment_buffer()));

  total_duration_ += segment_info.duration;

  if (muxer_listener()) {
    muxer_listener()->OnNewSegment(
        segment_path,
        segment_info.start_timestamp * segmenter_->timescale() +
            segmenter_->transport_stream_timestamp_offset(),
        segment_info.duration * segmenter_->timescale(), file_size);
  }

  segmenter_->set_segment_started(false);

  return Status::OK;
}

Status TsMuxer::WriteSegment(const std::string& segment_path,
                             BufferWriter* segment_buffer) {
  std::unique_ptr<File, FileCloser> file;

  if (output_file_) {
    // This is in single segment mode.
    Range range;
    range.start = media_ranges_.subsegment_ranges.empty()
                      ? 0
                      : (media_ranges_.subsegment_ranges.back().end + 1);
    range.end = range.start + segment_buffer->Size() - 1;
    media_ranges_.subsegment_ranges.push_back(range);
  } else {
    file.reset(File::Open(segment_path.c_str(), "w"));
    if (!file) {
      return Status(error::FILE_FAILURE,
                    "Cannot open file for write " + segment_path);
    }
  }

  RETURN_IF_ERROR(segment_buffer->WriteToFile(output_file_ ? output_file_.get()
                                                           : file.get()));

  if (file)
    RETURN_IF_ERROR(CloseFile(std::move(file)));
  return Status::OK;
}

Status TsMuxer::CloseFile(std::unique_ptr<File, FileCloser> file) {
  std::string file_name = file->file_name();
  if (!file.release()->Close()) {
    return Status(
        error::FILE_FAILURE,
        "Cannot close file " + file_name +
            ", possibly file permission issue or running out of disk space.");
  }
  return Status::OK;
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

  muxer_listener()->OnMediaEnd(media_ranges_, total_duration_);
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
