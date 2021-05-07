// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packager/media/formats/webm/multi_segment_segmenter.h"

#include <gtest/gtest.h>
#include <memory>
#include "packager/media/base/muxer_util.h"
#include "packager/media/formats/webm/segmenter_test_base.h"

namespace shaka {
namespace media {
namespace {

const uint32_t kTimeScale = 1000000u;
const uint64_t kDuration = 1000000u;
const int64_t kSegmentNumber1 = 1;
const int64_t kSegmentNumber2 = 2;
const bool kSubsegment = true;

const uint8_t kBasicSupportDataInit[] = {
  // ID: EBML Header omitted.
  // ID: Segment, Payload Size: Unknown
  0x18, 0x53, 0x80, 0x67, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // ID: Void, Payload Size: 87
    0xec, 0xd7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    // ID: Info, Payload Size: 81
    0x15, 0x49, 0xa9, 0x66, 0xd1,
      // TimecodeScale: 1000000
      0x2a, 0xd7, 0xb1, 0x83, 0x0f, 0x42, 0x40,
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
          0x54, 0xba, 0x81, 0x64
};
const uint8_t kBasicSupportDataSegment[] = {
  // ID: Cluster, Payload Size: 64
  0x1f, 0x43, 0xb6, 0x75, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
    // Timecode: 0
    0xe7, 0x81, 0x00,
    // ID: SimpleBlock, Payload Size: 9
    0xa3, 0x89, 0x81, 0x00, 0x00, 0x80, 0xde, 0xad, 0xbe, 0xef, 0x00,
    // ID: SimpleBlock, Payload Size: 9
    0xa3, 0x89, 0x81, 0x03, 0xe8, 0x80, 0xde, 0xad, 0xbe, 0xef, 0x00,
    // ID: SimpleBlock, Payload Size: 9
    0xa3, 0x89, 0x81, 0x07, 0xd0, 0x80, 0xde, 0xad, 0xbe, 0xef, 0x00,
    // ID: SimpleBlock, Payload Size: 9
    0xa3, 0x89, 0x81, 0x0b, 0xb8, 0x80, 0xde, 0xad, 0xbe, 0xef, 0x00,
    // ID: BlockGroup, Payload Size: 15
    0xa0, 0x8f,
      // ID: Block, Payload Size: 9
      0xa1, 0x89, 0x81, 0x0f, 0xa0, 0x00, 0xde, 0xad, 0xbe, 0xef, 0x00,
      // BlockDuration: 1000
      0x9b, 0x82, 0x03, 0xe8
};

}  // namespace

class MultiSegmentSegmenterTest : public SegmentTestBase {
 public:
  MultiSegmentSegmenterTest()
      : info_(CreateVideoStreamInfo(kTimeScale)),
        segment_template_(std::string(kMemoryFilePrefix) +
                          "output-template-$Number$.webm") {}

 protected:
  void InitializeSegmenter(const MuxerOptions& options) {
    ASSERT_NO_FATAL_FAILURE(
        CreateAndInitializeSegmenter<webm::MultiSegmentSegmenter>(
            options, *info_, &segmenter_));
  }

  std::string TemplateFileName(int number) const {
    return GetSegmentName(segment_template_, 0, number, 0);
  }

  std::shared_ptr<StreamInfo> info_;
  std::string segment_template_;
  std::unique_ptr<webm::Segmenter> segmenter_;
};

TEST_F(MultiSegmentSegmenterTest, BasicSupport) {
  MuxerOptions options = CreateMuxerOptions();
  options.segment_template = segment_template_;
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // Write the samples to the Segmenter.
  for (int i = 0; i < 5; i++) {
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, kNoSideData);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(0, 8 * kDuration, !kSubsegment,
                                        kSegmentNumber1));
  ASSERT_OK(segmenter_->Finalize());

  // Verify the resulting data.
  ASSERT_FILE_ENDS_WITH(OutputFileName().c_str(), kBasicSupportDataInit);
  ASSERT_FILE_EQ(TemplateFileName(1).c_str(), kBasicSupportDataSegment);

  // There is no second segment.
  EXPECT_FALSE(File::Open(TemplateFileName(2).c_str(), "r"));
}

TEST_F(MultiSegmentSegmenterTest, SplitsFilesOnSegment) {
  MuxerOptions options = CreateMuxerOptions();
  options.segment_template = segment_template_;
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // Write the samples to the Segmenter.
  for (int i = 0; i < 8; i++) {
    if (i == 5) {
      ASSERT_OK(segmenter_->FinalizeSegment(0, 5 * kDuration, !kSubsegment,
                                            kSegmentNumber1));
    }
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, kNoSideData);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(5 * kDuration, 8 * kDuration,
                                        !kSubsegment, kSegmentNumber2));
  ASSERT_OK(segmenter_->Finalize());

  // Verify the resulting data.
  ClusterParser parser;
  ASSERT_NO_FATAL_FAILURE(parser.PopulateFromCluster(TemplateFileName(1)));
  ASSERT_EQ(1u, parser.cluster_count());
  EXPECT_EQ(5u, parser.GetFrameCountForCluster(0));

  ASSERT_NO_FATAL_FAILURE(parser.PopulateFromCluster(TemplateFileName(2)));
  ASSERT_EQ(1u, parser.cluster_count());
  EXPECT_EQ(3u, parser.GetFrameCountForCluster(0));

  EXPECT_FALSE(File::Open(TemplateFileName(3).c_str(), "r"));
}

TEST_F(MultiSegmentSegmenterTest, SplitsClustersOnSubsegment) {
  MuxerOptions options = CreateMuxerOptions();
  options.segment_template = segment_template_;
  ASSERT_NO_FATAL_FAILURE(InitializeSegmenter(options));

  // Write the samples to the Segmenter.
  for (int i = 0; i < 8; i++) {
    if (i == 5) {
      ASSERT_OK(segmenter_->FinalizeSegment(0, 5 * kDuration, kSubsegment,
                                            kSegmentNumber1));
    }
    std::shared_ptr<MediaSample> sample =
        CreateSample(kKeyFrame, kDuration, kNoSideData);
    ASSERT_OK(segmenter_->AddSample(*sample));
  }
  ASSERT_OK(segmenter_->FinalizeSegment(0, 8 * kDuration, !kSubsegment,
                                        kSegmentNumber1));
  ASSERT_OK(segmenter_->Finalize());

  // Verify the resulting data.
  ClusterParser parser;
  ASSERT_NO_FATAL_FAILURE(parser.PopulateFromCluster(TemplateFileName(1)));
  ASSERT_EQ(2u, parser.cluster_count());
  EXPECT_EQ(5u, parser.GetFrameCountForCluster(0));
  EXPECT_EQ(3u, parser.GetFrameCountForCluster(1));

  EXPECT_FALSE(File::Open(TemplateFileName(2).c_str(), "r"));
}

}  // namespace media
}  // namespace shaka
