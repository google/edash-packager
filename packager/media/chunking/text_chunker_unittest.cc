// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/media/base/media_handler_test_base.h"
#include "packager/media/chunking/text_chunker.h"
#include "packager/status_test_util.h"

using ::testing::_;

namespace shaka {
namespace media {

namespace {
const uint64_t kStreamIndex = 0;
const uint64_t kMsTimeScale = 1000u;

const size_t kInputs = 1;
const size_t kOutputs = 1;

const size_t kInput = 0;
const size_t kOutput = 0;

const bool kEncrypted = true;
const bool kSubSegment = true;

const uint64_t kTimescaleMs = 1000;

const int64_t kStartSegmentNumber = 1;

const char* kNoId = "";
const char* kNoPayload = "";
}  // namespace

class TextChunkerTest : public MediaHandlerTestBase {
 protected:
  Status Init(double segment_duration) {
    return SetUpAndInitializeGraph(
        std::make_shared<TextChunker>(segment_duration, kStartSegmentNumber),
        kInputs, kOutputs);
  }
};

// Verify that the chunker will use the first sample's start time as the start
// time for the first segment.
//
// Segment Duration = 100 MS
//
// TIME (ms):0     5     1     1     2     2     3
//                 0     0     5     0     5     0
//                       0     0     0     0     0
// SAMPLES  :               [-----A-----]
// SEGMENTS :            ^           ^           ^
//
TEST_F(TextChunkerTest, SegmentsStartAtFirstSample) {
  const double kSegmentDurationSec = 0.1;
  const int64_t kSegmentDurationMs = 100;
  const int64_t kSegment0Start = 100;
  const int64_t kSegment1Start = 200;

  const int64_t kSampleAStart = 120;
  const int64_t kSampleAEnd = 220;

  ASSERT_OK(Init(kSegmentDurationSec));

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput), OnProcess(IsStreamInfo(_, _, _, _)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, _, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsSegmentInfo(_, kSegment0Start, kSegmentDurationMs, _, _)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, _, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsSegmentInfo(_, kSegment1Start, kSegmentDurationMs, _, _)));
    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Verify that when a sample elapses a full segment, that it only appears
// in the one segment.
//
// Segment Duration = 100 MS
//
// TIME (ms):0     5     1
//                 0     0
//                       0
// SAMPLES  :[-----A-----]
// SEGMENTS :            ^
//
TEST_F(TextChunkerTest, SampleEndingOnSegmentStart) {
  const double kSegmentDurationSec = 0.1;
  const int64_t kSegmentDurationMs = 100;

  const int64_t kSegment0Start = 0;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 100;

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));
    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Verify that samples only appear in the correct segments when they only exist
// in one segment.
//
// Segment Duration = 100 MS
//
// TIME (ms):0     5     1     1     2
//                 0     0     5     0
//                       0     0     0
// SAMPLES  :[--A--]
//                       [--B--]
// SEGMENTS :            ^           ^
//
TEST_F(TextChunkerTest, CreatesSegmentsForSamples) {
  const double kSegmentDurationSec = 0.1;
  const int64_t kSegmentDurationMs = 100;

  const int64_t kSegment0Start = 0;
  const int64_t kSegment1Start = 100;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 50;

  const int64_t kSampleBStart = 100;
  const int64_t kSampleBEnd = 150;

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Segment One
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Two
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleBStart, kSampleBEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Verify that a segment will get outputted even if there are no samples
// overlapping with it.
//
// Segment Duration = 100 MS
//
// TIME (ms):0     5     1     1     2     2     3
//                 0     0     5     0     5     0
//                       0     0     0     0     0
// SAMPLES  :[--A--]
//                                   [--B--]
// SEGMENTS :            ^           ^           ^
//
TEST_F(TextChunkerTest, OutputsEmptySegments) {
  const double kSegmentDurationSec = 0.1;
  const int64_t kSegmentDurationMs = 100;

  const int64_t kSegment0Start = 0;
  const int64_t kSegment1Start = 100;
  const int64_t kSegment2Start = 200;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 50;

  const int64_t kSampleBStart = 200;
  const int64_t kSampleBEnd = 250;

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Segment One
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Two (empty segment)
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Three
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment2Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleBStart, kSampleBEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Verify that samples that overlap multiple samples get dispatch in all
// segments.
//
// Segment Duration = 100 MS
//
// TIME (ms):0     5     1     1
//                 0     0     5
//                       0     0
// SAMPLES  :[--------A--------]
// SEGMENTS :            ^
//
TEST_F(TextChunkerTest, SampleCrossesSegments) {
  const double kSegmentDurationSec = 0.1;
  const int64_t kSegmentDurationMs = 100;

  const int64_t kSegment0Start = 0;
  const int64_t kSegment1Start = 100;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 150;

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Segment One
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Two
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kTimescaleMs))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Verify that samples that overlap multiple samples get dispatch in all
// segments, even if different samples elapse different number of segments.
//
// Segment Duration = 100 MS
//
// TIME (ms):0     5     1     1     2     2     3
//                 0     0     5     0     5     0
//                       0     0     0     0     0
// SAMPLES  :[--------A--------]
//           [--------B--------]
//           [-----------------C-----------]
// SEGMENTS :            ^           ^           ^
//
TEST_F(TextChunkerTest, PreservesOrder) {
  const double kSegmentDurationSec = 0.1;
  const int64_t kSegmentDurationMs = 100;

  const int64_t kSegment0Start = 0;
  const int64_t kSegment1Start = 100;
  const int64_t kSegment2Start = 200;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 150;

  const int64_t kSampleBStart = 0;
  const int64_t kSampleBEnd = 150;

  const int64_t kSampleCStart = 0;
  const int64_t kSampleCEnd = 250;

  const char* kSampleAId = "sample 0";
  const char* kSampleBId = "sample 1";
  const char* kSampleCId = "sample 2";

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Segment One
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsTextSample(_, kSampleAId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsTextSample(_, kSampleBId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsTextSample(_, kSampleCId, kSampleCStart, kSampleCEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Two
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsTextSample(_, kSampleAId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsTextSample(_, kSampleBId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsTextSample(_, kSampleCId, kSampleCStart, kSampleCEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Two
    EXPECT_CALL(
        *Output(kOutput),
        OnProcess(IsTextSample(_, kSampleCId, kSampleCStart, kSampleCEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment2Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kSampleAId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kSampleBId, kSampleBStart, kSampleBEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kSampleCId, kSampleCStart, kSampleCEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Check that when samples overlap/contain other samples, that they still
// get outputted in the correct segments.
//
// Segment Duration = 50 MS
//
// TIME (ms):0     5     1     1     2     2
//                 0     0     5     0     5
//                       0     0     0     0
// SAMPLES  :[--------------A--------------]
//                    [-----B------]
// SEGMENTS :      ^     ^     ^     ^     ^
//
TEST_F(TextChunkerTest, NestedSamples) {
  const double kSegmentDurationSec = 0.05;
  const int64_t kSegmentDurationMs = 50;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 250;

  const int64_t kSampleBStart = 75;
  const int64_t kSampleBEnd = 175;

  const int64_t kSegment0Start = 0;
  const int64_t kSegment1Start = 50;
  const int64_t kSegment2Start = 100;
  const int64_t kSegment3Start = 150;
  const int64_t kSegment4Start = 200;

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Segment 0
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment 1
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment 2
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment2Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment 3
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment3Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment 4
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment4Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kMsTimeScale))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleBStart, kSampleBEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Make sure that a sample that extends multiple segments is dropped when
// it no longer overlaps with a later segment.
//
// Segment Duration = 100 MS
//
// TIME (ms):0     5     1     1     2     2     3
//                 0     0     5     0     5     0
//                       0     0     0     0     0
// SAMPLES  :[-----------A-----------]
//                                   [--B--]
// SEGMENTS :            ^           ^           ^
//
TEST_F(TextChunkerTest, SecondSampleStartsAfterMultiSegmentSampleEnds) {
  const double kSegmentDurationSec = 0.1;
  const int64_t kSegmentDurationMs = 100;

  const int64_t kSegment0Start = 0;
  const int64_t kSegment1Start = 100;
  const int64_t kSegment2Start = 200;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 200;

  const int64_t kSampleBStart = 200;
  const int64_t kSampleBEnd = 250;

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Segment One
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Two
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    // Segment Three
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleBStart, kSampleBEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment2Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));

    EXPECT_CALL(*Output(kOutput), OnFlush(kStreamIndex));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kTimescaleMs))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleBStart, kSampleBEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

// Check that segments will be injected when a cue event comes down the
// pipeline and that the segment duration will get reset after the cues
// are dispatched.
//
// Segment Duration = 300 MS
//
// TIME (ms):0     5     1     1     2     2     3     3     4     5
//                 0     0     5     0     5     0     5     5     0
//                       0     0     0     0     0     0     0     0
// SAMPLES  :[--------------A--------------]
// CUES     :            ^           ^
// SEGMENTS :            ^           ^                             ^
//
TEST_F(TextChunkerTest, SampleSpanningMultipleCues) {
  const double kSegmentDurationSec = 0.3;
  const int64_t kSegmentDurationMs = 300;

  const int64_t kSampleAStart = 0;
  const int64_t kSampleAEnd = 250;

  const double kC0 = 0.1;
  const double kC1 = 0.2;

  const int64_t kSegment0Start = 0;
  const int64_t kSegment1Start = 100;
  const int64_t kSegment2Start = 200;
  ;

  const double kSegment0StartLength = 100;
  const double kSegment1StartLength = 100;

  Init(kSegmentDurationSec);

  {
    testing::InSequence s;

    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Segment 0 and Cue 0
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment0Start,
                                        kSegment0StartLength, !kSubSegment,
                                        !kEncrypted)));
    EXPECT_CALL(*Output(kOutput), OnProcess(IsCueEvent(kStreamIndex, kC0)));

    // Segment 1 and Cue 1
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                        kSegment1StartLength, !kSubSegment,
                                        !kEncrypted)));
    EXPECT_CALL(*Output(kOutput), OnProcess(IsCueEvent(kStreamIndex, kC1)));

    // Segment 2
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsTextSample(_, kNoId, kSampleAStart, kSampleAEnd)));
    EXPECT_CALL(*Output(kOutput),
                OnProcess(IsSegmentInfo(kStreamIndex, kSegment2Start,
                                        kSegmentDurationMs, !kSubSegment,
                                        !kEncrypted)));
  }

  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetTextStreamInfo(kTimescaleMs))));
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromTextSample(
      kStreamIndex,
      GetTextSample(kNoId, kSampleAStart, kSampleAEnd, kNoPayload))));
  ASSERT_OK(Input(kInput)->Dispatch(
      StreamData::FromCueEvent(kStreamIndex, GetCueEvent(kC0))));
  ASSERT_OK(Input(kInput)->Dispatch(
      StreamData::FromCueEvent(kStreamIndex, GetCueEvent(kC1))));
  ASSERT_OK(Input(kInput)->FlushAllDownstreams());
}

}  // namespace media
}  // namespace shaka
