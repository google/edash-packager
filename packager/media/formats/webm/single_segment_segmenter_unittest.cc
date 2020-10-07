// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packager/media/formats/webm/two_pass_single_segment_segmenter.h"

#include <gtest/gtest.h>
#include <memory>
#include "packager/media/formats/webm/segmenter_test_base.h"

namespace shaka {
namespace media {
namespace {

const uint32_t kTimeScale = 1000000;
const uint32_t kTimecodeScale = 1000000;
const int64_t kSecondsToNs = 1000000000L;
const uint64_t kDuration = 1000000;
const int64_t kSegmentIndex0 = 0;
const int64_t kSegmentIndex1 = 1;
const bool kSubsegment = true;

const uint8_t kBasicSupportData[] = {
  // ID: EBML Header omitted.
  // ID: Segment, Payload Size: 343
  0x18, 0x53, 0x80, 0x67, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x57,
    // ID: SeekHead, Payload Size: 57
    0x11, 0x4d, 0x9b, 0x74, 0xb8,
      // ID: Seek, Payload Size: 11
      0x4d, 0xbb, 0x8b,
        // SeekID: binary(4) (Info)
        0x53, 0xab, 0x84, 0x15, 0x49, 0xa9, 0x66,
        // SeekPosition: 89
        0x53, 0xac, 0x81, 0x59,
      // ID: Seek, Payload Size: 11
      0x4d, 0xbb, 0x8b,
        // SeekID: binary(4) (Tracks)
        0x53, 0xab, 0x84, 0x16, 0x54, 0xae, 0x6b,
        // SeekPosition: 182
        0x53, 0xac, 0x81, 0xb6,
      // ID: Seek, Payload Size: 12
      0x4d, 0xbb, 0x8b,
        // SeekID: binary(4) (Cues)
        0x53, 0xab, 0x84, 0x1c, 0x53, 0xbb, 0x6b,
        // SeekPosition: 228
        0x53, 0xac, 0x81, 0xe4,
      // ID: Seek, Payload Size: 11
      0x4d, 0xbb, 0x8b,
        // SeekID: binary(4) (Cluster)
        0x53, 0xab, 0x84, 0x1f, 0x43, 0xb6, 0x75,
        // SeekPosition: 246
        0x53, 0xac, 0x81, 0xf6,
    // ID: Void, Payload Size: 26
    0xec, 0x9a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // ID: Info, Payload Size: 88
    0x15, 0x49, 0xa9, 0x66, 0xd8,
      // TimecodeScale: 1000000
      0x2a, 0xd7, 0xb1, 0x83, 0x0f, 0x42, 0x40,
      // Duration: float(5000)
      0x44, 0x89, 0x84, 0x45, 0x9c, 0x40, 0x00,
      // MuxingApp: 'libwebm-0.2.1.0'
      0x4d, 0x80, 0x8f, 0x6c, 0x69, 0x62, 0x77, 0x65, 0x62, 0x6d, 0x2d, 0x30,
      0x2e, 0x32, 0x2e, 0x31, 0x2e, 0x30,
      // WritingApp: 'https://github.com/google/shaka-packager version test'
      0x57, 0x41, 0xb5,
      0x68, 0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x67, 0x69, 0x74, 0x68,
      0x75, 0x62, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0x67, 0x6f, 0x6f, 0x67, 0x6c,
      0x65, 0x2f, 0x73, 0x68, 0x61, 0x6b, 0x61, 0x2d, 0x70, 0x61, 0x63, 0x6b,
      0x61, 0x67, 0x65, 0x72, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
      0x20, 0x74, 0x65, 0x73, 0x74,
    // ID: Tracks, Payload Size: 41
    0x16, 0x54, 0xae, 0x6b, 0xa9,
      // ID: Track, Payload Size: 39
      0xae, 0xa7,
        // TrackNumber: 1
        0xd7, 0x81, 0x01,
        // TrackUID: 1
        0x73, 0xc5, 0x81, 0x01,
        // TrackType: 1
        0x83, 0x81, 0x01,
        // CodecID: 'V_VP8'
        0x86, 0x85, 0x56, 0x5f, 0x56, 0x50, 0x38,
        // Language: 'en'
        0x22, 0xb5, 0x9c, 0x82, 0x65, 0x6e,
        // ID: Video, Payload Size: 14
        0xe0, 0x8e,
          // PixelWidth: 100
          0xb0, 0x81, 0x64,
          // PixelHeight: 100
          0xba, 0x81, 0x64,
          // DisplayWidth: 100
          0x54, 0xb0, 0x81, 0x64,
          // DisplayHeight: 100
          0x54, 0xba, 0x81, 0x64,
    // ID: Cues, Payload Size: 13
    0x1c, 0x53, 0xbb, 0x6b, 0x8d,
      // ID: CuePoint, Payload Size: 11
      0xbb, 0x8b,
        // CueTime: 0
        0xb3, 0x81, 0x00,
        // ID: CueTrackPositions, Payload Size: 6
        0xb7, 0x86,
          // CueTrack: 1
          0xf7, 0x81, 0x01,
          // CueClusterPosition: 246
          0xf1, 0x81, 0xf6,
    // ID: Cluster, Payload Size: 85
    0x1f, 0x43, 0xb6, 0x75, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55,
      // Timecode: 0
      0xe7, 0x81, 0x00,
      // ID: SimpleBlock, Payload Size: 9
      0xa3, 0x89, 0x81, 0x00, 0x00, 0x80, 0xde, 0xad, 0xbe, 0xef, 0x00,
      // ID: SimpleBlock, Payload Size: 9
      0xa3, 0x89, 0x81, 0x03, 0xe8, 0x80, 0xde, 0xad, 0xbe, 0xef, 0x00,
      // ID: SimpleBlock, Payload Size: 9
      0xa3, 0x89, 0x81, 0x07, 0xd0, 0x80, 0xde, 0xad, 0xbe, 0xef, 0x00,
      // ID: BlockGroup, Payload Size: 30
      0xa0, 0x9e,
        // ID: Block, Payload Size: 9
        0xa1, 0x89, 0x81, 0x0b, 0xb8, 0x00, 0xde, 0xad, 0xbe, 0xef, 0x00,
        // ID: BlockAdditions, Payload Size: 16
        0x75, 0xa1, 0x90,
          // ID: BlockMore, Payload Size: 14
          0xa6, 0x8e,
            // ID: BlockAddID, Payload Size: 1
            0xee, 0x85, 0x9a, 0x78, 0x56, 0x34, 0x12,
            // ID: BlockAdditional, Payload Size: 5
            0xa5, 0x85, 0x73, 0x69, 0x64, 0x65, 0x00,
      // ID: BlockGroup, Payload Size: 15
      0xa0, 0x8f,
        // ID: Block, Payload Size: 9
        0xa1, 0x89, 0x81, 0x0f, 0xa0, 0x00, 0xde, 0xad, 0xbe, 0xef, 0x00,
        // BlockDuration: 1000
        0x9b, 0x82, 0x03, 0xe8,
};

}  // namespace

class SingleSegmentSegmenterTest : public SegmentTestBase {
 public:
  SingleSegmentSegmenterTest() : info_(CreateVideoStreamInfo(kTimeScale)) {}

 protected:
  void InitializeSegmenter(const MuxerOptions& options) {
    ASSERT_NO_FATAL_FAILURE(
        CreateAndInitializeSegmenter<webm::TwoPassSingleSegmentSegmenter>(
            options, *info_, &segmenter_));
  }

  std::shared_ptr<StreamInfo> info_;
  std::unique_ptr<webm::Segmenter> segmenter_;
};

TEST_F(SingleSegmentSegmenterTest, BasicSupport) {
  MuxerOptions options = CreateMuxerOptions();
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // Write the samples to the Segmenter.
  for (int i = 0; i < 5; i++) {
    const SideDataFlag side_data_flag =
        i == 3 ? kGenerateSideData : kNoSideData;
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, side_data_flag);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(0, 5 * kDuration, !kSubsegment,
                                        kSegmentIndex0));
  ASSERT_OK(segmenter_->Finalize());

  ASSERT_FILE_ENDS_WITH(OutputFileName().c_str(), kBasicSupportData);
}

TEST_F(SingleSegmentSegmenterTest, SplitsClustersOnSegment) {
  MuxerOptions options = CreateMuxerOptions();
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // Write the samples to the Segmenter.
  for (int i = 0; i < 8; i++) {
    if (i == 5) {
      ASSERT_OK(segmenter_->FinalizeSegment(0, 5 * kDuration, !kSubsegment,
                                            kSegmentIndex0));
    }
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, kNoSideData);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(5 * kDuration, 8 * kDuration,
                                        !kSubsegment, kSegmentIndex1));
  ASSERT_OK(segmenter_->Finalize());

  // Verify the resulting data.
  ClusterParser parser;
  ASSERT_NO_FATAL_FAILURE(parser.PopulateFromSegment(OutputFileName()));
  ASSERT_EQ(2u, parser.cluster_count());
  EXPECT_EQ(5u, parser.GetFrameCountForCluster(0));
  EXPECT_EQ(3u, parser.GetFrameCountForCluster(1));
}

TEST_F(SingleSegmentSegmenterTest, IgnoresSubsegment) {
  MuxerOptions options = CreateMuxerOptions();
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // Write the samples to the Segmenter.
  for (int i = 0; i < 8; i++) {
    if (i == 5) {
      ASSERT_OK(segmenter_->FinalizeSegment(0, 5 * kDuration, kSubsegment,
                                            kSegmentIndex0));
    }
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, kNoSideData);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(0, 8 * kDuration, !kSubsegment,
                                        kSegmentIndex1));
  ASSERT_OK(segmenter_->Finalize());

  // Verify the resulting data.
  ClusterParser parser;
  ASSERT_NO_FATAL_FAILURE(parser.PopulateFromSegment(OutputFileName()));
  ASSERT_EQ(1u, parser.cluster_count());
  EXPECT_EQ(8u, parser.GetFrameCountForCluster(0));
}

TEST_F(SingleSegmentSegmenterTest, LargeTimestamp) {
  MuxerOptions options = CreateMuxerOptions();
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // 3 hrs. It will overflow int64_t if multiplied by kSecondsToNs.
  const int64_t kLargeTimestamp = 3ll * 3600 * kTimeScale;
  set_cur_timestamp(kLargeTimestamp);

  // Write the samples to the Segmenter.
  for (int i = 0; i < 5; i++) {
    const SideDataFlag side_data_flag =
        i == 3 ? kGenerateSideData : kNoSideData;
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, side_data_flag);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(kLargeTimestamp, 5 * kDuration,
                           !kSubsegment, (kLargeTimestamp / (5 * kDuration))));
  ASSERT_OK(segmenter_->Finalize());

  // Verify the resulting data.
  ClusterParser parser;
  ASSERT_NO_FATAL_FAILURE(parser.PopulateFromSegment(OutputFileName()));
  ASSERT_EQ(1u, parser.cluster_count());
  ASSERT_EQ(5u, parser.GetFrameCountForCluster(0));

  const int64_t kClusterTimescode =
      kLargeTimestamp / kTimeScale * kSecondsToNs / kTimecodeScale;
  const int64_t kAdditionalTimecodePerSample =
      kDuration / kTimeScale * kSecondsToNs / kTimecodeScale;
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(kClusterTimescode + kAdditionalTimecodePerSample * i,
              parser.GetFrameTimecode(0, i));
  }
}

TEST_F(SingleSegmentSegmenterTest, ReallyLargeTimestamp) {
  MuxerOptions options = CreateMuxerOptions();
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // 10 years.
  const int64_t kReallyLargeTimestamp = 10ll * 365 * 24 * 3600 * kTimeScale;
  set_cur_timestamp(kReallyLargeTimestamp);

  // Write the samples to the Segmenter.
  for (int i = 0; i < 5; i++) {
    const SideDataFlag side_data_flag =
        i == 3 ? kGenerateSideData : kNoSideData;
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, side_data_flag);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(kReallyLargeTimestamp, 5 * kDuration,
                     !kSubsegment, (kReallyLargeTimestamp / (5 * kDuration))));
  ASSERT_OK(segmenter_->Finalize());

  // Verify the resulting data.
  ClusterParser parser;
  ASSERT_NO_FATAL_FAILURE(parser.PopulateFromSegment(OutputFileName()));
  ASSERT_EQ(1u, parser.cluster_count());
  ASSERT_EQ(5u, parser.GetFrameCountForCluster(0));

  const int64_t kClusterTimescode =
      kReallyLargeTimestamp / kTimeScale * kSecondsToNs / kTimecodeScale;
  const int64_t kAdditionalTimecodePerSample =
      kDuration / kTimeScale * kSecondsToNs / kTimecodeScale;
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(kClusterTimescode + kAdditionalTimecodePerSample * i,
              parser.GetFrameTimecode(0, i));
  }
}

}  // namespace media
}  // namespace shaka
