// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/vod_media_info_dump_muxer_listener.h"

#include <google/protobuf/text_format.h>

#include <cmath>

#include "packager/base/logging.h"
#include "packager/file/file.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/event/muxer_listener_internal.h"
#include "packager/mpd/base/media_info.pb.h"

namespace shaka {
namespace media {

VodMediaInfoDumpMuxerListener::VodMediaInfoDumpMuxerListener(
    const std::string& output_file_path)
    : output_file_name_(output_file_path) {}

VodMediaInfoDumpMuxerListener::~VodMediaInfoDumpMuxerListener() {}

void VodMediaInfoDumpMuxerListener::OnEncryptionInfoReady(
    bool is_initial_encryption_info,
    FourCC protection_scheme,
    const std::vector<uint8_t>& default_key_id,
    const std::vector<uint8_t>& iv,
    const std::vector<ProtectionSystemSpecificInfo>& key_system_info) {
  LOG_IF(WARNING, !is_initial_encryption_info)
      << "Updating (non initial) encryption info is not supported by "
         "this module.";
  protection_scheme_ = protection_scheme;
  default_key_id_ = default_key_id;
  key_system_info_ = key_system_info;
  is_encrypted_ = true;
}

void VodMediaInfoDumpMuxerListener::OnMediaStart(
    const MuxerOptions& muxer_options,
    const StreamInfo& stream_info,
    uint32_t time_scale,
    ContainerType container_type) {
  DCHECK(muxer_options.segment_template.empty());
  media_info_.reset(new MediaInfo());
  if (!internal::GenerateMediaInfo(muxer_options,
                                   stream_info,
                                   time_scale,
                                   container_type,
                                   media_info_.get())) {
    LOG(ERROR) << "Failed to generate MediaInfo from input.";
    return;
  }

  if (is_encrypted_) {
    internal::SetContentProtectionFields(protection_scheme_, default_key_id_,
                                         key_system_info_, media_info_.get());
  }
}

void VodMediaInfoDumpMuxerListener::OnEncryptionStart() {}

void VodMediaInfoDumpMuxerListener::OnSampleDurationReady(
    uint32_t sample_duration) {
  // Assume one VideoInfo.
  if (media_info_->has_video_info()) {
    media_info_->mutable_video_info()->set_frame_duration(sample_duration);
  }
}

void VodMediaInfoDumpMuxerListener::OnMediaEnd(const MediaRanges& media_ranges,
                                               float duration_seconds) {
  DCHECK(media_info_);
  if (!internal::SetVodInformation(media_ranges, duration_seconds,
                                   media_info_.get())) {
    LOG(ERROR) << "Failed to generate VOD information from input.";
    return;
  }
  if (!media_info_->has_bandwidth())
    media_info_->set_bandwidth(max_bitrate_);
  WriteMediaInfoToFile(*media_info_, output_file_name_);
}

void VodMediaInfoDumpMuxerListener::OnNewSegment(const std::string& file_name,
                                                 int64_t start_time,
                                                 int64_t duration,
                                                 uint64_t segment_file_size,
                                                 int64_t segment_number) {
  const double segment_duration_seconds =
      static_cast<double>(duration) / media_info_->reference_time_scale();

  const int kBitsInByte = 8;
  const uint64_t bitrate =
      ceil(kBitsInByte * segment_file_size / segment_duration_seconds);
  max_bitrate_ = std::max(max_bitrate_, bitrate);
}

void VodMediaInfoDumpMuxerListener::OnKeyFrame(int64_t timestamp,
                                               uint64_t start_byte_offset,
                                               uint64_t size) {}

void VodMediaInfoDumpMuxerListener::OnCueEvent(int64_t timestamp,
                                               const std::string& cue_data) {
  NOTIMPLEMENTED();
}

// static
bool VodMediaInfoDumpMuxerListener::WriteMediaInfoToFile(
    const MediaInfo& media_info,
    const std::string& output_file_path) {
  std::string output_string;
  if (!google::protobuf::TextFormat::PrintToString(media_info,
                                                   &output_string)) {
    LOG(ERROR) << "Failed to serialize MediaInfo to string.";
    return false;
  }

  File* file = File::Open(output_file_path.c_str(), "w");
  if (!file) {
    LOG(ERROR) << "Failed to open " << output_file_path;
    return false;
  }
  if (file->Write(output_string.data(), output_string.size()) <= 0) {
    LOG(ERROR) << "Failed to write MediaInfo to file.";
    file->Close();
    return false;
  }
  if (!file->Close()) {
    LOG(ERROR) << "Failed to close " << output_file_path;
    return false;
  }
  return true;
}

}  // namespace media
}  // namespace shaka
