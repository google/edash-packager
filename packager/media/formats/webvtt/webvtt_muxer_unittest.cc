// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/file/file_test_util.h"
#include "packager/media/base/media_handler_test_base.h"
#include "packager/media/base/text_stream_info.h"
#include "packager/media/event/combined_muxer_listener.h"
#include "packager/media/event/mock_muxer_listener.h"
#include "packager/media/formats/webvtt/webvtt_muxer.h"
#include "packager/status_test_util.h"

namespace shaka {
namespace media {
namespace webvtt {

namespace {

using testing::_;

const size_t kInputCount = 1;
const size_t kOutputCount = 0;
const size_t kInputIndex = 0;
const size_t kStreamIndex = 0;

const bool kEncrypted = true;
const char* kNoId = "";

const uint64_t kMsTimeScale = 1000u;

const char* kSegmentedFileTemplate = "memory://output/template-$Number$.vtt";
const char* kSegmentedFileOutput1 = "memory://output/template-1.vtt";
const char* kSegmentedFileOutput2 = "memory://output/template-2.vtt";

const int64_t kSegmentNumber1 = 1;
const int64_t kSegmentNumber2 = 2;

const uint64_t kSegmentDuration = 10000;
const float kMillisecondsPerSecond = 1000.0f;
}  // namespace

class WebVttMuxerTest : public MediaHandlerTestBase {
 protected:
  void SetUp() {
    MuxerOptions muxer_options;
    muxer_options.segment_template = kSegmentedFileTemplate;

    // Create a mock muxer listener but save a reference to the mock so that we
    // can use it in the test.
    std::unique_ptr<MockMuxerListener> muxer_listener(new MockMuxerListener);
    muxer_listener_ = muxer_listener.get();

    out_ = std::make_shared<WebVttMuxer>(muxer_options);
    out_->SetMuxerListener(std::move(muxer_listener));

    ASSERT_OK(SetUpAndInitializeGraph(out_, kInputCount, kOutputCount));
  }

  MockMuxerListener* muxer_listener_ = nullptr;
  std::shared_ptr<WebVttMuxer> out_;
};

TEST_F(WebVttMuxerTest, WithNoSegmentAndWithNoSamples) {
  EXPECT_CALL(*muxer_listener_, OnNewSegment(_, _, _, _, _)).Times(0);

  {
    // No segments should  have be created as there were no samples.

    testing::InSequence s;
    EXPECT_CALL(*muxer_listener_, OnMediaStart(_, _, _, _));

    EXPECT_CALL(*muxer_listener_, OnMediaEndMock(_, _, _, _, _, _, _, _, _));
  }

  ASSERT_OK(Input(kInputIndex)
                ->Dispatch(StreamData::FromStreamInfo(
                    kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(Input(kInputIndex)->FlushAllDownstreams());
}

TEST_F(WebVttMuxerTest, WithOneSegmentAndWithOneSample) {
  const char* kExpectedOutput =
      "WEBVTT\n"
      "\n"
      "00:00:05.000 --> 00:00:06.000 align:center\n"
      "payload\n"
      "\n";

  const uint64_t kSegmentStart = 0;

  {
    testing::InSequence s;
    EXPECT_CALL(*muxer_listener_, OnMediaStart(_, _, _, _));
    EXPECT_CALL(*muxer_listener_,
                OnNewSegment(kSegmentedFileOutput1, kSegmentStart,
                             kSegmentDuration, _, _));

    const float kMediaDuration = 1 * kSegmentDuration / kMillisecondsPerSecond;
    EXPECT_CALL(*muxer_listener_,
                OnMediaEndMock(_, _, _, _, _, _, _, _, kMediaDuration));
  }

  ASSERT_OK(Input(kInputIndex)
                ->Dispatch(StreamData::FromStreamInfo(
                    kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromTextSample(
              kStreamIndex, GetTextSample(kNoId, 5000, 6000, "payload"))));
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromSegmentInfo(
              kStreamIndex, GetSegmentInfo(kSegmentStart, kSegmentDuration,
                                           !kEncrypted, kSegmentNumber1))));
  ASSERT_OK(Input(kInputIndex)->FlushAllDownstreams());

  ASSERT_FILE_STREQ(kSegmentedFileOutput1, kExpectedOutput);
}

TEST_F(WebVttMuxerTest, WithTwoSegmentAndWithOneSample) {
  const char* kExpectedOutput1 =
      "WEBVTT\n"
      "\n"
      "00:00:05.000 --> 00:00:06.000 align:center\n"
      "payload 1\n"
      "\n";

  const char* kExpectedOutput2 =
      "WEBVTT\n"
      "\n"
      "00:00:15.000 --> 00:00:16.000 align:center\n"
      "payload 2\n"
      "\n";

  const uint64_t kSegment1Start = 0;
  const uint64_t kSegment2Start = kSegmentDuration;

  {
    testing::InSequence s;
    EXPECT_CALL(*muxer_listener_, OnMediaStart(_, _, _, _));
    EXPECT_CALL(*muxer_listener_,
                OnNewSegment(kSegmentedFileOutput1, kSegment1Start,
                             kSegmentDuration, _, _));
    EXPECT_CALL(*muxer_listener_,
                OnNewSegment(kSegmentedFileOutput2, kSegment2Start,
                             kSegmentDuration, _, _));

    const float kMediaDuration = 2 * kSegmentDuration / kMillisecondsPerSecond;
    EXPECT_CALL(*muxer_listener_,
                OnMediaEndMock(_, _, _, _, _, _, _, _, kMediaDuration));
  }

  ASSERT_OK(Input(kInputIndex)
                ->Dispatch(StreamData::FromStreamInfo(
                    0, GetTextStreamInfo(kMsTimeScale))));

  // Segment One
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromTextSample(
              kStreamIndex, GetTextSample(kNoId, 5000, 6000, "payload 1"))));
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromSegmentInfo(
              kStreamIndex, GetSegmentInfo(kSegment1Start, kSegmentDuration,
                                           !kEncrypted, kSegmentNumber1))));
  // Segment Two
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromTextSample(
              kStreamIndex, GetTextSample(kNoId, 15000, 16000, "payload 2"))));
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromSegmentInfo(
              kStreamIndex, GetSegmentInfo(kSegment2Start, kSegmentDuration,
                                           !kEncrypted, kSegmentNumber2))));
  ASSERT_OK(Input(kInputIndex)->FlushAllDownstreams());

  ASSERT_FILE_STREQ(kSegmentedFileOutput1, kExpectedOutput1);
  ASSERT_FILE_STREQ(kSegmentedFileOutput2, kExpectedOutput2);
}

TEST_F(WebVttMuxerTest, WithAnEmptySegment) {
  const char* kExpectedOutput1 =
      "WEBVTT\n"
      "\n";

  const char* kExpectedOutput2 =
      "WEBVTT\n"
      "\n"
      "00:00:15.000 --> 00:00:16.000 align:center\n"
      "payload 2\n"
      "\n";

  const uint64_t kSegment1Start = 0;
  const uint64_t kSegment2Start = kSegmentDuration;

  {
    testing::InSequence s;
    EXPECT_CALL(*muxer_listener_, OnMediaStart(_, _, _, _));
    EXPECT_CALL(*muxer_listener_,
                OnNewSegment(kSegmentedFileOutput1, kSegment1Start,
                             kSegmentDuration, _, _));
    EXPECT_CALL(*muxer_listener_,
                OnNewSegment(kSegmentedFileOutput2, kSegment2Start,
                             kSegmentDuration, _, _));

    const float kMediaDuration = 2 * kSegmentDuration / kMillisecondsPerSecond;
    EXPECT_CALL(*muxer_listener_,
                OnMediaEndMock(_, _, _, _, _, _, _, _, kMediaDuration));
  }

  ASSERT_OK(Input(kInputIndex)
                ->Dispatch(StreamData::FromStreamInfo(
                    0, GetTextStreamInfo(kMsTimeScale))));
  // Segment One
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromSegmentInfo(
              kStreamIndex, GetSegmentInfo(kSegment1Start, kSegmentDuration,
                                           !kEncrypted, kSegmentNumber1))));
  // Segment Two
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromTextSample(
              kStreamIndex, GetTextSample(kNoId, 15000, 16000, "payload 2"))));
  ASSERT_OK(
      Input(kInputIndex)
          ->Dispatch(StreamData::FromSegmentInfo(
              kStreamIndex, GetSegmentInfo(kSegment2Start, kSegmentDuration,
                                           !kEncrypted, kSegmentNumber2))));
  ASSERT_OK(Input(kInputIndex)->FlushAllDownstreams());

  ASSERT_FILE_STREQ(kSegmentedFileOutput1, kExpectedOutput1);
  ASSERT_FILE_STREQ(kSegmentedFileOutput2, kExpectedOutput2);
}

}  // namespace webvtt
}  // namespace media
}  // namespace shaka
