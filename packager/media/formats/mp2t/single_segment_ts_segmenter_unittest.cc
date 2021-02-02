// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/media/base/audio_stream_info.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/event/mock_muxer_listener.h"
#include "packager/media/formats/mp2t/pes_packet.h"
#include "packager/media/formats/mp2t/program_map_table_writer.h"
#include "packager/media/formats/mp2t/ts_segmenter.h"
#include "packager/media/formats/mp2t/single_segment_ts_segmenter.h"
#include "packager/status_test_util.h"
#include "packager/media/base/macros.h"

namespace shaka {
namespace media {
namespace mp2t {

using ::testing::InSequence;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::StrEq;
using ::testing::_;

namespace {

// All data here is bogus. They are used to create VideoStreamInfo but the
// actual values don't matter at all.
const bool kIsKeyFrame = true;
const Codec kH264Codec = Codec::kCodecH264;
const uint8_t kExtraData[] = {
    0x00,
};
const int kTrackId = 0;
const uint32_t kZeroTransportStreamTimestampOffset = 0;
const uint32_t kTimeScale = 90000;
const uint64_t kDuration = 180000;
const char kCodecString[] = "avc1";
const char kLanguage[] = "eng";
const uint32_t kWidth = 1280;
const uint32_t kHeight = 720;
const uint32_t kPixelWidth = 1;
const uint32_t kPixelHeight = 1;
const uint8_t kTransferCharacteristics = 0;
const uint16_t kTrickPlayFactor = 1;
const uint8_t kNaluLengthSize = 1;
const bool kIsEncrypted = false;

const uint8_t kAnyData[] = {
    0x01, 0x0F, 0x3C,
};

class MockPesPacketGenerator : public PesPacketGenerator {
 public:
  MockPesPacketGenerator()
      : PesPacketGenerator(kZeroTransportStreamTimestampOffset) {}

  MOCK_METHOD1(Initialize, bool(const StreamInfo& info));
  MOCK_METHOD1(PushSample, bool(const MediaSample& sample));

  MOCK_METHOD0(NumberOfReadyPesPackets, size_t());

  // Hack found at the URL below for mocking methods that return
  // std::unique_ptr.
  // https://groups.google.com/a/chromium.org/forum/#!topic/chromium-dev/01sDxsJ1OYw
  MOCK_METHOD0(GetNextPesPacketMock, PesPacket*());
  std::unique_ptr<PesPacket> GetNextPesPacket() override {
    return std::unique_ptr<PesPacket>(GetNextPesPacketMock());
  }

  MOCK_METHOD0(Flush, bool());
};

class MockTsWriter : public TsWriter {
 public:
  MockTsWriter()
      : TsWriter(std::unique_ptr<ProgramMapTableWriter>(
            // Create a bogus pmt writer, which we don't really care.
            new VideoProgramMapTableWriter(kUnknownCodec))) {}

  MOCK_METHOD1(NewSegment, bool(BufferWriter* buffer_writer));
  MOCK_METHOD0(SignalEncrypted, void());

  // Similar to the hack above but takes a std::unique_ptr.
  MOCK_METHOD2(AddPesPacketMock, bool(PesPacket* pes_packet,
			  BufferWriter* buffer_writer));
  bool AddPesPacket(std::unique_ptr<PesPacket> pes_packet,
		  BufferWriter* buffer_writer) override {
     buffer_writer->AppendArray(kAnyData, arraysize(kAnyData));
    // No need to keep the pes packet around for the current tests.
    return AddPesPacketMock(pes_packet.get(), buffer_writer);
  }
};

}  // namespace

class SingleSegmentTsSegmenterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ts_writer_.reset(new MockTsWriter());
    mock_pes_packet_generator_.reset(new MockPesPacketGenerator());
  }

  std::unique_ptr<MockTsWriter> mock_ts_writer_;
  std::unique_ptr<MockPesPacketGenerator> mock_pes_packet_generator_;
};

TEST_F(SingleSegmentTsSegmenterTest, Initialize) {
  std::shared_ptr<VideoStreamInfo> stream_info(new VideoStreamInfo(
      kTrackId, kTimeScale, kDuration, kH264Codec,
      H26xStreamFormat::kAnnexbByteStream, kCodecString, kExtraData,
      arraysize(kExtraData), kWidth, kHeight, kPixelWidth, kPixelHeight,
      kTransferCharacteristics, kTrickPlayFactor, kNaluLengthSize, kLanguage,
      kIsEncrypted));
  MuxerOptions options;
  options.output_file_name = "file.ts";
  SingleSegmentTsSegmenter segmenter(options, nullptr);

  EXPECT_CALL(*mock_pes_packet_generator_, Initialize(_))
      .WillOnce(Return(true));

  segmenter.InjectPesPacketGeneratorForTesting(
      std::move(mock_pes_packet_generator_));

  EXPECT_OK(segmenter.Initialize(*stream_info));
}

TEST_F(SingleSegmentTsSegmenterTest, AddSample) {
  std::shared_ptr<VideoStreamInfo> stream_info(new VideoStreamInfo(
      kTrackId, kTimeScale, kDuration, kH264Codec,
      H26xStreamFormat::kAnnexbByteStream, kCodecString, kExtraData,
      arraysize(kExtraData), kWidth, kHeight, kPixelWidth, kPixelHeight,
      kTransferCharacteristics, kTrickPlayFactor, kNaluLengthSize, kLanguage,
      kIsEncrypted));
  MuxerOptions options;
  options.output_file_name = "file.ts";
  SingleSegmentTsSegmenter segmenter(options, nullptr);

  EXPECT_CALL(*mock_pes_packet_generator_, Initialize(_))
      .WillOnce(Return(true));

  std::shared_ptr<MediaSample> sample =
      MediaSample::CopyFrom(kAnyData, arraysize(kAnyData), kIsKeyFrame);

  Sequence writer_sequence;
  EXPECT_CALL(*mock_ts_writer_, NewSegment(_))
      .InSequence(writer_sequence)
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_pes_packet_generator_, PushSample(_))
      .WillOnce(Return(true));

  Sequence ready_pes_sequence;
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(1u));
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(0u));

  EXPECT_CALL(*mock_ts_writer_, AddPesPacketMock(_, _))
      .WillOnce(Return(true));

  // The pointer is released inside the segmenter.
  EXPECT_CALL(*mock_pes_packet_generator_, GetNextPesPacketMock())
      .WillOnce(Return(new PesPacket()));

  segmenter.InjectPesPacketGeneratorForTesting(
      std::move(mock_pes_packet_generator_));

  EXPECT_OK(segmenter.Initialize(*stream_info));
  segmenter.InjectTsWriterForTesting(std::move(mock_ts_writer_));
  EXPECT_OK(segmenter.AddSample(*sample));
}

// This will add one sample then finalize segment then add another sample.
TEST_F(SingleSegmentTsSegmenterTest, PassedSegmentDuration) {
  // Use something significantly smaller than 90000 to check that the scaling is
  // done correctly in the segmenter.
  const uint32_t kInputTimescale = 1001;
  std::shared_ptr<VideoStreamInfo> stream_info(new VideoStreamInfo(
      kTrackId, kInputTimescale, kDuration, kH264Codec,
      H26xStreamFormat::kAnnexbByteStream, kCodecString, kExtraData,
      arraysize(kExtraData), kWidth, kHeight, kPixelWidth, kPixelHeight,
      kTransferCharacteristics, kTrickPlayFactor, kNaluLengthSize, kLanguage,
      kIsEncrypted));
  MuxerOptions options;
  options.output_file_name = "file.ts";

  MockMuxerListener mock_listener;
  SingleSegmentTsSegmenter segmenter(options, &mock_listener);

  const uint32_t kFirstPts = 1000;

  EXPECT_CALL(*mock_pes_packet_generator_, Initialize(_))
      .WillOnce(Return(true));

  std::shared_ptr<MediaSample> sample1 =
      MediaSample::CopyFrom(kAnyData, arraysize(kAnyData), kIsKeyFrame);
  sample1->set_duration(kInputTimescale * 11);
  std::shared_ptr<MediaSample> sample2 =
      MediaSample::CopyFrom(kAnyData, arraysize(kAnyData), kIsKeyFrame);
  // Doesn't really matter how long this is.
  sample2->set_duration(kInputTimescale * 7);

  EXPECT_CALL(mock_listener,
              OnNewSegment("file.ts",
		           kFirstPts * kTimeScale / kInputTimescale,
                           kTimeScale * 11, _));

  Sequence writer_sequence;
  EXPECT_CALL(*mock_ts_writer_, NewSegment(_))
      .InSequence(writer_sequence)
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_pes_packet_generator_, PushSample(_))
      .Times(2)
      .WillRepeatedly(Return(true));

  Sequence ready_pes_sequence;
  // First AddSample().
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(1u));
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(0u));
  // When Flush() is called, inside second AddSample().
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(0u));
  // Still inside AddSample() but after Flush().
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(1u));

  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(0u));

  EXPECT_CALL(*mock_pes_packet_generator_, Flush())
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_ts_writer_, NewSegment(_))
      .InSequence(writer_sequence)
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_ts_writer_, AddPesPacketMock(_, _))
      .Times(2)
      .WillRepeatedly(Return(true));

  // The pointers are released inside the segmenter.
  Sequence pes_packet_sequence;
  EXPECT_CALL(*mock_pes_packet_generator_, GetNextPesPacketMock())
      .InSequence(pes_packet_sequence)
      .WillOnce(Return(new PesPacket));
  EXPECT_CALL(*mock_pes_packet_generator_, GetNextPesPacketMock())
      .InSequence(pes_packet_sequence)
      .WillOnce(Return(new PesPacket()));

  segmenter.InjectPesPacketGeneratorForTesting(
      std::move(mock_pes_packet_generator_));
  EXPECT_OK(segmenter.Initialize(*stream_info));
  segmenter.InjectTsWriterForTesting(std::move(mock_ts_writer_));
  EXPECT_OK(segmenter.AddSample(*sample1));
  EXPECT_OK(segmenter.FinalizeSegment(kFirstPts, sample1->duration()));
  EXPECT_OK(segmenter.AddSample(*sample2));
}

// Finalize right after Initialize(). The writer will not be initialized.
TEST_F(SingleSegmentTsSegmenterTest, InitializeThenFinalize) {
  std::shared_ptr<VideoStreamInfo> stream_info(new VideoStreamInfo(
      kTrackId, kTimeScale, kDuration, kH264Codec,
      H26xStreamFormat::kAnnexbByteStream, kCodecString, kExtraData,
      arraysize(kExtraData), kWidth, kHeight, kPixelWidth, kPixelHeight,
      kTransferCharacteristics, kTrickPlayFactor, kNaluLengthSize, kLanguage,
      kIsEncrypted));
  MuxerOptions options;
  options.output_file_name = "file.ts";

  MockMuxerListener mock_listener;
  SingleSegmentTsSegmenter segmenter(options, &mock_listener);

  EXPECT_CALL(*mock_pes_packet_generator_, Initialize(_))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_pes_packet_generator_, Flush()).Times(0);
  ON_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .WillByDefault(Return(0));

  segmenter.InjectPesPacketGeneratorForTesting(
      std::move(mock_pes_packet_generator_));
  EXPECT_OK(segmenter.Initialize(*stream_info));
  EXPECT_OK(segmenter.Finalize());
}

// Verify the "normal" case where samples have been added and the writer has
// been initialized.
// The test does not really add any samples but instead simulates an initialized
// writer with a mock.
TEST_F(SingleSegmentTsSegmenterTest, FinalizeSegment) {
  std::shared_ptr<VideoStreamInfo> stream_info(new VideoStreamInfo(
      kTrackId, kTimeScale, kDuration, kH264Codec,
      H26xStreamFormat::kAnnexbByteStream, kCodecString, kExtraData,
      arraysize(kExtraData), kWidth, kHeight, kPixelWidth, kPixelHeight,
      kTransferCharacteristics, kTrickPlayFactor, kNaluLengthSize, kLanguage,
      kIsEncrypted));
  MuxerOptions options;
  options.output_file_name = "file.ts";
  
  MockMuxerListener mock_listener;
  SingleSegmentTsSegmenter segmenter(options, &mock_listener);

  EXPECT_CALL(*mock_pes_packet_generator_, Initialize(_))
      .WillOnce(Return(true));

  InSequence s;
  EXPECT_CALL(*mock_pes_packet_generator_, Flush()).WillOnce(Return(true));
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .WillOnce(Return(0u));

  segmenter.InjectPesPacketGeneratorForTesting(
      std::move(mock_pes_packet_generator_));
  EXPECT_OK(segmenter.Initialize(*stream_info));
  segmenter.InjectTsWriterForTesting(std::move(mock_ts_writer_));

  EXPECT_OK(segmenter.FinalizeSegment(0, 100 /* arbitrary duration*/));
}

TEST_F(SingleSegmentTsSegmenterTest, EncryptedSample) {
  std::shared_ptr<VideoStreamInfo> stream_info(new VideoStreamInfo(
      kTrackId, kTimeScale, kDuration, kH264Codec,
      H26xStreamFormat::kAnnexbByteStream, kCodecString, kExtraData,
      arraysize(kExtraData), kWidth, kHeight, kPixelWidth, kPixelHeight,
      kTransferCharacteristics, kTrickPlayFactor, kNaluLengthSize, kLanguage,
      kIsEncrypted));
  MuxerOptions options;
  options.output_file_name = "file.ts";

  MockMuxerListener mock_listener;
  SingleSegmentTsSegmenter segmenter(options, &mock_listener);

  ON_CALL(*mock_ts_writer_, NewSegment(_)).WillByDefault(Return(true));
  ON_CALL(*mock_ts_writer_, AddPesPacketMock(_,_)).WillByDefault(Return(true));
  ON_CALL(*mock_pes_packet_generator_, Initialize(_))
      .WillByDefault(Return(true));
  ON_CALL(*mock_pes_packet_generator_, Flush()).WillByDefault(Return(true));

  const uint8_t kAnyData[] = {
      0x01, 0x0F, 0x3C,
  };
  std::shared_ptr<MediaSample> sample1 =
      MediaSample::CopyFrom(kAnyData, arraysize(kAnyData), kIsKeyFrame);
  sample1->set_duration(kTimeScale * 2);
  std::shared_ptr<MediaSample> sample2 =
      MediaSample::CopyFrom(kAnyData, arraysize(kAnyData), kIsKeyFrame);
  sample2->set_duration(kTimeScale * 3);

  EXPECT_CALL(*mock_pes_packet_generator_, PushSample(_))
      .Times(2)
      .WillRepeatedly(Return(true));

  Sequence ready_pes_sequence;
  // First AddSample().
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(1u));
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(0u));
  // When Flush() is called, inside second AddSample().
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(0u));
  // Still inside AddSample() but after Flush().
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(1u));
  EXPECT_CALL(*mock_pes_packet_generator_, NumberOfReadyPesPackets())
      .InSequence(ready_pes_sequence)
      .WillOnce(Return(0u));

  EXPECT_CALL(*mock_ts_writer_, AddPesPacketMock(_, _))
      .Times(2)
      .WillRepeatedly(Return(true));

  // The pointers are released inside the segmenter.
  Sequence pes_packet_sequence;
  EXPECT_CALL(*mock_pes_packet_generator_, GetNextPesPacketMock())
      .InSequence(pes_packet_sequence)
      .WillOnce(Return(new PesPacket()));
  EXPECT_CALL(*mock_pes_packet_generator_, GetNextPesPacketMock())
      .InSequence(pes_packet_sequence)
      .WillOnce(Return(new PesPacket()));

  EXPECT_CALL(mock_listener, OnNewSegment("file.ts", _, _, _));

  MockTsWriter* mock_ts_writer_raw = mock_ts_writer_.get();

  segmenter.InjectPesPacketGeneratorForTesting(
      std::move(mock_pes_packet_generator_));

  EXPECT_OK(segmenter.Initialize(*stream_info));
  segmenter.InjectTsWriterForTesting(std::move(mock_ts_writer_));
  EXPECT_OK(segmenter.AddSample(*sample1));
  EXPECT_OK(segmenter.FinalizeSegment(1, sample1->duration()));
  // Signal encrypted if sample is encrypted.
  EXPECT_CALL(*mock_ts_writer_raw, SignalEncrypted());
  sample2->set_is_encrypted(true);
  EXPECT_OK(segmenter.AddSample(*sample2));
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
