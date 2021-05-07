// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_EVENT_MOCK_MUXER_LISTENER_H_
#define PACKAGER_MEDIA_EVENT_MOCK_MUXER_LISTENER_H_

#include <gmock/gmock.h>

#include "packager/media/base/muxer_options.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/event/muxer_listener.h"

namespace shaka {
namespace media {

class MockMuxerListener : public MuxerListener {
 public:
  MockMuxerListener();
  ~MockMuxerListener() override;

  MOCK_METHOD5(
      OnEncryptionInfoReady,
      void(bool is_initial_encryption_info,
           FourCC protection_scheme,
           const std::vector<uint8_t>& key_id,
           const std::vector<uint8_t>& iv,
           const std::vector<ProtectionSystemSpecificInfo>& key_system_info));

  MOCK_METHOD0(OnEncryptionStart, void());

  MOCK_METHOD4(OnMediaStart,
               void(const MuxerOptions& muxer_options,
                    const StreamInfo& stream_info,
                    uint32_t time_scale,
                    ContainerType container_type));

  MOCK_METHOD1(OnSampleDurationReady, void(uint32_t sample_duration));

  MOCK_METHOD9(OnMediaEndMock,
               void(bool has_init_range,
                    uint64_t init_range_start,
                    uint64_t init_range_end,
                    bool has_index_range,
                    uint64_t index_range_start,
                    uint64_t index_range_end,
                    bool has_subsegment_ranges,
                    const std::vector<Range> subsegment_ranges,
                    float duration_seconds));

  // Windows 32 bit cannot mock MediaRanges because it has Optionals that use
  // memory alignment of 8 bytes. The compiler fails if it is mocked.
  void OnMediaEnd(const MediaRanges& range,
                  float duration_seconds) override;

  MOCK_METHOD5(OnNewSegment,
               void(const std::string& segment_name,
                    int64_t start_time,
                    int64_t duration,
                    uint64_t segment_file_size,
                    int64_t segment_number));

  MOCK_METHOD3(OnKeyFrame,
               void(int64_t timestamp,
                    uint64_t start_byte_offset,
                    uint64_t size));

  MOCK_METHOD2(OnCueEvent,
               void(int64_t timestamp, const std::string& cue_data));
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_EVENT_MOCK_MUXER_LISTENER_H_
