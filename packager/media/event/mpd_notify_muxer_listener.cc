// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/mpd_notify_muxer_listener.h"

#include <cmath>

#include "packager/base/logging.h"
#include "packager/media/base/audio_stream_info.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/event/muxer_listener_internal.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/mpd_notifier.h"

namespace shaka {
namespace media {

MpdNotifyMuxerListener::MpdNotifyMuxerListener(MpdNotifier* mpd_notifier)
    : mpd_notifier_(mpd_notifier), is_encrypted_(false) {
  DCHECK(mpd_notifier);
  DCHECK(mpd_notifier->dash_profile() == DashProfile::kOnDemand ||
         mpd_notifier->dash_profile() == DashProfile::kLive);
}

MpdNotifyMuxerListener::~MpdNotifyMuxerListener() {}

void MpdNotifyMuxerListener::OnEncryptionInfoReady(
    bool is_initial_encryption_info,
    FourCC protection_scheme,
    const std::vector<uint8_t>& key_id,
    const std::vector<uint8_t>& iv,
    const std::vector<ProtectionSystemSpecificInfo>& key_system_info) {
  if (is_initial_encryption_info) {
    LOG_IF(WARNING, is_encrypted_)
        << "Updating initial encryption information.";
    protection_scheme_ = protection_scheme;
    default_key_id_ = key_id;
    key_system_info_ = key_system_info;
    is_encrypted_ = true;
    return;
  }
  if (!notification_id_)
    return;
  DCHECK_EQ(protection_scheme, protection_scheme_);

  for (const ProtectionSystemSpecificInfo& info : key_system_info) {
    const std::string drm_uuid = internal::CreateUUIDString(info.system_id);
    bool updated = mpd_notifier_->NotifyEncryptionUpdate(
        notification_id_.value(), drm_uuid, key_id, info.psshs);
    LOG_IF(WARNING, !updated) << "Failed to update encryption info.";
  }
}

void MpdNotifyMuxerListener::OnEncryptionStart() {}

void MpdNotifyMuxerListener::OnMediaStart(
    const MuxerOptions& muxer_options,
    const StreamInfo& stream_info,
    uint32_t time_scale,
    ContainerType container_type) {
  std::unique_ptr<MediaInfo> media_info(new MediaInfo());
  if (!internal::GenerateMediaInfo(muxer_options,
                                   stream_info,
                                   time_scale,
                                   container_type,
                                   media_info.get())) {
    LOG(ERROR) << "Failed to generate MediaInfo from input.";
    return;
  }
  for (const std::string& accessibility : accessibilities_)
    media_info->add_dash_accessibilities(accessibility);
  if (roles_.empty() && stream_info.stream_type() == kStreamText) {
    // If there aren't any roles, default to "subtitle" since some apps may
    // require it to distinguish between subtitle/caption.
    media_info->add_dash_roles("subtitle");
  } else {
    for (const std::string& role : roles_)
      media_info->add_dash_roles(role);
  }

  if (is_encrypted_) {
    internal::SetContentProtectionFields(protection_scheme_, default_key_id_,
                                         key_system_info_, media_info.get());
    media_info->mutable_protected_content()->set_include_mspr_pro(mpd_notifier_->include_mspr_pro());
  }

  // The content may be splitted into multiple files, but their MediaInfo
  // should be compatible.
  if (media_info_ &&
      !internal::IsMediaInfoCompatible(*media_info, *media_info_)) {
    LOG(WARNING) << "Incompatible MediaInfo \n"
                 << media_info->ShortDebugString() << "\n vs \n"
                 << media_info_->ShortDebugString()
                 << "\nThe result manifest may not be playable.";
  }
  media_info_ = std::move(media_info);

  if (mpd_notifier_->dash_profile() == DashProfile::kLive) {
    if (!NotifyNewContainer())
      return;
    DCHECK(notification_id_);
  }
}

// Record the sample duration in the media info for VOD so that OnMediaEnd, all
// the information is in the media info.
void MpdNotifyMuxerListener::OnSampleDurationReady(
    uint32_t sample_duration) {
  if (mpd_notifier_->dash_profile() == DashProfile::kLive) {
    mpd_notifier_->NotifySampleDuration(notification_id_.value(),
                                        sample_duration);
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

void MpdNotifyMuxerListener::OnMediaEnd(const MediaRanges& media_ranges,
                                        float duration_seconds) {
  if (mpd_notifier_->dash_profile() == DashProfile::kLive) {
    DCHECK(event_info_.empty());
    // TODO(kqyang): Set mpd duration to |duration_seconds|, which is more
    // accurate than the duration coded in the original media header.
    if (mpd_notifier_->mpd_type() == MpdType::kStatic)
      mpd_notifier_->Flush();
    return;
  }

  DCHECK(media_info_);
  if (!internal::SetVodInformation(media_ranges, duration_seconds,
                                   media_info_.get())) {
    LOG(ERROR) << "Failed to generate VOD information from input.";
    return;
  }

  if (notification_id_) {
    mpd_notifier_->NotifyMediaInfoUpdate(notification_id_.value(),
                                         *media_info_);
  } else {
    if (!NotifyNewContainer())
      return;
    DCHECK(notification_id_);
  }
  // TODO(rkuroiwa): Use media_ranges.subsegment_ranges instead of caching the
  // subsegments.
  for (const auto& event_info : event_info_) {
    switch (event_info.type) {
      case EventInfoType::kSegment:
        mpd_notifier_->NotifyNewSegment(
            notification_id_.value(), event_info.segment_info.start_time,
            event_info.segment_info.duration,
            event_info.segment_info.segment_file_size,
            event_info.segment_info.segment_number);
        break;
      case EventInfoType::kKeyFrame:
        // NO-OP for DASH.
        break;
      case EventInfoType::kCue:
        mpd_notifier_->NotifyCueEvent(notification_id_.value(),
                                      event_info.cue_event_info.timestamp);
        break;
    }
  }
  event_info_.clear();
  mpd_notifier_->Flush();
}

void MpdNotifyMuxerListener::OnNewSegment(const std::string& file_name,
                                          int64_t start_time,
                                          int64_t duration,
                                          uint64_t segment_file_size,
                                          int64_t segment_number) {
  if (mpd_notifier_->dash_profile() == DashProfile::kLive) {
    mpd_notifier_->NotifyNewSegment(notification_id_.value(), start_time,
                                    duration, segment_file_size,
                                    segment_number);
    if (mpd_notifier_->mpd_type() == MpdType::kDynamic)
      mpd_notifier_->Flush();
  } else {
    EventInfo event_info;
    event_info.type = EventInfoType::kSegment;
    event_info.segment_info = {start_time, duration, segment_file_size,
                               segment_number};
    event_info_.push_back(event_info);
  }
}

void MpdNotifyMuxerListener::OnKeyFrame(int64_t timestamp,
                                        uint64_t start_byte_offset,
                                        uint64_t size) {
  // NO-OP for DASH.
}

void MpdNotifyMuxerListener::OnCueEvent(int64_t timestamp,
                                        const std::string& cue_data) {
  // Not using |cue_data| at this moment.
  if (mpd_notifier_->dash_profile() == DashProfile::kLive) {
    mpd_notifier_->NotifyCueEvent(notification_id_.value(), timestamp);
  } else {
    EventInfo event_info;
    event_info.type = EventInfoType::kCue;
    event_info.cue_event_info = {timestamp};
    event_info_.push_back(event_info);
  }
}

bool MpdNotifyMuxerListener::NotifyNewContainer() {
  uint32_t notification_id;
  if (!mpd_notifier_->NotifyNewContainer(*media_info_, &notification_id)) {
    LOG(ERROR) << "Failed to notify MpdNotifier.";
    return false;
  }
  notification_id_ = notification_id;
  return true;
}

}  // namespace media
}  // namespace shaka
