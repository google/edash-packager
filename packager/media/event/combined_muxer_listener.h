// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_EVENT_COMBINED_MUXER_LISTENER_H_
#define PACKAGER_MEDIA_EVENT_COMBINED_MUXER_LISTENER_H_

#include <memory>
#include <vector>

#include "packager/media/event/muxer_listener.h"

namespace shaka {
namespace media {

/// This class supports a group of MuxerListeners. All events are forwarded to
/// every individual MuxerListeners contained in this CombinedMuxerListener.
class CombinedMuxerListener : public MuxerListener {
 public:
  CombinedMuxerListener() = default;

  void AddListener(std::unique_ptr<MuxerListener> listener);

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
                    int64_t segment_index) override;
  void OnKeyFrame(int64_t timestamp, uint64_t start_byte_offset, uint64_t size);
  void OnCueEvent(int64_t timestamp, const std::string& cue_data) override;
  /// @}

 protected:
  /// Limit the number of children MuxerListeners. It can only be used to reduce
  /// the number of children.
  /// @num is the number to set to. It is a no-op if `num` is equal or greater
  ///      than the existing number of children MuxerListeners.
  void LimitNumOfMuxerListners(size_t num) {
    if (num < muxer_listeners_.size())
      muxer_listeners_.resize(num);
  }
  /// @return MuxerListener at the specified index or nullptr if the index is
  ///         out of range.
  MuxerListener* MuxerListenerAt(size_t index) {
    return (index < muxer_listeners_.size()) ? muxer_listeners_[index].get()
                                             : nullptr;
  }

 private:
  CombinedMuxerListener(const CombinedMuxerListener&) = delete;
  CombinedMuxerListener& operator=(const CombinedMuxerListener&) = delete;

  std::vector<std::unique_ptr<MuxerListener>> muxer_listeners_;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_EVENT_COMBINED_MUXER_LISTENER_H_
