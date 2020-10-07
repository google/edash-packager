// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/hls_notify_muxer_listener.h"

#include <memory>
#include "packager/base/logging.h"
#include "packager/hls/base/hls_notifier.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/event/muxer_listener_internal.h"

namespace shaka {
namespace media {

HlsNotifyMuxerListener::HlsNotifyMuxerListener(
    const std::string& playlist_name,
    bool iframes_only,
    const std::string& ext_x_media_name,
    const std::string& ext_x_media_group_id,
    const std::vector<std::string>& characteristics,
    hls::HlsNotifier* hls_notifier)
    : playlist_name_(playlist_name),
      iframes_only_(iframes_only),
      ext_x_media_name_(ext_x_media_name),
      ext_x_media_group_id_(ext_x_media_group_id),
      characteristics_(characteristics),
      hls_notifier_(hls_notifier) {
  DCHECK(hls_notifier);
}

HlsNotifyMuxerListener::~HlsNotifyMuxerListener() {}

// These methods work together to notify that the media is encrypted.
// If OnEncryptionInfoReady() is called before the media has been started, then
// the information is stored and handled when OnEncryptionStart() is called.
// If OnEncryptionStart() is called before the media has been started then
// OnMediaStart() is responsible for notifying that the segments are encrypted
// right away i.e. call OnEncryptionStart().
void HlsNotifyMuxerListener::OnEncryptionInfoReady(
    bool is_initial_encryption_info,
    FourCC protection_scheme,
    const std::vector<uint8_t>& key_id,
    const std::vector<uint8_t>& iv,
    const std::vector<ProtectionSystemSpecificInfo>& key_system_infos) {
  if (!stream_id_) {
    next_key_id_ = key_id;
    next_iv_ = iv;
    next_key_system_infos_ = key_system_infos;
    protection_scheme_ = protection_scheme;
    return;
  }
  for (const ProtectionSystemSpecificInfo& info : key_system_infos) {
    const bool result = hls_notifier_->NotifyEncryptionUpdate(
        stream_id_.value(), key_id, info.system_id, iv, info.psshs);
    LOG_IF(WARNING, !result) << "Failed to add encryption info.";
  }
}

void HlsNotifyMuxerListener::OnEncryptionStart() {
  if (!stream_id_) {
    must_notify_encryption_start_ = true;
    return;
  }
  if (next_key_id_.empty()) {
    DCHECK(next_iv_.empty());
    DCHECK(next_key_system_infos_.empty());
    return;
  }

  for (const ProtectionSystemSpecificInfo& info : next_key_system_infos_) {
    const bool result = hls_notifier_->NotifyEncryptionUpdate(
        stream_id_.value(), next_key_id_, info.system_id, next_iv_, info.psshs);
    LOG_IF(WARNING, !result) << "Failed to add encryption info";
  }
  next_key_id_.clear();
  next_iv_.clear();
  next_key_system_infos_.clear();
  must_notify_encryption_start_ = false;
}

void HlsNotifyMuxerListener::OnMediaStart(const MuxerOptions& muxer_options,
                                          const StreamInfo& stream_info,
                                          uint32_t time_scale,
                                          ContainerType container_type) {
  std::unique_ptr<MediaInfo> media_info(new MediaInfo);
  if (!internal::GenerateMediaInfo(muxer_options, stream_info, time_scale,
                                   container_type, media_info.get())) {
    LOG(ERROR) << "Failed to generate MediaInfo from input.";
    return;
  }
  if (!characteristics_.empty()) {
    for (const std::string& characteristic : characteristics_)
      media_info->add_hls_characteristics(characteristic);
  }
  if (protection_scheme_ != FOURCC_NULL) {
    internal::SetContentProtectionFields(protection_scheme_, next_key_id_,
                                         next_key_system_infos_,
                                         media_info.get());
  }

  // The content may be splitted into multiple files, but their MediaInfo
  // should be compatible.
  if (media_info_ &&
      !internal::IsMediaInfoCompatible(*media_info, *media_info_)) {
    LOG(WARNING) << "Incompatible MediaInfo " << media_info->ShortDebugString()
                 << " vs " << media_info_->ShortDebugString()
                 << ". The result manifest may not be playable.";
  }
  media_info_ = std::move(media_info);

  if (!media_info_->has_segment_template()) {
    return;
  }

  if (!NotifyNewStream())
    return;
  DCHECK(stream_id_);

  if (must_notify_encryption_start_) {
    OnEncryptionStart();
  }
}

void HlsNotifyMuxerListener::OnSampleDurationReady(uint32_t sample_duration) {
  if (stream_id_) {
    // This happens in live mode.
    hls_notifier_->NotifySampleDuration(stream_id_.value(), sample_duration);
    return;
  }

  if (!media_info_) {
    LOG(WARNING) << "Got sample duration " << sample_duration
                 << " but no media was specified.";
    return;
  }
  if (!media_info_->has_video_info()) {
    // If non video, don't worry about it (at the moment).
    return;
  }

  media_info_->mutable_video_info()->set_frame_duration(sample_duration);
}

void HlsNotifyMuxerListener::OnMediaEnd(const MediaRanges& media_ranges,
                                        float duration_seconds) {
  DCHECK(media_info_);
  // TODO(kqyang): Should we just Flush here to avoid calling Flush explicitly?
  // Don't flush the notifier here. Flushing here would write all the playlists
  // before all Media Playlists are read. Which could cause problems
  // setting the correct EXT-X-TARGETDURATION.
  if (media_info_->has_segment_template()) {
    return;
  }
  if (media_ranges.init_range) {
    shaka::Range* init_range = media_info_->mutable_init_range();
    init_range->set_begin(media_ranges.init_range.value().start);
    init_range->set_end(media_ranges.init_range.value().end);
  }
  if (media_ranges.index_range) {
    shaka::Range* index_range = media_info_->mutable_index_range();
    index_range->set_begin(media_ranges.index_range.value().start);
    index_range->set_end(media_ranges.index_range.value().end);
  }

  if (!stream_id_) {
    if (!NotifyNewStream())
      return;
    DCHECK(stream_id_);
  } else {
    // HLS is not interested in MediaInfo update.
  }

  // TODO(rkuroiwa); Keep track of which (sub)segments are encrypted so that the
  // notification is sent right before the enecrypted (sub)segments.
  if (must_notify_encryption_start_) {
    OnEncryptionStart();
  }

  if (!media_ranges.subsegment_ranges.empty()) {
    const std::vector<Range>& subsegment_ranges =
        media_ranges.subsegment_ranges;
    const size_t num_subsegments = subsegment_ranges.size();
    size_t subsegment_index = 0;
    for (const auto& event_info : event_info_) {
      switch (event_info.type) {
        case EventInfoType::kSegment:
          if (subsegment_index < num_subsegments) {
            const Range& range = subsegment_ranges[subsegment_index];
            hls_notifier_->NotifyNewSegment(
                stream_id_.value(), media_info_->media_file_name(),
                event_info.segment_info.start_time,
                event_info.segment_info.duration, range.start,
                range.end + 1 - range.start);
          }
          ++subsegment_index;
          break;
        case EventInfoType::kKeyFrame:
          if (subsegment_index < num_subsegments) {
            const uint64_t segment_start_offset =
                subsegment_ranges[subsegment_index].start;
            hls_notifier_->NotifyKeyFrame(
                stream_id_.value(), event_info.key_frame.timestamp,
                segment_start_offset +
                    event_info.key_frame.start_offset_in_segment,
                event_info.key_frame.size);
          }
          break;
        case EventInfoType::kCue:
          hls_notifier_->NotifyCueEvent(stream_id_.value(),
                                        event_info.cue_event_info.timestamp);
          break;
      }
    }
    if (subsegment_index != num_subsegments) {
      LOG(WARNING) << "Number of subsegment ranges (" << num_subsegments
                   << ") does not match the number of subsegments notified to "
                      "OnNewSegment() ("
                   << event_info_.size() << ").";
    }
  }
  event_info_.clear();
}

void HlsNotifyMuxerListener::OnNewSegment(const std::string& file_name,
                                          int64_t start_time,
                                          int64_t duration,
                                          uint64_t segment_file_size,
                                          int64_t segment_index) {
  if (!media_info_->has_segment_template()) {
    EventInfo event_info;
    event_info.type = EventInfoType::kSegment;
    event_info.segment_info = {start_time, duration, segment_file_size};
    event_info_.push_back(event_info);
  } else {
    // For multisegment, it always starts from the beginning of the file.
    const size_t kStartingByteOffset = 0u;
    const bool result = hls_notifier_->NotifyNewSegment(
        stream_id_.value(), file_name, start_time, duration,
        kStartingByteOffset, segment_file_size);
    LOG_IF(WARNING, !result) << "Failed to add new segment.";
  }
}

void HlsNotifyMuxerListener::OnKeyFrame(int64_t timestamp,
                                        uint64_t start_byte_offset,
                                        uint64_t size) {
  if (!iframes_only_)
    return;
  if (!media_info_->has_segment_template()) {
    EventInfo event_info;
    event_info.type = EventInfoType::kKeyFrame;
    event_info.key_frame = {timestamp, start_byte_offset, size};
    event_info_.push_back(event_info);
  } else {
    const bool result = hls_notifier_->NotifyKeyFrame(
        stream_id_.value(), timestamp, start_byte_offset, size);
    LOG_IF(WARNING, !result) << "Failed to add new segment.";
  }
}

void HlsNotifyMuxerListener::OnCueEvent(int64_t timestamp,
                                        const std::string& cue_data) {
  // Not using |cue_data| at this moment.
  if (!media_info_->has_segment_template()) {
    EventInfo event_info;
    event_info.type = EventInfoType::kCue;
    event_info.cue_event_info = {timestamp};
    event_info_.push_back(event_info);
  } else {
    hls_notifier_->NotifyCueEvent(stream_id_.value(), timestamp);
  }
}

bool HlsNotifyMuxerListener::NotifyNewStream() {
  DCHECK(media_info_);

  uint32_t stream_id;
  const bool result = hls_notifier_->NotifyNewStream(
      *media_info_, playlist_name_, ext_x_media_name_, ext_x_media_group_id_,
      &stream_id);
  if (!result) {
    LOG(WARNING) << "Failed to notify new stream for VOD.";
    return false;
  }
  stream_id_ = stream_id;
  return true;
}

}  // namespace media
}  // namespace shaka
