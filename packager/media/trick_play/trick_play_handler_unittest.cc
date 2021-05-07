// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/trick_play/trick_play_handler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/media/base/audio_stream_info.h"
#include "packager/media/base/media_handler_test_base.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/status_test_util.h"

using ::testing::_;

namespace shaka {
namespace media {
namespace {
const size_t kInputCount = 1;
const size_t kOutputCount = 1;
const size_t kInputIndex = 0;
const size_t kOutputIndex = 0;
const size_t kStreamIndex = 0;
const int64_t kSegmentNumber = 1;

// This value does not matter as trick play does not use it, but it is needed
// to create the audio and video info.
const uint32_t kTimescale = 1000u;

const bool kKeyFrame = true;
}  // namespace

class TrickPlayHandlerTest : public MediaHandlerTestBase {
 protected:
  void SetUpAndInitializeGraph(uint32_t factor) {
    ASSERT_OK(MediaHandlerTestBase::SetUpAndInitializeGraph(
        std::make_shared<TrickPlayHandler>(factor), kInputCount, kOutputCount));
  }

  Status DispatchVideoInfo() {
    auto info = GetVideoStreamInfo(kTimescale);
    auto data = StreamData::FromStreamInfo(kStreamIndex, std::move(info));
    return Input(kInputIndex)->Dispatch(std::move(data));
  }

  Status DispatchSample(int64_t time, int64_t duration, bool keyframe) {
    auto sample = GetMediaSample(time, duration, keyframe);
    auto data = StreamData::FromMediaSample(kStreamIndex, std::move(sample));
    return Input(kInputIndex)->Dispatch(std::move(data));
  }

  Status DispatchSegment(int64_t start_time, int64_t duration) {
    const bool kSubSegment = true;

    auto info =
        GetSegmentInfo(start_time, duration, !kSubSegment, kSegmentNumber);
    auto data = StreamData::FromSegmentInfo(kStreamIndex, std::move(info));
    return Input(kInputIndex)->Dispatch(std::move(data));
  }

  Status Flush() { return Input(kInputIndex)->FlushAllDownstreams(); }
};

// This test makes sure that audio streams are rejected by trick play handlers.
TEST_F(TrickPlayHandlerTest, RejectsAudio) {
  const uint32_t kTrickPlayFactor = 1u;
  SetUpAndInitializeGraph(kTrickPlayFactor);

  auto info = GetAudioStreamInfo(kTimescale);
  auto data = StreamData::FromStreamInfo(kStreamIndex, std::move(info));
  Status status = Input(kInputIndex)->Dispatch(std::move(data));
  EXPECT_EQ(error::TRICK_PLAY_ERROR, status.error_code());
}

// This test makes sure that when the trick play handler is initialized using
// a non-main-stream track that only a sub set of information gets passed
// through. This checks the specific case where no media samples are sent.
TEST_F(TrickPlayHandlerTest, TrickTrackNoSamples) {
  // When there are no samples, play rate could not be determined and should be
  // set to 0.
  const int64_t kPlayRate = 0;
  const uint32_t kTrickPlayFactor = 1u;

  SetUpAndInitializeGraph(kTrickPlayFactor);

  {
    testing::InSequence s;
    EXPECT_CALL(*Output(kOutputIndex),
                OnProcess(IsVideoStream(_, kTrickPlayFactor, kPlayRate)));
    EXPECT_CALL(*Output(kOutputIndex), OnFlush(_));
  }

  ASSERT_OK(DispatchVideoInfo());
  ASSERT_OK(Flush());
}

// This test makes sure that when the trick play handler is initialized using
// a non-main-stream track that only a sub set of information gets passed
// through.
TEST_F(TrickPlayHandlerTest, TrickTrackWithSamplesOnlyGetsKeyFrames) {
  const uint32_t kTrickPlayFactor = 1u;

  const int64_t kFrameDuration = 100;
  const int64_t kFrame0 = 0;
  const int64_t kFrame1 = 100;
  const int64_t kFrame2 = 200;
  const int64_t kFrame3 = 300;
  const int64_t kFrame4 = 400;
  const int64_t kFrame5 = 500;
  const int64_t kFrame6 = 600;
  const int64_t kFrame7 = 700;
  const int64_t kFrame8 = 800;

  // Key frame every three frames.
  // Use every key frame in the trick play stream.
  // A single trick play frame has 3 times the duration of a normal frame.
  const int64_t kPlayRate = 3;
  const int64_t kTrickPlayDuration = kFrameDuration * 3;

  SetUpAndInitializeGraph(kTrickPlayFactor);

  // Only some samples we send to trick play should also be sent to our mock
  // handler.
  {
    testing::InSequence s;
    EXPECT_CALL(*Output(kOutputIndex),
                OnProcess(IsVideoStream(_, kTrickPlayFactor, kPlayRate)));
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame0, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame3, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame6, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(*Output(kOutputIndex), OnFlush(_));
  }

  ASSERT_OK(DispatchVideoInfo());

  // GOP 1
  ASSERT_OK(DispatchSample(kFrame0, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame1, kFrameDuration, !kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame2, kFrameDuration, !kKeyFrame));

  // GOP 2
  ASSERT_OK(DispatchSample(kFrame3, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame4, kFrameDuration, !kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame5, kFrameDuration, !kKeyFrame));

  // GOP 3
  ASSERT_OK(DispatchSample(kFrame6, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame7, kFrameDuration, !kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame8, kFrameDuration, !kKeyFrame));

  ASSERT_OK(Flush());
}

// This test makes sure that when the trick play handler is initialized using
// a non-main-stream track that only a sub set of information gets passed
// through.
TEST_F(TrickPlayHandlerTest, TrickTrackWithSamples) {
  const uint32_t kTrickPlayFactor = 2u;

  const int64_t kFrameDuration = 100;
  const int64_t kFrame0 = 0;
  const int64_t kFrame1 = 100;
  const int64_t kFrame2 = 200;
  const int64_t kFrame3 = 300;
  const int64_t kFrame4 = 400;
  const int64_t kFrame5 = 500;
  const int64_t kFrame6 = 600;
  const int64_t kFrame7 = 700;

  // Key frame every two frames.
  // Use every second key frame in the trick play stream.
  // A single trick play frame has 4 times the duration of a normal frame.
  const int64_t kPlayRate = 4;
  const int64_t kTrickPlayDuration = kFrameDuration * 4;

  SetUpAndInitializeGraph(kTrickPlayFactor);

  // Only some samples we send to trick play should also be sent to our mock
  // handler.
  {
    testing::InSequence s;
    EXPECT_CALL(*Output(kOutputIndex),
                OnProcess(IsVideoStream(_, kTrickPlayFactor, kPlayRate)));
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame0, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame4, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(*Output(kOutputIndex), OnFlush(_));
  }

  ASSERT_OK(DispatchVideoInfo());

  // GOP 1
  ASSERT_OK(DispatchSample(kFrame0, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame1, kFrameDuration, !kKeyFrame));

  // GOP 2
  ASSERT_OK(DispatchSample(kFrame2, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame3, kFrameDuration, !kKeyFrame));

  // GOP 3
  ASSERT_OK(DispatchSample(kFrame4, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame5, kFrameDuration, !kKeyFrame));

  // GOP 4
  ASSERT_OK(DispatchSample(kFrame6, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame7, kFrameDuration, !kKeyFrame));

  ASSERT_OK(Flush());
}

TEST_F(TrickPlayHandlerTest, TrickTrackWithSamplesAndSegments) {
  const uint32_t kTrickPlayFactor = 1u;

  const int64_t kFrameDuration = 100;
  const int64_t kFrame0 = 0;
  const int64_t kFrame1 = 100;
  const int64_t kFrame2 = 200;
  const int64_t kFrame3 = 300;
  const int64_t kFrame4 = 400;
  const int64_t kFrame5 = 500;
  const int64_t kFrame6 = 600;
  const int64_t kFrame7 = 700;

  const int64_t kSegmentDuration = 400;
  const int64_t kSegment0 = 0;
  const int64_t kSegment1 = 400;

  // Key frame every two frames.
  // Use every key frame in the trick play stream.
  // This means that each trick play frame covers two normal frames.
  const int64_t kPlayRate = 2;
  const int64_t kTrickPlayDuration = kFrameDuration * 2;

  SetUpAndInitializeGraph(kTrickPlayFactor);

  // Only some samples we send to trick play should also be sent to our mock
  // handler.
  {
    testing::InSequence s;
    EXPECT_CALL(*Output(kOutputIndex),
                OnProcess(IsVideoStream(_, kTrickPlayFactor, kPlayRate)));

    // Segment One
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame0, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame2, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(*Output(kOutputIndex),
                OnProcess(IsSegmentInfo(_, kSegment0, kSegmentDuration, _, _)));

    // Segment Two
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame4, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(
        *Output(kOutputIndex),
        OnProcess(IsMediaSample(_, kFrame6, kTrickPlayDuration, _, kKeyFrame)));
    EXPECT_CALL(*Output(kOutputIndex),
                OnProcess(IsSegmentInfo(_, kSegment1, kSegmentDuration, _, _)));

    EXPECT_CALL(*Output(kOutputIndex), OnFlush(_));
  }

  ASSERT_OK(DispatchVideoInfo());

  // GOP 1
  ASSERT_OK(DispatchSample(kFrame0, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame1, kFrameDuration, !kKeyFrame));

  // GOP 2
  ASSERT_OK(DispatchSample(kFrame2, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame3, kFrameDuration, !kKeyFrame));

  // Segment One
  ASSERT_OK(DispatchSegment(kSegment0, kSegmentDuration));

  // GOP 3
  ASSERT_OK(DispatchSample(kFrame4, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame5, kFrameDuration, !kKeyFrame));

  // GOP 4
  ASSERT_OK(DispatchSample(kFrame6, kFrameDuration, kKeyFrame));
  ASSERT_OK(DispatchSample(kFrame7, kFrameDuration, !kKeyFrame));

  // Segment Two
  ASSERT_OK(DispatchSegment(kSegment1, kSegmentDuration));

  ASSERT_OK(Flush());
}

}  // namespace media
}  // namespace shaka
