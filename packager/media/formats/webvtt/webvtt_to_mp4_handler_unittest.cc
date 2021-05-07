// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/media/base/media_handler_test_base.h"
#include "packager/media/formats/webvtt/webvtt_to_mp4_handler.h"
#include "packager/status_test_util.h"

using testing::_;
using testing::AllOf;
using testing::Not;

namespace shaka {
namespace media {
namespace {
const size_t kStreamIndex = 0;
const bool kSubSegment = true;
const bool kEncrypted = true;
const int64_t kSegmentNumber = 1;

const char* kId1 = "sample-id-1";
const char* kId2 = "sample-id-2";
const char* kId3 = "sample-id-3";

const char* kSimplePayload = "simple-payload-that-has-some-text";
const char* kEmptyPayload = "";
}  // namespace

MATCHER_P(MediaSampleContainsId, id, "") {
  auto& sample = arg->media_sample;

  if (!sample) {
    return false;
  }

  // Convert the sample to a string so that we can look for the id but also
  // so we can print the data if we need to look at it. Replace the
  // non-displayable characters with "." as they can cause problems.
  std::string s;
  for (size_t i = 0; i < sample->data_size(); i++) {
    char c = static_cast<char>(sample->data()[i]);
    s.push_back(isprint(c) ? c : '.');
  }

  *result_listener << s << " does not contain " << id;
  return s.find(id) != std::string::npos;
}

class WebVttToMp4HandlerTest : public MediaHandlerTestBase {
 protected:
  Status SetUpTestGraph() {
    const size_t kOneInput = 1;
    const size_t kOneOutput = 1;

    auto handler = std::make_shared<WebVttToMp4Handler>();
    return SetUpAndInitializeGraph(handler, kOneInput, kOneOutput);
  }

  FakeInputMediaHandler* In() {
    const size_t kInputIndex = 0;
    return Input(kInputIndex);
  }

  MockOutputMediaHandler* Out() {
    const size_t kOutputIndex = 0;
    return Output(kOutputIndex);
  }

  Status DispatchStream() {
    const uint64_t kMsTimeScale = 1000u;

    auto info = GetTextStreamInfo(kMsTimeScale);
    return In()->Dispatch(
        StreamData::FromStreamInfo(kStreamIndex, std::move(info)));
  }

  Status DispatchText(const std::string& id,
                      const std::string& payload,
                      int64_t start_time,
                      int64_t end_time) {
    auto sample = GetTextSample(id, start_time, end_time, payload);
    return In()->Dispatch(
        StreamData::FromTextSample(kStreamIndex, std::move(sample)));
  }

  Status DispatchSegment(int64_t start_time, int64_t end_time) {
    DCHECK_GT(end_time, start_time);

    const bool kIsSubSegment = true;
    int64_t duration = end_time - start_time;

    auto segment =
        GetSegmentInfo(start_time, duration, !kIsSubSegment, kSegmentNumber);
    return In()->Dispatch(
        StreamData::FromSegmentInfo(kStreamIndex, std::move(segment)));
  }

  Status Flush() { return In()->FlushAllDownstreams(); }
};

// Verify that samples with no payload are ignored and act as gaps.
//
// |[-- SEGMENT ------------------]|
// |         [--- EMPTY SAMPLE ---]|
// |[- GAP -]                      |
//
TEST_F(WebVttToMp4HandlerTest, IngoresEmptyPayloadSamples) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kGapStart = kSegmentStart;
  const int64_t kGapEnd = kGapStart + 200;

  const int64_t kSampleStart = kGapEnd;
  const int64_t kSampleEnd = kSegmentEnd;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Gap - The gap and sample should be combines into a new gap that spans
    // the while segment.
    EXPECT_CALL(*Out(),
                OnProcess(IsMediaSample(kStreamIndex, kSegmentStart,
                                        kSegmentDuration, !kEncrypted, _)));
    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  // Even if the sample has an id, it should still get ignored if it has no
  // payload.
  ASSERT_OK(DispatchText(kId1, kEmptyPayload, kSampleStart, kSampleEnd));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// Verify that when the stream starts at a non-zero value, the gap at the
// start will be filled.
//
// |[-- SEGMENT ------------]|
// |         [--- SAMPLE ---]|
// |[- GAP -]                |
//
TEST_F(WebVttToMp4HandlerTest, NonZeroStartTime) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kGapStart = kSegmentStart;
  const int64_t kGapEnd = kGapStart + 200;
  const int64_t kGapDuration = kGapEnd - kGapStart;

  const int64_t kSampleStart = kGapEnd;
  const int64_t kSampleEnd = kSegmentEnd;
  const int64_t kSampleDuration = kSampleEnd - kSampleStart;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Gap
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kGapStart,
                                              kGapDuration, !kEncrypted, _),
                                Not(MediaSampleContainsId(kId1)))));

    // Sample
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kSampleStart,
                                              kSampleDuration, !kEncrypted, _),
                                MediaSampleContainsId(kId1))));

    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSampleStart, kSampleEnd));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// Verify the cues are grouped correctly when the cues do not overlap at all.
// An empty cue should be inserted between the two as there is a gap.
//
// |[-- SEGMENT --------------------------]|
// |[-- SAMPLE --]           [-- SAMPLE --]|
// |              [-- GAP --]              |
//
TEST_F(WebVttToMp4HandlerTest, NoOverlap) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kSample1Start = kSegmentStart;
  const int64_t kSample1End = kSample1Start + 1000;
  const int64_t kSample1Duration = kSample1End - kSample1Start;

  const int64_t kSample2Start = kSegmentEnd - 1000;
  const int64_t kSample2End = kSegmentEnd;
  const int64_t kSample2Duration = kSample2End - kSample2Start;

  const int64_t kGapStart = kSample1End;
  const int64_t kGapEnd = kSample2Start;
  const int64_t kGapDuration = kGapEnd - kGapStart;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Sample 1
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kSample1Start,
                                              kSample1Duration, !kEncrypted, _),
                                MediaSampleContainsId(kId1),
                                Not(MediaSampleContainsId(kId2)))));

    // Gap
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kGapStart,
                                              kGapDuration, !kEncrypted, _),
                                Not(MediaSampleContainsId(kId1)),
                                Not(MediaSampleContainsId(kId2)))));

    // Sample 2
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kSample2Start,
                                              kSample2Duration, !kEncrypted, _),
                                Not(MediaSampleContainsId(kId1)),
                                MediaSampleContainsId(kId2))));

    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSample1Start, kSample1End));
  ASSERT_OK(DispatchText(kId2, kSimplePayload, kSample2Start, kSample2End));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// Verify the cues are grouped correctly when one cue overlaps another cue at
// one end.
//
// |[-- SEGMENT -----------------]|
// |[-- SAMPLE --------]          |
// |           [------- SAMPLE --]|
TEST_F(WebVttToMp4HandlerTest, Overlap) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kSample1Start = kSegmentStart;
  const int64_t kSample1End = kSegmentEnd - 3000;

  const int64_t kSample2Start = kSegmentStart + 3000;
  const int64_t kSample2End = kSegmentEnd;

  const int64_t kOnlySample1Start = kSample1Start;
  const int64_t kOnlySample1End = kSample2Start;
  const int64_t kOnlySample1Duration = kOnlySample1End - kOnlySample1Start;

  const int64_t kSample1AndSample2Start = kSample2Start;
  const int64_t kSample1AndSample2End = kSample1End;
  const int64_t kSample1AndSample2Duration =
      kSample1AndSample2End - kSample1AndSample2Start;

  const int64_t kOnlySample2Start = kSample1End;
  const int64_t kOnlySample2End = kSample2End;
  const int64_t kOnlySample2Duration = kOnlySample2End - kOnlySample2Start;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Sample 1
    EXPECT_CALL(*Out(), OnProcess(AllOf(
                            IsMediaSample(kStreamIndex, kOnlySample1Start,
                                          kOnlySample1Duration, !kEncrypted, _),
                            MediaSampleContainsId(kId1),
                            Not(MediaSampleContainsId(kId2)))));

    // Sample 1 and Sample 2
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(
                    IsMediaSample(kStreamIndex, kSample1AndSample2Start,
                                  kSample1AndSample2Duration, !kEncrypted, _),
                    MediaSampleContainsId(kId1), MediaSampleContainsId(kId2))));

    // Sample 2
    EXPECT_CALL(*Out(), OnProcess(AllOf(
                            IsMediaSample(kStreamIndex, kOnlySample2Start,
                                          kOnlySample2Duration, !kEncrypted, _),
                            Not(MediaSampleContainsId(kId1)),
                            MediaSampleContainsId(kId2))));

    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSample1Start, kSample1End));
  ASSERT_OK(DispatchText(kId2, kSimplePayload, kSample2Start, kSample2End));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// Verify the cues are grouped correctly when one cue starts before and ends
// after another cue.
//
// |[-- SEGMENT -----------------]|
// |[-- SAMPLE ------------------]|
// |      [------- SAMPLE --]     |
//
TEST_F(WebVttToMp4HandlerTest, Contains) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kSample1Start = kSegmentStart;
  const int64_t kSample1End = kSegmentEnd;

  const int64_t kSample2Start = kSegmentStart + 1000;
  const int64_t kSample2End = kSegmentEnd - 1000;

  const int64_t kBeforeSample2Start = kSample1Start;
  const int64_t kBeforeSample2End = kSample2Start;
  const int64_t kBeforeSample2Duration =
      kBeforeSample2End - kBeforeSample2Start;

  const int64_t kDuringSample2Start = kSample2Start;
  const int64_t kDuringSample2End = kSample2End;
  const int64_t kDuringSample2Duration =
      kDuringSample2End - kDuringSample2Start;

  const int64_t kAfterSample2Start = kSample2End;
  const int64_t kAfterSample2End = kSample1End;
  const int64_t kAfterSample2Duration = kAfterSample2End - kAfterSample2Start;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Sample 1
    EXPECT_CALL(
        *Out(),
        OnProcess(AllOf(IsMediaSample(kStreamIndex, kBeforeSample2Start,
                                      kBeforeSample2Duration, !kEncrypted, _),
                        MediaSampleContainsId(kId1),
                        Not(MediaSampleContainsId(kId2)))));

    // Sample 1 and Sample 2
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(
                    IsMediaSample(kStreamIndex, kDuringSample2Start,
                                  kDuringSample2Duration, !kEncrypted, _),
                    MediaSampleContainsId(kId1), MediaSampleContainsId(kId2))));

    // Sample 1 Again
    EXPECT_CALL(
        *Out(),
        OnProcess(AllOf(IsMediaSample(kStreamIndex, kAfterSample2Start,
                                      kAfterSample2Duration, !kEncrypted, _),
                        MediaSampleContainsId(kId1),
                        Not(MediaSampleContainsId(kId2)))));

    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSample1Start, kSample1End));
  ASSERT_OK(DispatchText(kId2, kSimplePayload, kSample2Start, kSample2End));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// Verify that when two cues are completely on top of each other, that there
// is no extra boxes sent out.
//
// |[-- SEGMENT -----------------]|
// |[-- SAMPLE ------------------]|
// |[-- SAMPLE ------------------]|
//
TEST_F(WebVttToMp4HandlerTest, ExactOverlap) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kSampleStart = kSegmentStart;
  const int64_t kSampleEnd = kSegmentEnd;
  const int64_t kSampleDuration = kSampleEnd - kSampleStart;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Both Samples
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kSampleStart,
                                              kSampleDuration, !kEncrypted, _),
                                MediaSampleContainsId(kId1),
                                MediaSampleContainsId(kId2))));

    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSampleStart, kSampleEnd));
  ASSERT_OK(DispatchText(kId2, kSimplePayload, kSampleStart, kSampleEnd));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// Verify that when two cues are completely on top of each other, that there
// is no extra boxes sent out.
//
// |[-- SEGMENT -----------------]|
// |[-- SAMPLE ------------------]|
// |[-- SAMPLE ------------]      |
// |[-- SAMPLE ------]            |
TEST_F(WebVttToMp4HandlerTest, OverlapStartWithStaggerEnd) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kSample1Start = kSegmentStart;
  const int64_t kSample1End = kSegmentEnd;

  const int64_t kSample2Start = kSegmentStart;
  const int64_t kSample2End = kSegmentEnd - 1000;

  const int64_t kSample3Start = kSegmentStart;
  const int64_t kSample3End = kSegmentEnd - 2000;

  const int64_t kThreeSamplesStart = kSegmentStart;
  const int64_t kThreeSamplesEnd = kSample3End;
  const int64_t kThreeSamplesDuration = kThreeSamplesEnd - kThreeSamplesStart;

  const int64_t kTwoSamplesStart = kSample3End;
  const int64_t kTwoSamplesEnd = kSample2End;
  const int64_t kTwoSamplesDuration = kTwoSamplesEnd - kTwoSamplesStart;

  const int64_t kOneSampleStart = kSample2End;
  const int64_t kOneSampleEnd = kSample1End;
  const int64_t kOneSampleDuration = kOneSampleEnd - kOneSampleStart;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Three Samples
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(
                    IsMediaSample(kStreamIndex, kThreeSamplesStart,
                                  kThreeSamplesDuration, !kEncrypted, _),
                    MediaSampleContainsId(kId1), MediaSampleContainsId(kId2),
                    MediaSampleContainsId(kId3))));

    // Two Samples
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(
                    IsMediaSample(kStreamIndex, kTwoSamplesStart,
                                  kTwoSamplesDuration, !kEncrypted, _),
                    MediaSampleContainsId(kId1), MediaSampleContainsId(kId2),
                    Not(MediaSampleContainsId(kId3)))));

    // One Sample
    EXPECT_CALL(*Out(), OnProcess(AllOf(
                            IsMediaSample(kStreamIndex, kOneSampleStart,
                                          kOneSampleDuration, !kEncrypted, _),
                            MediaSampleContainsId(kId1),
                            Not(MediaSampleContainsId(kId2)),
                            Not(MediaSampleContainsId(kId3)))));

    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSample1Start, kSample1End));
  ASSERT_OK(DispatchText(kId2, kSimplePayload, kSample2Start, kSample2End));
  ASSERT_OK(DispatchText(kId3, kSimplePayload, kSample3Start, kSample3End));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// Verify that when two cues are completely on top of each other, that there
// is no extra boxes sent out.
//
// |[-- SEGMENT -----------------]|
// |[-- SAMPLE ------------------]|
// |      [-- SAMPLE ------------]|
// |            [-- SAMPLE ------]|
TEST_F(WebVttToMp4HandlerTest, StaggerStartWithOverlapEnd) {
  const int64_t kSegmentStart = 0;
  const int64_t kSegmentEnd = 10000;
  const int64_t kSegmentDuration = kSegmentEnd - kSegmentStart;

  const int64_t kSample1Start = kSegmentStart;
  const int64_t kSample1End = kSegmentEnd;

  const int64_t kSample2Start = kSegmentStart + 1000;
  const int64_t kSample2End = kSegmentEnd;

  const int64_t kSample3Start = kSegmentStart + 2000;
  const int64_t kSample3End = kSegmentEnd;

  const int64_t kOneSampleStart = kSample1Start;
  const int64_t kOneSampleEnd = kSample2Start;
  const int64_t kOneSampleDuration = kOneSampleEnd - kOneSampleStart;

  const int64_t kTwoSamplesStart = kSample2Start;
  const int64_t kTwoSamplesEnd = kSample3Start;
  const int64_t kTwoSamplesDuration = kTwoSamplesEnd - kTwoSamplesStart;

  const int64_t kThreeSamplesStart = kSample3Start;
  const int64_t kThreeSamplesEnd = kSample3End;
  const int64_t kThreeSamplesDuration = kThreeSamplesEnd - kThreeSamplesStart;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // One Sample
    EXPECT_CALL(*Out(), OnProcess(AllOf(
                            IsMediaSample(kStreamIndex, kOneSampleStart,
                                          kOneSampleDuration, !kEncrypted, _),
                            MediaSampleContainsId(kId1),
                            Not(MediaSampleContainsId(kId2)),
                            Not(MediaSampleContainsId(kId3)))));

    // Two Samples
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(
                    IsMediaSample(kStreamIndex, kTwoSamplesStart,
                                  kTwoSamplesDuration, !kEncrypted, _),
                    MediaSampleContainsId(kId1), MediaSampleContainsId(kId2),
                    Not(MediaSampleContainsId(kId3)))));

    // Three Samples
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(
                    IsMediaSample(kStreamIndex, kThreeSamplesStart,
                                  kThreeSamplesDuration, !kEncrypted, _),
                    MediaSampleContainsId(kId1), MediaSampleContainsId(kId2),
                    MediaSampleContainsId(kId3))));

    // Segment
    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegmentStart,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSample1Start, kSample1End));
  ASSERT_OK(DispatchText(kId2, kSimplePayload, kSample2Start, kSample2End));
  ASSERT_OK(DispatchText(kId3, kSimplePayload, kSample3Start, kSample3End));
  ASSERT_OK(DispatchSegment(kSegmentStart, kSegmentEnd));
  ASSERT_OK(Flush());
}

// The text chunking handler will repeat text samples that cross over a segment
// boundary. We need to know that this handler will be okay with those repeated
// samples.
//
// |[------ SEGMENT ------]|[------ SEGMENT ------]|
// |        [--- SAMPLE ---|--------]              |
// |- GAP -]               |         [- GAP ------]|
TEST_F(WebVttToMp4HandlerTest, CrossSegmentSamples) {
  const int64_t kSegmentDuration = 10000;
  const int64_t kGapDuration = 1000;

  const int64_t kSegment1Start = 0;
  const int64_t kSegment1End = 10000;

  const int64_t kSegment2Start = 10000;
  const int64_t kSegment2End = 20000;

  const int64_t kGap1Start = 0;
  const int64_t kGap2Start = 19000;

  const int64_t kSampleStart = 1000;
  const int64_t kSampleEnd = 19000;

  const int64_t kSamplePart1Start = 1000;
  const int64_t kSamplePart1Duration = 9000;

  const int64_t kSamplePart2Start = 10000;
  const int64_t kSamplePart2Duration = 9000;

  ASSERT_OK(SetUpTestGraph());

  {
    testing::InSequence s;

    EXPECT_CALL(*Out(), OnProcess(IsStreamInfo(kStreamIndex, _, _, _)));

    // Gap, Sample, Segment
    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kGap1Start,
                                              kGapDuration, !kEncrypted, _),
                                Not(MediaSampleContainsId(kId1)))));

    EXPECT_CALL(*Out(), OnProcess(AllOf(
                            IsMediaSample(kStreamIndex, kSamplePart1Start,
                                          kSamplePart1Duration, !kEncrypted, _),
                            MediaSampleContainsId(kId1))));

    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegment1Start,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    // Sample, Gap, Segment
    EXPECT_CALL(*Out(), OnProcess(AllOf(
                            IsMediaSample(kStreamIndex, kSamplePart2Start,
                                          kSamplePart2Duration, !kEncrypted, _),
                            MediaSampleContainsId(kId1))));

    EXPECT_CALL(*Out(),
                OnProcess(AllOf(IsMediaSample(kStreamIndex, kGap2Start,
                                              kGapDuration, !kEncrypted, _),
                                Not(MediaSampleContainsId(kId1)))));

    EXPECT_CALL(*Out(), OnProcess(IsSegmentInfo(kStreamIndex, kSegment2Start,
                                                kSegmentDuration, !kSubSegment,
                                                !kEncrypted)));

    EXPECT_CALL(*Out(), OnFlush(kStreamIndex));
  }

  ASSERT_OK(DispatchStream());
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSampleStart, kSampleEnd));
  ASSERT_OK(DispatchSegment(kSegment1Start, kSegment1End));
  ASSERT_OK(DispatchText(kId1, kSimplePayload, kSampleStart, kSampleEnd));
  ASSERT_OK(DispatchSegment(kSegment2Start, kSegment2End));
  ASSERT_OK(Flush());
}
}  // namespace media
}  // namespace shaka
