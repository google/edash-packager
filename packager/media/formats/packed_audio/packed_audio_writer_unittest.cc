// Copyright 2018 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/packed_audio/packed_audio_writer.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/file/file_test_util.h"
#include "packager/media/base/media_handler_test_base.h"
#include "packager/media/event/mock_muxer_listener.h"
#include "packager/media/formats/packed_audio/packed_audio_segmenter.h"
#include "packager/status_test_util.h"

using ::testing::_;
using ::testing::AllOf;
using ::testing::Bool;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Invoke;
using ::testing::Ref;
using ::testing::Return;
using ::testing::WithParamInterface;

namespace shaka {
namespace media {
namespace {

const size_t kInputs = 1;
const size_t kOutputs = 0;
const size_t kInput = 0;
const size_t kStreamIndex = 0;

const uint32_t kZeroTransportStreamTimestampOffset = 0;
const uint32_t kTimescale = 9000;

// For single-segment mode.
const char kOutputFile[] = "memory://test.aac";
// For multi-segment mode.
const char kSegmentTemplate[] = "memory://test_$Number$.aac";
const char kSegment1Name[] = "memory://test_1.aac";
const char kSegment2Name[] = "memory://test_2.aac";
const int64_t kSegmentNumber1 = 1;
const int64_t kSegmentNumber2 = 2;

class MockPackedAudioSegmenter : public PackedAudioSegmenter {
 public:
  MockPackedAudioSegmenter()
      : PackedAudioSegmenter(kZeroTransportStreamTimestampOffset) {}

  MOCK_METHOD1(Initialize, Status(const StreamInfo& stream_info));
  MOCK_METHOD1(AddSample, Status(const MediaSample& sample));
  MOCK_METHOD0(FinalizeSegment, Status());
  MOCK_CONST_METHOD0(TimescaleScale, double());
};

}  // namespace

class PackedAudioWriterTest : public MediaHandlerTestBase,
                              public WithParamInterface<bool> {
 protected:
  void SetUp() override {
    MediaHandlerTestBase::SetUp();

    is_single_segment_mode_ = GetParam();

    if (is_single_segment_mode_)
      muxer_options_.output_file_name = kOutputFile;
    else
      muxer_options_.segment_template = kSegmentTemplate;

    auto packed_audio_writer =
        std::make_shared<PackedAudioWriter>(muxer_options_);

    std::unique_ptr<MockPackedAudioSegmenter> mock_segmenter(
        new MockPackedAudioSegmenter);
    mock_segmenter_ptr_ = mock_segmenter.get();
    packed_audio_writer->segmenter_ = std::move(mock_segmenter);

    std::unique_ptr<MockMuxerListener> mock_muxer_listener(
        new MockMuxerListener);
    mock_muxer_listener_ptr_ = mock_muxer_listener.get();
    packed_audio_writer->SetMuxerListener(std::move(mock_muxer_listener));

    ASSERT_OK(SetUpAndInitializeGraph(packed_audio_writer, kInputs, kOutputs));
  }

  MuxerOptions muxer_options_;
  MockPackedAudioSegmenter* mock_segmenter_ptr_;
  MockMuxerListener* mock_muxer_listener_ptr_;
  bool is_single_segment_mode_;
};

TEST_P(PackedAudioWriterTest, InitializeWithStreamInfo) {
  auto stream_info_data =
      StreamData::FromStreamInfo(kStreamIndex, GetAudioStreamInfo(kTimescale));
  EXPECT_CALL(*mock_muxer_listener_ptr_,
              OnMediaStart(_, Ref(*stream_info_data->stream_info),
                           kPackedAudioTimescale,
                           MuxerListener::kContainerPackedAudio));
  EXPECT_CALL(*mock_segmenter_ptr_,
              Initialize(Ref(*stream_info_data->stream_info)));
  ASSERT_OK(Input(kInput)->Dispatch(std::move(stream_info_data)));
}

TEST_P(PackedAudioWriterTest, Sample) {
  const int64_t kTimestamp = 12345;
  const int64_t kDuration = 100;
  const bool kKeyFrame = true;
  auto sample_stream_data = StreamData::FromMediaSample(
      kStreamIndex, GetMediaSample(kTimestamp, kDuration, kKeyFrame));

  EXPECT_CALL(*mock_segmenter_ptr_,
              AddSample(Ref(*sample_stream_data->media_sample)));
  ASSERT_OK(Input(kInput)->Dispatch(std::move(sample_stream_data)));
}

TEST_P(PackedAudioWriterTest, SubsegmentIgnored) {
  const int64_t kTimestamp = 12345;
  const int64_t kDuration = 100;
  const bool kSubsegment = true;
  auto subsegment_stream_data = StreamData::FromSegmentInfo(
      kStreamIndex,
      GetSegmentInfo(kTimestamp, kDuration, kSubsegment, kSegmentNumber1));

  EXPECT_CALL(*mock_muxer_listener_ptr_, OnNewSegment(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*mock_segmenter_ptr_, FinalizeSegment()).Times(0);
  ASSERT_OK(Input(kInput)->Dispatch(std::move(subsegment_stream_data)));
}

TEST_P(PackedAudioWriterTest, OneSegment) {
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetAudioStreamInfo(kTimescale))));

  const int64_t kTimestamp = 12345;
  const int64_t kDuration = 100;
  const bool kSubsegment = true;
  auto segment_stream_data = StreamData::FromSegmentInfo(
      kStreamIndex,
      GetSegmentInfo(kTimestamp, kDuration, !kSubsegment, kSegmentNumber1));

  const double kMockTimescaleScale = 10;
  const char kMockSegmentData[] = "hello segment 1";
  const size_t kSegmentDataSize = sizeof(kMockSegmentData) - 1;

  EXPECT_CALL(
      *mock_muxer_listener_ptr_,
      OnNewSegment(is_single_segment_mode_ ? kOutputFile : kSegment1Name,
                   kTimestamp * kMockTimescaleScale,
                   kDuration * kMockTimescaleScale, kSegmentDataSize,
                   kSegmentNumber1));

  EXPECT_CALL(*mock_segmenter_ptr_, TimescaleScale())
      .WillRepeatedly(Return(kMockTimescaleScale));
  EXPECT_CALL(*mock_segmenter_ptr_, FinalizeSegment())
      .WillOnce(Invoke([this, &kMockSegmentData]() {
        this->mock_segmenter_ptr_->segment_buffer()->AppendString(
            kMockSegmentData);
        return Status::OK;
      }));
  ASSERT_OK(Input(kInput)->Dispatch(std::move(segment_stream_data)));

  const bool kHasInitRange = true;
  const bool kHasIndexRange = true;
  const bool kHasSegmentRange = true;
  if (is_single_segment_mode_) {
    EXPECT_CALL(
        *mock_muxer_listener_ptr_,
        OnMediaEndMock(
            !kHasInitRange, 0, 0, !kHasIndexRange, 0, 0, kHasSegmentRange,
            ElementsAre(AllOf(Field(&Range::start, Eq(0u)),
                              Field(&Range::end, Eq(kSegmentDataSize - 1)))),
            kDuration * kMockTimescaleScale));
  } else {
    EXPECT_CALL(*mock_muxer_listener_ptr_,
                OnMediaEndMock(!kHasInitRange, 0, 0, !kHasIndexRange, 0, 0,
                               !kHasSegmentRange, ElementsAre(),
                               kDuration * kMockTimescaleScale));
  }
  ASSERT_OK(Input(kInput)->FlushDownstream(kStreamIndex));

  ASSERT_FILE_STREQ(is_single_segment_mode_ ? kOutputFile : kSegment1Name,
                    kMockSegmentData);
}

TEST_P(PackedAudioWriterTest, TwoSegments) {
  ASSERT_OK(Input(kInput)->Dispatch(StreamData::FromStreamInfo(
      kStreamIndex, GetAudioStreamInfo(kTimescale))));

  const int64_t kTimestamp = 12345;
  const int64_t kDuration = 100;
  const bool kSubsegment = true;
  auto segment1_stream_data = StreamData::FromSegmentInfo(
      kStreamIndex,
      GetSegmentInfo(kTimestamp, kDuration, !kSubsegment, kSegmentNumber1));
  auto segment2_stream_data = StreamData::FromSegmentInfo(
      kStreamIndex, GetSegmentInfo(kTimestamp + kDuration, kDuration,
                                   !kSubsegment, kSegmentNumber2));

  const double kMockTimescaleScale = 10;
  const char kMockSegment1Data[] = "hello segment 1";
  const char kMockSegment2Data[] = "hello segment 2";
  const size_t kSegment1DataSize = sizeof(kMockSegment1Data) - 1;
  const size_t kSegment2DataSize = sizeof(kMockSegment2Data) - 1;

  EXPECT_CALL(
      *mock_muxer_listener_ptr_,
      OnNewSegment(is_single_segment_mode_ ? kOutputFile : kSegment1Name,
                   kTimestamp * kMockTimescaleScale,
                   kDuration * kMockTimescaleScale,
                   sizeof(kMockSegment1Data) - 1, kSegmentNumber1));
  EXPECT_CALL(
      *mock_muxer_listener_ptr_,
      OnNewSegment(is_single_segment_mode_ ? kOutputFile : kSegment2Name,
                   (kTimestamp + kDuration) * kMockTimescaleScale,
                   kDuration * kMockTimescaleScale, kSegment2DataSize,
                   kSegmentNumber2));

  EXPECT_CALL(*mock_segmenter_ptr_, TimescaleScale())
      .WillRepeatedly(Return(kMockTimescaleScale));
  EXPECT_CALL(*mock_segmenter_ptr_, FinalizeSegment())
      .WillOnce(Invoke([this, &kMockSegment1Data]() {
        this->mock_segmenter_ptr_->segment_buffer()->AppendString(
            kMockSegment1Data);
        return Status::OK;
      }))
      .WillOnce(Invoke([this, &kMockSegment2Data]() {
        this->mock_segmenter_ptr_->segment_buffer()->AppendString(
            kMockSegment2Data);
        return Status::OK;
      }));
  ASSERT_OK(Input(kInput)->Dispatch(std::move(segment1_stream_data)));
  ASSERT_OK(Input(kInput)->Dispatch(std::move(segment2_stream_data)));

  if (is_single_segment_mode_) {
    EXPECT_CALL(
        *mock_muxer_listener_ptr_,
        OnMediaEndMock(
            _, _, _, _, _, _, _,
            ElementsAre(AllOf(Field(&Range::start, Eq(0u)),
                              Field(&Range::end, Eq(kSegment1DataSize - 1))),
                        AllOf(Field(&Range::start, Eq(kSegment1DataSize)),
                              Field(&Range::end, Eq(kSegment1DataSize +
                                                    kSegment2DataSize - 1)))),
            kDuration * 2 * kMockTimescaleScale));
  } else {
    EXPECT_CALL(*mock_muxer_listener_ptr_,
                OnMediaEndMock(_, _, _, _, _, _, _, _,
                               kDuration * 2 * kMockTimescaleScale));
  }
  ASSERT_OK(Input(kInput)->FlushDownstream(kStreamIndex));

  if (is_single_segment_mode_) {
    ASSERT_FILE_STREQ(kOutputFile, std::string(kMockSegment1Data) +
                                       std::string(kMockSegment2Data));
  } else {
    ASSERT_FILE_STREQ(kSegment1Name, kMockSegment1Data);
    ASSERT_FILE_STREQ(kSegment2Name, kMockSegment2Data);
  }
}

INSTANTIATE_TEST_CASE_P(SingleSegmentOrMultiSegment,
                        PackedAudioWriterTest,
                        Bool());

}  // namespace media
}  // namespace shaka
