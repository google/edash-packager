// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/combined_muxer_listener.h"

namespace shaka {
namespace media {

void CombinedMuxerListener::AddListener(
    std::unique_ptr<MuxerListener> listener) {
  muxer_listeners_.push_back(std::move(listener));
}

void CombinedMuxerListener::OnEncryptionInfoReady(
    bool is_initial_encryption_info,
    FourCC protection_scheme,
    const std::vector<uint8_t>& key_id,
    const std::vector<uint8_t>& iv,
    const std::vector<ProtectionSystemSpecificInfo>& key_system_info) {
  for (auto& listener : muxer_listeners_) {
    listener->OnEncryptionInfoReady(is_initial_encryption_info,
                                    protection_scheme, key_id, iv,
                                    key_system_info);
  }
}

void CombinedMuxerListener::OnEncryptionStart() {
  for (auto& listener : muxer_listeners_) {
    listener->OnEncryptionStart();
  }
}

void CombinedMuxerListener::OnMediaStart(const MuxerOptions& muxer_options,
                                         const StreamInfo& stream_info,
                                         uint32_t time_scale,
                                         ContainerType container_type) {
  for (auto& listener : muxer_listeners_) {
    listener->OnMediaStart(muxer_options, stream_info, time_scale,
                           container_type);
  }
}

void CombinedMuxerListener::OnSampleDurationReady(uint32_t sample_duration) {
  for (auto& listener : muxer_listeners_) {
    listener->OnSampleDurationReady(sample_duration);
  }
}

void CombinedMuxerListener::OnMediaEnd(const MediaRanges& media_ranges,
                                       float duration_seconds) {
  for (auto& listener : muxer_listeners_) {
    listener->OnMediaEnd(media_ranges, duration_seconds);
  }
}

void CombinedMuxerListener::OnNewSegment(const std::string& file_name,
                                         int64_t start_time,
                                         int64_t duration,
                                         uint64_t segment_file_size,
                                         int64_t segment_index) {
  for (auto& listener : muxer_listeners_) {
    listener->OnNewSegment(file_name, start_time, duration, segment_file_size,
		           segment_index);
  }
}

void CombinedMuxerListener::OnKeyFrame(int64_t timestamp,
                                       uint64_t start_byte_offset,
                                       uint64_t size) {
  for (auto& listener : muxer_listeners_) {
    listener->OnKeyFrame(timestamp, start_byte_offset, size);
  }
}

void CombinedMuxerListener::OnCueEvent(int64_t timestamp,
                                       const std::string& cue_data) {
  for (auto& listener : muxer_listeners_) {
    listener->OnCueEvent(timestamp, cue_data);
  }
}

}  // namespace media
}  // namespace shaka
