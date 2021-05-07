// Copyright 2019 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/multi_codec_muxer_listener.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/media/base/muxer_options.h"
#include "packager/media/event/mock_muxer_listener.h"
#include "packager/media/event/muxer_listener_test_helper.h"

namespace shaka {
namespace media {

using ::testing::_;
using ::testing::Property;
using ::testing::StrEq;
using ::testing::StrictMock;

namespace {

const uint64_t kSegmentStartTime = 19283;
const uint64_t kSegmentDuration = 98028;
const uint64_t kSegmentSize = 756739;
const uint32_t kTimescale = 90000;
const int64_t kSegmentNumber = 10;

MuxerListener::ContainerType kContainer = MuxerListener::kContainerMpeg2ts;

}  // namespace

class MultiCodecMuxerListenerTest : public ::testing::Test {
 protected:
  MultiCodecMuxerListenerTest() {
    std::unique_ptr<StrictMock<MockMuxerListener>> listener_1(
        new StrictMock<MockMuxerListener>);
    listener_for_first_codec_ = listener_1.get();
    std::unique_ptr<StrictMock<MockMuxerListener>> listener_2(
        new StrictMock<MockMuxerListener>);
    listener_for_second_codec_ = listener_2.get();
    multi_codec_listener_.AddListener(std::move(listener_1));
    multi_codec_listener_.AddListener(std::move(listener_2));

    muxer_options_.segment_template = "$Number$.ts";

    VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
    video_stream_info_ = CreateVideoStreamInfo(video_params);
  }

  MultiCodecMuxerListener multi_codec_listener_;
  StrictMock<MockMuxerListener>* listener_for_first_codec_;
  StrictMock<MockMuxerListener>* listener_for_second_codec_;
  MuxerOptions muxer_options_;
  std::shared_ptr<StreamInfo> video_stream_info_;
};

TEST_F(MultiCodecMuxerListenerTest, OnMediaStartSingleCodec) {
  video_stream_info_->set_codec_string("codec_1");

  EXPECT_CALL(
      *listener_for_first_codec_,
      OnMediaStart(_, Property(&StreamInfo::codec_string, StrEq("codec_1")),
                   kTimescale, kContainer))
      .Times(1);

  multi_codec_listener_.OnMediaStart(muxer_options_, *video_stream_info_,
                                     kTimescale, kContainer);
}

TEST_F(MultiCodecMuxerListenerTest, OnNewSegmentAfterOnMediaStartSingleCodec) {
  video_stream_info_->set_codec_string("codec_1");

  EXPECT_CALL(*listener_for_first_codec_, OnMediaStart(_, _, _, _)).Times(1);

  multi_codec_listener_.OnMediaStart(muxer_options_, *video_stream_info_,
                                     kTimescale, kContainer);

  EXPECT_CALL(*listener_for_first_codec_,
              OnNewSegment(StrEq("new_segment_name10.ts"), kSegmentStartTime,
                           kSegmentDuration, kSegmentSize, kSegmentNumber));

  multi_codec_listener_.OnNewSegment("new_segment_name10.ts", kSegmentStartTime,
                                     kSegmentDuration, kSegmentSize,
                                     kSegmentNumber);
}

TEST_F(MultiCodecMuxerListenerTest, OnMediaStartTwoCodecs) {
  video_stream_info_->set_codec_string("codec_1;codec_2");

  EXPECT_CALL(
      *listener_for_first_codec_,
      OnMediaStart(_, Property(&StreamInfo::codec_string, StrEq("codec_1")),
                   kTimescale, kContainer))
      .Times(1);
  EXPECT_CALL(
      *listener_for_second_codec_,
      OnMediaStart(_, Property(&StreamInfo::codec_string, StrEq("codec_2")),
                   kTimescale, kContainer))
      .Times(1);

  multi_codec_listener_.OnMediaStart(muxer_options_, *video_stream_info_,
                                     kTimescale, kContainer);
}

TEST_F(MultiCodecMuxerListenerTest, OnNewSegmentAfterOnMediaStartTwoCodecs) {
  video_stream_info_->set_codec_string("codec_1;codec_2");

  EXPECT_CALL(*listener_for_first_codec_, OnMediaStart(_, _, _, _)).Times(1);
  EXPECT_CALL(*listener_for_second_codec_, OnMediaStart(_, _, _, _)).Times(1);

  multi_codec_listener_.OnMediaStart(muxer_options_, *video_stream_info_,
                                     kTimescale, kContainer);

  EXPECT_CALL(*listener_for_first_codec_,
              OnNewSegment(StrEq("new_segment_name10.ts"), kSegmentStartTime,
                           kSegmentDuration, kSegmentSize, kSegmentNumber));
  EXPECT_CALL(*listener_for_second_codec_,
              OnNewSegment(StrEq("new_segment_name10.ts"), kSegmentStartTime,
                           kSegmentDuration, kSegmentSize, kSegmentNumber));

  multi_codec_listener_.OnNewSegment("new_segment_name10.ts", kSegmentStartTime,
                                     kSegmentDuration, kSegmentSize,
                                     kSegmentNumber);
}

}  // namespace media
}  // namespace shaka
