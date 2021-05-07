// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_EVENT_HLS_NOTIFY_MUXER_LISTENER_H_
#define PACKAGER_MEDIA_EVENT_HLS_NOTIFY_MUXER_LISTENER_H_

#include <memory>
#include <string>
#include <vector>

#include "packager/base/optional.h"
#include "packager/media/event/event_info.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/mpd/base/media_info.pb.h"

namespace shaka {

namespace hls {
class HlsNotifier;
}  // namespace hls

namespace media {

/// MuxerListener that uses HlsNotifier.
class HlsNotifyMuxerListener : public MuxerListener {
 public:
  /// @param playlist_name is the name of the playlist for the muxer's stream.
  /// @param iframes_only if true, indicates that it is for iframes-only
  ///        playlist.
  /// @param ext_x_media_name is the name of this playlist. This is the
  ///        value of the NAME attribute for EXT-X-MEDIA, it is not the same as
  ///        @a playlist_name. This may be empty for video.
  /// @param ext_x_media_group_id is the group ID for this playlist. This is the
  ///        value of GROUP-ID attribute for EXT-X-MEDIA. This may be empty for
  ///        video.
  /// @param characteristics is the characteristics for this playlist. This is
  ///        the value of CHARACTERISTICS attribute for EXT-X-MEDIA. This may be
  ///        empty.
  /// @param hls_notifier used by this listener. Ownership does not transfer.
  HlsNotifyMuxerListener(const std::string& playlist_name,
                         bool iframes_only,
                         const std::string& ext_x_media_name,
                         const std::string& ext_x_media_group_id,
                         const std::vector<std::string>& characteristics,
                         hls::HlsNotifier* hls_notifier);
  ~HlsNotifyMuxerListener() override;

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
                    uint64_t segment_file_size,
                    int64_t segment_number) override;
  void OnKeyFrame(int64_t timestamp, uint64_t start_byte_offset, uint64_t size);
  void OnCueEvent(int64_t timestamp, const std::string& cue_data) override;
  /// @}

 private:
  HlsNotifyMuxerListener(const HlsNotifyMuxerListener&) = delete;
  HlsNotifyMuxerListener& operator=(const HlsNotifyMuxerListener&) = delete;

  bool NotifyNewStream();

  const std::string playlist_name_;
  const bool iframes_only_;
  const std::string ext_x_media_name_;
  const std::string ext_x_media_group_id_;
  const std::vector<std::string> characteristics_;
  hls::HlsNotifier* const hls_notifier_;
  base::Optional<uint32_t> stream_id_;

  bool must_notify_encryption_start_ = false;
  // Cached encryption info before OnMediaStart() is called.
  std::vector<uint8_t> next_key_id_;
  std::vector<uint8_t> next_iv_;
  std::vector<ProtectionSystemSpecificInfo> next_key_system_infos_;
  FourCC protection_scheme_ = FOURCC_NULL;

  // MediaInfo passed to Notifier::OnNewStream(). Mainly for single segment
  // playlists.
  std::unique_ptr<MediaInfo> media_info_;
  // Even information for delayed function calls (NotifyNewSegment and
  // NotifyCueEvent) after NotifyNewStream is called in OnMediaEnd. Only needed
  // for on-demand as the functions are called immediately in live mode.
  std::vector<EventInfo> event_info_;
};

}  // namespace media
}  // namespace shaka
#endif  // PACKAGER_MEDIA_EVENT_HLS_NOTIFY_MUXER_LISTENER_H_
