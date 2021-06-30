// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Implementation of MuxerListener that deals with MpdNotifier.

#ifndef PACKAGER_MEDIA_EVENT_MPD_NOTIFY_MUXER_LISTENER_H_
#define PACKAGER_MEDIA_EVENT_MPD_NOTIFY_MUXER_LISTENER_H_

#include <memory>
#include <vector>

#include "packager/base/optional.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/event/event_info.h"
#include "packager/media/event/muxer_listener.h"

namespace shaka {

class MediaInfo;
class MpdNotifier;

namespace media {

class MpdNotifyMuxerListener : public MuxerListener {
 public:
  /// @param mpd_notifier must be initialized, i.e mpd_notifier->Init() must be
  ///        called.
  explicit MpdNotifyMuxerListener(MpdNotifier* mpd_notifier);
  ~MpdNotifyMuxerListener() override;

  /// @name MuxerListener implementation overrides.
  /// @{
  void OnEncryptionInfoReady(bool is_initial_encryption_info,
                             FourCC protection_scheme,
                             const std::vector<uint8_t>& key_id,
                             const std::vector<uint8_t>& iv,
                             const std::vector<ProtectionSystemSpecificInfo>&
                                 key_system_info) override;
  void OnEncryptionStart() override;
  void OnMediaStart(const MuxerOptions& muxer_options,
                    const StreamInfo& stream_info,
                    uint32_t time_scale,
                    ContainerType container_type) override;
  void OnSampleDurationReady(uint32_t sample_duration) override;
  void OnMediaEnd(const MediaRanges& media_ranges,
                  float duration_seconds) override;
  void OnNewSegment(const std::string& file_name,
                    int64_t start_time,
                    int64_t duration,
                    uint64_t segment_file_size) override;
  void OnKeyFrame(int64_t timestamp, uint64_t start_byte_offset, uint64_t size);
  void OnCueEvent(int64_t timestamp, const std::string& cue_data) override;
  /// @}

  void set_accessibilities(const std::vector<std::string>& accessiblities) {
    accessibilities_ = accessiblities;
  }

  void set_roles(const std::vector<std::string>& roles) { roles_ = roles; }

  void set_dash_label(std::string label) { dash_label_ = label; }

 private:
  MpdNotifyMuxerListener(const MpdNotifyMuxerListener&) = delete;
  MpdNotifyMuxerListener& operator=(const MpdNotifyMuxerListener&) = delete;

  bool NotifyNewContainer();

  MpdNotifier* const mpd_notifier_ = nullptr;
  base::Optional<uint32_t> notification_id_;
  std::unique_ptr<MediaInfo> media_info_;

  std::vector<std::string> accessibilities_;
  std::vector<std::string> roles_;
  std::string dash_label_;

  bool is_encrypted_ = false;
  // Storage for values passed to OnEncryptionInfoReady().
  FourCC protection_scheme_ = FOURCC_NULL;
  std::vector<uint8_t> default_key_id_;
  std::vector<ProtectionSystemSpecificInfo> key_system_info_;

  // Saves all the Subsegment and CueEvent information for VOD. This should be
  // used to call NotifyNewSegment() and NotifyCueEvent after
  // NotifyNewContainer() is called (in OnMediaEnd). This is not used for live
  // because NotifyNewSegment() is called immediately in OnNewSegment(), and
  // NotifyCueEvent is called immediately in OnCueEvent.
  std::vector<EventInfo> event_info_;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_EVENT_MPD_NOTIFY_MUXER_LISTENER_H_
