// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/webvtt/webvtt_text_output_handler.h"

#include <algorithm>  // needed for min and max

#include "packager/base/logging.h"
#include "packager/file/file.h"
#include "packager/file/file_closer.h"
#include "packager/media/base/muxer_util.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {
namespace {
double kMillisecondsToSeconds = 1000.0;

std::string ToString(const std::vector<uint8_t>& v) {
  return std::string(v.begin(), v.end());
}
}  // namespace

WebVttTextOutputHandler::WebVttTextOutputHandler(
    const MuxerOptions& muxer_options,
    std::unique_ptr<MuxerListener> muxer_listener)
    : muxer_options_(muxer_options),
      muxer_listener_(std::move(muxer_listener)) {}

Status WebVttTextOutputHandler::InitializeInternal() {
  return Status::OK;
}

Status WebVttTextOutputHandler::Process(
    std::unique_ptr<StreamData> stream_data) {
  switch (stream_data->stream_data_type) {
    case StreamDataType::kStreamInfo:
      return OnStreamInfo(*stream_data->stream_info);
    case StreamDataType::kSegmentInfo:
      return OnSegmentInfo(*stream_data->segment_info);
    case StreamDataType::kCueEvent:
      return OnCueEvent(*stream_data->cue_event);
    case StreamDataType::kTextSample:
      OnTextSample(*stream_data->text_sample);
      return Status::OK;
    default:
      return Status(error::INTERNAL_ERROR,
                    "Invalid stream data type for this handler");
  }
}

Status WebVttTextOutputHandler::OnFlushRequest(size_t input_stream_index) {
  if (!buffer_) {
    LOG(INFO) << "Skip stream '" << muxer_options_.segment_template
              << "' which does not contain any sample.";
    return Status::OK;
  }

  DCHECK_EQ(buffer_->sample_count(), 0u)
      << "There should have been a segment info before flushing that would "
         "have cleared out all the samples.";

  const float duration_ms = static_cast<float>(total_duration_ms_);
  const float duration_seconds = duration_ms / 1000.0f;

  MuxerListener::MediaRanges empty_ranges;
  muxer_listener_->OnMediaEnd(empty_ranges, duration_seconds);

  return Status::OK;
}

Status WebVttTextOutputHandler::OnStreamInfo(const StreamInfo& info) {
  buffer_.reset(
      new WebVttFileBuffer(muxer_options_.transport_stream_timestamp_offset_ms,
                           ToString(info.codec_config())));
  muxer_listener_->OnMediaStart(muxer_options_, info, info.time_scale(),
                                MuxerListener::kContainerText);
  return Status::OK;
}

Status WebVttTextOutputHandler::OnSegmentInfo(const SegmentInfo& info) {
  total_duration_ms_ += info.duration;

  const std::string& segment_template = muxer_options_.segment_template;
  const uint32_t index = info.segment_index;
  const uint64_t start = info.start_timestamp;
  const uint64_t duration = info.duration;
  const uint32_t bandwidth = muxer_options_.bandwidth;

  const std::string filename =
      GetSegmentName(segment_template, start, index, bandwidth);

  // Write everything to the file before telling the manifest so that the
  // file will exist on disk.
  std::unique_ptr<File, FileCloser> file(File::Open(filename.c_str(), "w"));

  if (!file) {
    return Status(error::FILE_FAILURE, "Failed to open " + filename);
  }

  buffer_->WriteTo(file.get());
  buffer_->Reset();

  if (!file.release()->Close()) {
    return Status(error::FILE_FAILURE, "Failed to close " + filename);
  }

  // Update the manifest with our new file.
  const uint64_t size = File::GetFileSize(filename.c_str());
  muxer_listener_->OnNewSegment(filename, start, duration, size, 
		  info.segment_index);

  return Status::OK;
}

Status WebVttTextOutputHandler::OnCueEvent(const CueEvent& event) {
  double timestamp_seconds = event.time_in_seconds;
  double timestamp_ms = timestamp_seconds * kMillisecondsToSeconds;
  uint64_t timestamp = static_cast<uint64_t>(timestamp_ms);
  muxer_listener_->OnCueEvent(timestamp, event.cue_data);
  return Status::OK;
}

void WebVttTextOutputHandler::OnTextSample(const TextSample& sample) {
  // Skip empty samples. It is normal to see empty samples as earlier in the
  // pipeline we pad the stream to remove gaps.
  if (sample.payload().size()) {
    buffer_->Append(sample);
  }
}
}  // namespace media
}  // namespace shaka
