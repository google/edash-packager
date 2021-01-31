// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_HLS_BASE_SIMPLE_HLS_NOTIFIER_H_
#define PACKAGER_HLS_BASE_SIMPLE_HLS_NOTIFIER_H_

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "packager/base/macros.h"
#include "packager/base/synchronization/lock.h"
#include "packager/hls/base/hls_notifier.h"
#include "packager/hls/base/master_playlist.h"
#include "packager/hls/base/media_playlist.h"
#include "packager/hls/public/hls_params.h"

namespace shaka {
namespace hls {

/// For testing.
/// Creates MediaPlaylist. Mock this and return mock MediaPlaylist.
class MediaPlaylistFactory {
 public:
  virtual ~MediaPlaylistFactory();
  virtual std::unique_ptr<MediaPlaylist> Create(const HlsParams& hls_params,
                                                const std::string& file_name,
                                                const std::string& name,
                                                const std::string& group_id);
};

/// This is thread safe.
class SimpleHlsNotifier : public HlsNotifier {
 public:
  /// @param hls_params contains parameters for setting up the notifier.
  explicit SimpleHlsNotifier(const HlsParams& hls_params);
  ~SimpleHlsNotifier() override;

  /// @name HlsNotifier implemetation overrides.
  /// @{
  bool Init() override;
  bool NotifyNewStream(const MediaInfo& media_info,
                       const std::string& playlist_name,
                       const std::string& stream_name,
                       const std::string& group_id,
                       uint32_t* stream_id) override;
  bool NotifySampleDuration(uint32_t stream_id,
                            uint32_t sample_duration) override;
  bool NotifyNewSegment(uint32_t stream_id,
                        const std::string& segment_name,
                        uint64_t start_time,
                        uint64_t duration,
                        uint64_t start_byte_offset,
                        uint64_t size) override;
  bool NotifyKeyFrame(uint32_t stream_id,
                      uint64_t timestamp,
                      uint64_t start_byte_offset,
                      uint64_t size) override;
  bool NotifyCueEvent(uint32_t container_id, uint64_t timestamp) override;
  bool NotifyEncryptionUpdate(
      uint32_t stream_id,
      const std::vector<uint8_t>& key_id,
      const std::vector<uint8_t>& system_id,
      const std::vector<uint8_t>& iv,
      const std::vector<uint8_t>& protection_system_specific_data) override;
  bool Flush() override;
  /// }@

 private:
  friend class SimpleHlsNotifierTest;

  struct StreamEntry {
    std::unique_ptr<MediaPlaylist> media_playlist;
    MediaPlaylist::EncryptionMethod encryption_method;
  };

  std::string master_playlist_dir_;
  uint32_t target_duration_ = 0;

  std::unique_ptr<MediaPlaylistFactory> media_playlist_factory_;
  std::unique_ptr<MasterPlaylist> master_playlist_;

  // Maps to unique_ptr because StreamEntry also holds unique_ptr
  std::map<uint32_t, std::unique_ptr<StreamEntry>> stream_map_;
  std::list<MediaPlaylist*> media_playlists_;

  uint32_t sequence_number_ = 0;

  base::Lock lock_;

  // Reference time to allow computation of segments associated wall time
  base::Time reference_time_ = base::Time();

  DISALLOW_COPY_AND_ASSIGN(SimpleHlsNotifier);
};

}  // namespace hls
}  // namespace shaka

#endif  // PACKAGER_HLS_BASE_SIMPLE_HLS_NOTIFIER_H_
