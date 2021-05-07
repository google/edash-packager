// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/mpd/base/representation.h"

#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <inttypes.h>

#include "packager/base/strings/stringprintf.h"
#include "packager/file/file.h"
#include "packager/file/file_closer.h"
#include "packager/mpd/base/mpd_options.h"
#include "packager/mpd/test/mpd_builder_test_helper.h"
#include "packager/mpd/test/xml_compare.h"

using ::testing::Bool;
using ::testing::Not;
using ::testing::Values;
using ::testing::WithParamInterface;

DECLARE_bool(use_legacy_vp9_codec_string);

namespace shaka {
namespace {

const uint32_t kAnyRepresentationId = 1;

class MockRepresentationStateChangeListener
    : public RepresentationStateChangeListener {
 public:
  MockRepresentationStateChangeListener() {}
  ~MockRepresentationStateChangeListener() {}

  MOCK_METHOD2(OnNewSegmentForRepresentation,
               void(int64_t start_time, int64_t duration));

  MOCK_METHOD2(OnSetFrameRateForRepresentation,
               void(uint32_t frame_duration, uint32_t timescale));
};

}  // namespace

class RepresentationTest : public ::testing::Test {
 public:
  // TODO(rkuroiwa): Once std::forward() is allowed by chromium style guide, use
  // variadic template and std::forward() so that we don't need to copy the
  // constructor signatures.
  std::unique_ptr<Representation> CreateRepresentation(
      const MediaInfo& media_info,
      uint32_t representation_id,
      std::unique_ptr<RepresentationStateChangeListener>
          state_change_listener) {
    return std::unique_ptr<Representation>(
        new Representation(media_info, mpd_options_, representation_id,
                           std::move(state_change_listener)));
  }

  std::unique_ptr<Representation> CopyRepresentation(
      const Representation& representation,
      std::unique_ptr<RepresentationStateChangeListener>
          state_change_listener) {
    return std::unique_ptr<Representation>(
        new Representation(representation, std::move(state_change_listener)));
  }

  std::unique_ptr<RepresentationStateChangeListener> NoListener() {
    return std::unique_ptr<RepresentationStateChangeListener>();
  }

 protected:
  MpdOptions mpd_options_;
};

// Verify that Representation::Init() works with all "required" fields of
// MedieInfo proto.
TEST_F(RepresentationTest, ValidMediaInfo) {
  const char kTestMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1'\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 1\n";

  auto representation = CreateRepresentation(
      ConvertToMediaInfo(kTestMediaInfo), kAnyRepresentationId, NoListener());
  EXPECT_TRUE(representation->Init());
}

// Verify that if VideoInfo, AudioInfo, or TextInfo is not set, Init() fails.
TEST_F(RepresentationTest, VideoAudioTextInfoNotSet) {
  const char kTestMediaInfo[] = "container_type: 1";

  auto representation = CreateRepresentation(
      ConvertToMediaInfo(kTestMediaInfo), kAnyRepresentationId, NoListener());
  EXPECT_FALSE(representation->Init());
}

// Verify that if more than one of VideoInfo, AudioInfo, or TextInfo is set,
// then Init() fails.
TEST_F(RepresentationTest, VideoAndAudioInfoSet) {
  const char kTestMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1'\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "audio_info {\n"
      "  codec: 'mp4a.40.2'\n"
      "  sampling_frequency: 44100\n"
      "  time_scale: 1200\n"
      "  num_channels: 2\n"
      "}\n"
      "container_type: CONTAINER_MP4\n";

  auto representation = CreateRepresentation(
      ConvertToMediaInfo(kTestMediaInfo), kAnyRepresentationId, NoListener());
  EXPECT_FALSE(representation->Init());
}

// Verify that Representation::Init() fails if a required field is missing.
TEST_F(RepresentationTest, InvalidMediaInfo) {
  // Missing width.
  const char kTestMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1'\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 1\n";
  auto representation = CreateRepresentation(
      ConvertToMediaInfo(kTestMediaInfo), kAnyRepresentationId, NoListener());
  EXPECT_FALSE(representation->Init());
}

// Basic check that the fields in video info are in the XML.
TEST_F(RepresentationTest, CheckVideoInfoReflectedInXml) {
  const char kTestMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1'\n"
      "  width: 1280\n"
      "  height: 720\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 1\n";
  auto representation = CreateRepresentation(
      ConvertToMediaInfo(kTestMediaInfo), kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  const char kExpectedOutput[] =
      "<Representation id=\"1\" bandwidth=\"0\" "
      " codecs=\"avc1\" mimeType=\"video/mp4\" "
      " sar=\"1:1\" width=\"1280\" height=\"720\" "
      " frameRate=\"10/10\"/>";
  EXPECT_THAT(representation->GetXml(), XmlNodeEqual(kExpectedOutput));
}

TEST_F(RepresentationTest, CheckVideoInfoVp8CodecInMp4) {
  const char kTestMediaInfoCodecVp8[] =
      "video_info {\n"
      "  codec: 'vp08.00.00.08.01.01.00.00'\n"
      "  width: 1280\n"
      "  height: 720\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 1\n";
  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTestMediaInfoCodecVp8),
                           kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(),
              AttributeEqual("codecs", "vp08.00.00.08.01.01.00.00"));
}

// Check that vp8 codec string will be updated for backward compatibility
// support in webm.
TEST_F(RepresentationTest, CheckVideoInfoVp8CodecInWebm) {
  const char kTestMediaInfoCodecVp8[] =
      "video_info {\n"
      "  codec: 'vp08.00.00.08.01.01.00.00'\n"
      "  width: 1280\n"
      "  height: 720\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 3\n";
  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTestMediaInfoCodecVp8),
                           kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(), AttributeEqual("codecs", "vp8"));
}

TEST_F(RepresentationTest, CheckVideoInfoVp9CodecInWebm) {
  const char kTestMediaInfoCodecVp9[] =
      "video_info {\n"
      "  codec: 'vp09.00.00.08.01.01.00.00'\n"
      "  width: 1280\n"
      "  height: 720\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 3\n";
  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTestMediaInfoCodecVp9),
                           kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(),
              AttributeEqual("codecs", "vp09.00.00.08.01.01.00.00"));
}

TEST_F(RepresentationTest, CheckVideoInfoLegacyVp9CodecInWebm) {
  FLAGS_use_legacy_vp9_codec_string = true;

  const char kTestMediaInfoCodecVp9[] =
      "video_info {\n"
      "  codec: 'vp09.00.00.08.01.01.00.00'\n"
      "  width: 1280\n"
      "  height: 720\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 3\n";
  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTestMediaInfoCodecVp9),
                           kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(), AttributeEqual("codecs", "vp9"));
}

// Make sure RepresentationStateChangeListener::OnNewSegmentForRepresentation()
// is called.
TEST_F(RepresentationTest,
       RepresentationStateChangeListenerOnNewSegmentForRepresentation) {
  const char kTestMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1'\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 1\n";

  const int64_t kStartTime = 199238u;
  const int64_t kDuration = 98u;
  const int64_t kSegmentNumber = 1;

  std::unique_ptr<MockRepresentationStateChangeListener> listener(
      new MockRepresentationStateChangeListener());
  EXPECT_CALL(*listener, OnNewSegmentForRepresentation(kStartTime, kDuration));
  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTestMediaInfo),
                           kAnyRepresentationId, std::move(listener));
  EXPECT_TRUE(representation->Init());

  representation->AddNewSegment(kStartTime, kDuration, 10 /* any size */,
                                kSegmentNumber);
}

// Make sure
// RepresentationStateChangeListener::OnSetFrameRateForRepresentation()
// is called.
TEST_F(RepresentationTest,
       RepresentationStateChangeListenerOnSetFrameRateForRepresentation) {
  const char kTestMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1'\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 1000\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 1\n";

  const uint32_t kTimeScale = 1000u;
  const int64_t kFrameDuration = 33u;
  std::unique_ptr<MockRepresentationStateChangeListener> listener(
      new MockRepresentationStateChangeListener());
  EXPECT_CALL(*listener,
              OnSetFrameRateForRepresentation(kFrameDuration, kTimeScale));
  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTestMediaInfo),
                           kAnyRepresentationId, std::move(listener));
  EXPECT_TRUE(representation->Init());

  representation->SetSampleDuration(kFrameDuration);
}

TEST_F(RepresentationTest, TtmlXmlMimeType) {
  const char kTtmlXmlMediaInfo[] =
      "text_info {\n"
      "  codec: 'ttml'\n"
      "}\n"
      "container_type: CONTAINER_TEXT\n";

  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTtmlXmlMediaInfo),
                           kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(),
              AttributeEqual("mimeType", "application/ttml+xml"));
}

TEST_F(RepresentationTest, TtmlMp4MimeType) {
  const char kTtmlMp4MediaInfo[] =
      "text_info {\n"
      "  codec: 'ttml'\n"
      "}\n"
      "container_type: CONTAINER_MP4\n";

  auto representation =
      CreateRepresentation(ConvertToMediaInfo(kTtmlMp4MediaInfo),
                           kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(),
              AttributeEqual("mimeType", "application/mp4"));
}

TEST_F(RepresentationTest, WebVttMimeType) {
  const char kWebVttMediaInfo[] =
      "text_info {\n"
      "  codec: 'wvtt'\n"
      "}\n"
      "container_type: CONTAINER_TEXT\n";

  auto representation = CreateRepresentation(
      ConvertToMediaInfo(kWebVttMediaInfo), kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(), AttributeEqual("mimeType", "text/vtt"));
}

// Verify that Suppress*() methods work.
TEST_F(RepresentationTest, SuppressRepresentationAttributes) {
  const char kTestMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1'\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  frame_duration: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "container_type: 1\n";

  auto representation = CreateRepresentation(
      ConvertToMediaInfo(kTestMediaInfo), kAnyRepresentationId, NoListener());

  representation->SuppressOnce(Representation::kSuppressWidth);
  auto no_width = representation->GetXml();
  EXPECT_THAT(no_width, Not(AttributeSet("width")));
  EXPECT_THAT(no_width, AttributeEqual("height", "480"));
  EXPECT_THAT(no_width, AttributeEqual("frameRate", "10/10"));

  representation->SuppressOnce(Representation::kSuppressHeight);
  auto no_height = representation->GetXml();
  EXPECT_THAT(no_height, Not(AttributeSet("height")));
  EXPECT_THAT(no_height, AttributeEqual("width", "720"));
  EXPECT_THAT(no_height, AttributeEqual("frameRate", "10/10"));

  representation->SuppressOnce(Representation::kSuppressFrameRate);
  auto no_frame_rate = representation->GetXml();
  EXPECT_THAT(no_frame_rate, Not(AttributeSet("frameRate")));
  EXPECT_THAT(no_frame_rate, AttributeEqual("width", "720"));
  EXPECT_THAT(no_frame_rate, AttributeEqual("height", "480"));
}

TEST_F(RepresentationTest, CheckRepresentationId) {
  const MediaInfo video_media_info = GetTestMediaInfo(kFileNameVideoMediaInfo1);
  const uint32_t kRepresentationId = 1;

  auto representation =
      CreateRepresentation(video_media_info, kRepresentationId, NoListener());
  EXPECT_TRUE(representation->Init());
  EXPECT_THAT(representation->GetXml(),
              AttributeEqual("id", std::to_string(kRepresentationId)));
}

namespace {

// Any number for {AdaptationSet,Representation} ID. Required to create
// either objects. Not checked in test.
const char kSElementTemplate[] =
    "<S t=\"%" PRIu64 "\" d=\"%" PRIu64 "\" r=\"%d\"/>\n";
const char kSElementTemplateWithoutR[] =
    "<S t=\"%" PRIu64 "\" d=\"%" PRIu64 "\"/>\n";
const int kDefaultStartNumber = 1;
const uint32_t kDefaultTimeScale = 1000u;
const int64_t kScaledTargetSegmentDuration = 10;
const double kTargetSegmentDurationInSeconds =
    static_cast<double>(kScaledTargetSegmentDuration) / kDefaultTimeScale;
const uint32_t kSampleDuration = 2;

std::string GetDefaultMediaInfo() {
  const char kMediaInfo[] =
      "video_info {\n"
      "  codec: 'avc1.010101'\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  frame_duration: 5\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "reference_time_scale: %u\n"
      "container_type: 1\n"
      "init_segment_url: 'init.mp4'\n"
      "segment_template_url: '$Time$.mp4'\n";

  return base::StringPrintf(kMediaInfo, kDefaultTimeScale);
}

}  // namespace

class SegmentTemplateTest : public RepresentationTest {
 public:
  void SetUp() override {
    mpd_options_.mpd_type = MpdType::kDynamic;
    representation_ =
        CreateRepresentation(ConvertToMediaInfo(GetDefaultMediaInfo()),
                             kAnyRepresentationId, NoListener());
    ASSERT_TRUE(representation_->Init());
  }

  void AddSegments(int64_t start_time,
                   int64_t duration,
                   uint64_t size,
                   int repeat) {
    DCHECK(representation_);

    SegmentInfo s = {start_time, duration, repeat, segment_number_};
    segment_infos_for_expected_out_.push_back(s);
    if (repeat == 0) {
      expected_s_elements_ +=
          base::StringPrintf(kSElementTemplateWithoutR, start_time, duration);
    } else {
      expected_s_elements_ +=
          base::StringPrintf(kSElementTemplate, start_time, duration, repeat);
    }

    for (int i = 0; i < repeat + 1; ++i) {
      representation_->AddNewSegment(start_time, duration, size,
                                     segment_number_++);
      start_time += duration;
      bandwidth_estimator_.AddBlock(
          size, static_cast<double>(duration) / kDefaultTimeScale);
    }
  }

 protected:
  std::string ExpectedXml() {
    const char kOutputTemplate[] =
        "<Representation id=\"1\" bandwidth=\"%" PRIu64
        "\" "
        " codecs=\"avc1.010101\" mimeType=\"video/mp4\" sar=\"1:1\" "
        " width=\"720\" height=\"480\" frameRate=\"10/5\">\n"
        "  <SegmentTemplate timescale=\"1000\" "
        "   initialization=\"init.mp4\" media=\"$Time$.mp4\" "
        "   startNumber=\"1\">\n"
        "    <SegmentTimeline>\n"
        "      %s\n"
        "    </SegmentTimeline>\n"
        "  </SegmentTemplate>\n"
        "</Representation>\n";
    return base::StringPrintf(kOutputTemplate, bandwidth_estimator_.Max(),
                              expected_s_elements_.c_str());
  }

  std::unique_ptr<Representation> representation_;
  std::list<SegmentInfo> segment_infos_for_expected_out_;
  std::string expected_s_elements_;
  BandwidthEstimator bandwidth_estimator_;
  int64_t segment_number_ = 1;
};

// Estimate the bandwidth given the info from AddNewSegment().
TEST_F(SegmentTemplateTest, OneSegmentNormal) {
  const int64_t kStartTime = 0;
  const int64_t kDuration = 10;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDuration, kSize, 0);

  expected_s_elements_ = "<S t=\"0\" d=\"10\"/>";
  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

TEST_F(SegmentTemplateTest, RepresentationClone) {
  MediaInfo media_info = ConvertToMediaInfo(GetDefaultMediaInfo());
  media_info.set_segment_template_url("$Number$.mp4");
  representation_ =
      CreateRepresentation(media_info, kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation_->Init());

  const int64_t kStartTime = 0;
  const int64_t kDuration = 10;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDuration, kSize, 0);

  auto cloned_representation =
      CopyRepresentation(*representation_, NoListener());

  const char kExpectedXml[] =
      "<Representation id=\"1\" bandwidth=\"0\" "
      " codecs=\"avc1.010101\" mimeType=\"video/mp4\" sar=\"1:1\" "
      " width=\"720\" height=\"480\" frameRate=\"10/5\">\n"
      "  <SegmentTemplate timescale=\"1000\" initialization=\"init.mp4\" "
      "   media=\"$Number$.mp4\" startNumber=\"1\">\n"
      "  </SegmentTemplate>\n"
      "</Representation>\n";
  EXPECT_THAT(cloned_representation->GetXml(), XmlNodeEqual(kExpectedXml));
}

TEST_F(SegmentTemplateTest, PresentationTimeOffset) {
  const int64_t kStartTime = 0;
  const int64_t kDuration = 10;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDuration, kSize, 0);

  const double kPresentationTimeOffsetSeconds = 2.3;
  representation_->SetPresentationTimeOffset(kPresentationTimeOffsetSeconds);

  const char kExpectedXml[] =
      "<Representation id=\"1\" bandwidth=\"102400\" "
      " codecs=\"avc1.010101\" mimeType=\"video/mp4\" sar=\"1:1\" "
      " width=\"720\" height=\"480\" frameRate=\"10/5\">\n"
      "  <SegmentTemplate timescale=\"1000\" presentationTimeOffset=\"2300\""
      "   initialization=\"init.mp4\" media=\"$Time$.mp4\" startNumber=\"1\">\n"
      "    <SegmentTimeline>\n"
      "      <S t=\"0\" d=\"10\"/>\n"
      "     </SegmentTimeline>\n"
      "  </SegmentTemplate>\n"
      "</Representation>\n";
  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(kExpectedXml));
}

TEST_F(SegmentTemplateTest, GetStartAndEndTimestamps) {
  double start_timestamp;
  double end_timestamp;
  // No segments.
  EXPECT_FALSE(representation_->GetStartAndEndTimestamps(&start_timestamp,
                                                         &end_timestamp));

  const int64_t kStartTime = 88;
  const int64_t kDuration = 10;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDuration, kSize, 0);
  AddSegments(kStartTime + kDuration, kDuration, kSize, 2);
  ASSERT_TRUE(representation_->GetStartAndEndTimestamps(&start_timestamp,
                                                        &end_timestamp));
  EXPECT_EQ(static_cast<double>(kStartTime) / kDefaultTimeScale,
            start_timestamp);
  EXPECT_EQ(static_cast<double>(kStartTime + kDuration * 4) / kDefaultTimeScale,
            end_timestamp);
}

TEST_F(SegmentTemplateTest, NormalRepeatedSegmentDuration) {
  const uint64_t kSize = 256;
  int64_t start_time = 0;
  int64_t duration = 40000;
  int repeat = 2;
  AddSegments(start_time, duration, kSize, repeat);

  start_time += duration * (repeat + 1);
  duration = 54321;
  repeat = 0;
  AddSegments(start_time, duration, kSize, repeat);

  start_time += duration * (repeat + 1);
  duration = 12345;
  repeat = 0;
  AddSegments(start_time, duration, kSize, repeat);

  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

TEST_F(SegmentTemplateTest, RepeatedSegmentsFromNonZeroStartTime) {
  const uint64_t kSize = 100000;
  int64_t start_time = 0;
  int64_t duration = 100000;
  int repeat = 2;
  AddSegments(start_time, duration, kSize, repeat);

  start_time += duration * (repeat + 1);
  duration = 20000;
  repeat = 3;
  AddSegments(start_time, duration, kSize, repeat);

  start_time += duration * (repeat + 1);
  duration = 32123;
  repeat = 3;
  AddSegments(start_time, duration, kSize, repeat);

  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

// Segments not starting from 0.
// Start time is 10. Make sure r gets set correctly.
TEST_F(SegmentTemplateTest, NonZeroStartTime) {
  const int64_t kStartTime = 10;
  const int64_t kDuration = 22000;
  const int kSize = 123456;
  const int kRepeat = 1;
  AddSegments(kStartTime, kDuration, kSize, kRepeat);

  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

// There is a gap in the segments, but still valid.
TEST_F(SegmentTemplateTest, NonContiguousLiveInfo) {
  const int64_t kStartTime = 10;
  const int64_t kDuration = 22000;
  const int kSize = 123456;
  const int kRepeat = 0;
  AddSegments(kStartTime, kDuration, kSize, kRepeat);

  const int64_t kStartTimeOffset = 100;
  AddSegments(kDuration + kStartTimeOffset, kDuration, kSize, kRepeat);

  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

// Add segments out of order. Segments that start before the previous segment
// cannot be added.
TEST_F(SegmentTemplateTest, OutOfOrder) {
  const int64_t kEarlierStartTime = 0;
  const int64_t kLaterStartTime = 1000;
  const int64_t kDuration = 1000;
  const int kSize = 123456;
  const int kRepeat = 0;

  AddSegments(kLaterStartTime, kDuration, kSize, kRepeat);
  AddSegments(kEarlierStartTime, kDuration, kSize, kRepeat);

  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

// No segments should be overlapping.
TEST_F(SegmentTemplateTest, OverlappingSegments) {
  const int64_t kEarlierStartTime = 0;
  const int64_t kDuration = 1000;
  const int kSize = 123456;
  const int kRepeat = 0;

  const int64_t kOverlappingSegmentStartTime = kDuration / 2;
  CHECK_GT(kDuration, kOverlappingSegmentStartTime);

  AddSegments(kEarlierStartTime, kDuration, kSize, kRepeat);
  AddSegments(kOverlappingSegmentStartTime, kDuration, kSize, kRepeat);

  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

// Some segments can be overlapped due to rounding errors. As long as it falls
// in the range of rounding error defined inside MpdBuilder, the segment gets
// accepted.
TEST_F(SegmentTemplateTest, OverlappingSegmentsWithinErrorRange) {
  const int64_t kEarlierStartTime = 0;
  const int64_t kDuration = 1000;
  const int kSize = 123456;
  const int kRepeat = 0;

  const int64_t kOverlappingSegmentStartTime = kDuration - 1;
  CHECK_GT(kDuration, kOverlappingSegmentStartTime);

  AddSegments(kEarlierStartTime, kDuration, kSize, kRepeat);
  AddSegments(kOverlappingSegmentStartTime, kDuration, kSize, kRepeat);

  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(ExpectedXml()));
}

class SegmentTimelineTestBase : public SegmentTemplateTest {
 public:
  void SetUp() override {
    // The only diff with current GetDefaultMediaInfo() is that this uses
    // $Number$ for segment template.
    const char kMediaInfo[] =
        "video_info {\n"
        "  codec: 'avc1.010101'\n"
        "  width: 720\n"
        "  height: 480\n"
        "  time_scale: 10\n"
        "  frame_duration: 2\n"
        "  pixel_width: 1\n"
        "  pixel_height: 1\n"
        "}\n"
        "reference_time_scale: %u\n"
        "container_type: 1\n"
        "init_segment_url: 'init.mp4'\n"
        "segment_template_url: '$Number$.mp4'\n";
    const std::string& number_template_media_info =
        base::StringPrintf(kMediaInfo, kDefaultTimeScale);
    mpd_options_.mpd_type = MpdType::kDynamic;
    mpd_options_.mpd_params.target_segment_duration =
        kTargetSegmentDurationInSeconds;
    representation_ =
        CreateRepresentation(ConvertToMediaInfo(number_template_media_info),
                             kAnyRepresentationId, NoListener());
    ASSERT_TRUE(representation_->Init());
  }

  std::string ExpectedXml(const std::string& expected_s_element,
                          int expected_start_number) {
    const char kOutputTemplate[] =
        "<Representation id=\"1\" bandwidth=\"%" PRIu64
        "\" "
        " codecs=\"avc1.010101\" mimeType=\"video/mp4\" sar=\"1:1\" "
        " width=\"720\" height=\"480\" frameRate=\"10/2\">\n"
        "  <SegmentTemplate timescale=\"1000\" "
        "   initialization=\"init.mp4\" media=\"$Number$.mp4\" "
        "   startNumber=\"%d\">\n"
        "    <SegmentTimeline>\n"
        "      %s\n"
        "    </SegmentTimeline>\n"
        "  </SegmentTemplate>\n"
        "</Representation>\n";

    return base::StringPrintf(kOutputTemplate, bandwidth_estimator_.Max(),
                              expected_start_number,
                              expected_s_element.c_str());
  }
};

class ApproximateSegmentTimelineTest : public SegmentTimelineTestBase,
                                       public WithParamInterface<bool> {
 public:
  void SetUp() override {
    allow_approximate_segment_timeline_ = GetParam();
    mpd_options_.mpd_params.allow_approximate_segment_timeline =
        allow_approximate_segment_timeline_;
    SegmentTimelineTestBase::SetUp();
    representation_->SetSampleDuration(kSampleDuration);
  }

  std::string ExpectedXml(const std::string& expected_s_element) {
    return SegmentTimelineTestBase::ExpectedXml(expected_s_element,
                                                kDefaultStartNumber);
  }

 protected:
  bool allow_approximate_segment_timeline_;
};

TEST_P(ApproximateSegmentTimelineTest, SegmentDurationAdjusted) {
  const int64_t kStartTime = 0;
  const int64_t kDurationSmaller =
      kScaledTargetSegmentDuration - kSampleDuration / 2;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDurationSmaller, kSize, 0);

  std::string expected_s_elements;
  if (allow_approximate_segment_timeline_) {
    expected_s_elements = base::StringPrintf(
        kSElementTemplateWithoutR, kStartTime, kScaledTargetSegmentDuration);
  } else {
    expected_s_elements = base::StringPrintf(kSElementTemplateWithoutR,
                                             kStartTime, kDurationSmaller);
  }
  EXPECT_THAT(representation_->GetXml(),
              XmlNodeEqual(ExpectedXml(expected_s_elements)));
}

TEST_P(ApproximateSegmentTimelineTest,
       SegmentDurationAdjustedWithNonZeroStartTime) {
  const int64_t kStartTime = 12345;
  const int64_t kDurationSmaller =
      kScaledTargetSegmentDuration - kSampleDuration / 2;
  const uint64_t kSize = 128;

  AddSegments(kStartTime, kDurationSmaller, kSize, 0);

  std::string expected_s_elements;
  if (allow_approximate_segment_timeline_) {
    expected_s_elements = base::StringPrintf(
        kSElementTemplateWithoutR, kStartTime, kScaledTargetSegmentDuration);
  } else {
    expected_s_elements = base::StringPrintf(kSElementTemplateWithoutR,
                                             kStartTime, kDurationSmaller);
  }
  EXPECT_THAT(representation_->GetXml(),
              XmlNodeEqual(ExpectedXml(expected_s_elements)));
}

TEST_P(ApproximateSegmentTimelineTest, SegmentsWithSimilarDurations) {
  const int64_t kStartTime = 0;
  const int64_t kDurationSmaller =
      kScaledTargetSegmentDuration - kSampleDuration / 2;
  const int64_t kDurationLarger =
      kScaledTargetSegmentDuration + kSampleDuration / 2;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDurationSmaller, kSize, 0);
  AddSegments(kStartTime + kDurationSmaller, kDurationLarger, kSize, 0);
  AddSegments(kStartTime + kDurationSmaller + kDurationLarger, kDurationSmaller,
              kSize, 0);

  std::string expected_s_elements;
  if (allow_approximate_segment_timeline_) {
    int kNumSegments = 3;
    expected_s_elements =
        base::StringPrintf(kSElementTemplate, kStartTime,
                           kScaledTargetSegmentDuration, kNumSegments - 1);
  } else {
    expected_s_elements =
        base::StringPrintf(kSElementTemplateWithoutR, kStartTime,
                           kDurationSmaller) +
        base::StringPrintf(kSElementTemplateWithoutR,
                           kStartTime + kDurationSmaller, kDurationLarger) +
        base::StringPrintf(kSElementTemplateWithoutR,
                           kStartTime + kDurationSmaller + kDurationLarger,
                           kDurationSmaller);
  }
  EXPECT_THAT(representation_->GetXml(),
              XmlNodeEqual(ExpectedXml(expected_s_elements)));
}

// We assume the actual segment duration fluctuates around target segment
// duration; if it is not the case (which should not happen with our demuxer),
// this is how the output would look like.
TEST_P(ApproximateSegmentTimelineTest, SegmentsWithSimilarDurations2) {
  const int64_t kStartTime = 0;
  const int64_t kDurationLarger =
      kScaledTargetSegmentDuration + kSampleDuration / 2;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDurationLarger, kSize, 0);
  AddSegments(kStartTime + kDurationLarger, kDurationLarger, kSize, 0);
  AddSegments(kStartTime + 2 * kDurationLarger, kDurationLarger, kSize, 0);

  std::string expected_s_elements;
  if (allow_approximate_segment_timeline_) {
    expected_s_elements =
        "<S t=\"0\" d=\"10\" r=\"1\"/>"
        "<S t=\"20\" d=\"13\"/>";
  } else {
    int kNumSegments = 3;
    expected_s_elements = base::StringPrintf(kSElementTemplate, kStartTime,
                                             kDurationLarger, kNumSegments - 1);
  }
  EXPECT_THAT(representation_->GetXml(),
              XmlNodeEqual(ExpectedXml(expected_s_elements)));
}

TEST_P(ApproximateSegmentTimelineTest, FillSmallGap) {
  const int64_t kStartTime = 0;
  const int64_t kDuration = kScaledTargetSegmentDuration;
  const int64_t kGap = kSampleDuration / 2;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDuration, kSize, 0);
  AddSegments(kStartTime + kDuration + kGap, kDuration, kSize, 0);
  AddSegments(kStartTime + 2 * kDuration + kGap, kDuration, kSize, 0);

  std::string expected_s_elements;
  if (allow_approximate_segment_timeline_) {
    int kNumSegments = 3;
    expected_s_elements = base::StringPrintf(kSElementTemplate, kStartTime,
                                             kDuration, kNumSegments - 1);
  } else {
    expected_s_elements =
        base::StringPrintf(kSElementTemplateWithoutR, kStartTime, kDuration) +
        base::StringPrintf(kSElementTemplate, kStartTime + kDuration + kGap,
                           kDuration, 1 /* repeat */);
  }
  EXPECT_THAT(representation_->GetXml(),
              XmlNodeEqual(ExpectedXml(expected_s_elements)));
}

TEST_P(ApproximateSegmentTimelineTest, FillSmallOverlap) {
  const int64_t kStartTime = 0;
  const int64_t kDuration = kScaledTargetSegmentDuration;
  const int64_t kOverlap = kSampleDuration / 2;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDuration, kSize, 0);
  AddSegments(kStartTime + kDuration - kOverlap, kDuration, kSize, 0);
  AddSegments(kStartTime + 2 * kDuration - kOverlap, kDuration, kSize, 0);

  std::string expected_s_elements;
  if (allow_approximate_segment_timeline_) {
    int kNumSegments = 3;
    expected_s_elements = base::StringPrintf(kSElementTemplate, kStartTime,
                                             kDuration, kNumSegments - 1);
  } else {
    expected_s_elements =
        base::StringPrintf(kSElementTemplateWithoutR, kStartTime, kDuration) +
        base::StringPrintf(kSElementTemplate, kStartTime + kDuration - kOverlap,
                           kDuration, 1 /* repeat */);
  }
  EXPECT_THAT(representation_->GetXml(),
              XmlNodeEqual(ExpectedXml(expected_s_elements)));
}

// Check the segments are grouped correctly when sample duration is not
// available, which happens for text streams.
// See https://github.com/google/shaka-packager/issues/417 for the background.
TEST_P(ApproximateSegmentTimelineTest, NoSampleDuration) {
  const char kMediaInfo[] =
      "text_info {\n"
      "  codec: 'wvtt'\n"
      "}\n"
      "reference_time_scale: 1000\n"
      "container_type: 1\n"
      "init_segment_url: 'init.mp4'\n"
      "segment_template_url: '$Number$.mp4'\n";
  representation_ = CreateRepresentation(ConvertToMediaInfo(kMediaInfo),
                                         kAnyRepresentationId, NoListener());
  ASSERT_TRUE(representation_->Init());

  const int64_t kStartTime = 0;
  const int64_t kDuration = kScaledTargetSegmentDuration;
  const uint64_t kSize = 128;
  AddSegments(kStartTime, kDuration, kSize, 0);
  AddSegments(kStartTime + kDuration, kDuration, kSize, 0);
  AddSegments(kStartTime + 2 * kDuration, kDuration, kSize, 0);

  const char kExpectedXml[] =
      "<Representation id=\"1\" bandwidth=\"102400\" codecs=\"wvtt\""
      " mimeType=\"application/mp4\">\n"
      "  <SegmentTemplate timescale=\"1000\" initialization=\"init.mp4\" "
      "   media=\"$Number$.mp4\" startNumber=\"1\">\n"
      "    <SegmentTimeline>\n"
      "      <S t=\"0\" d=\"10\" r=\"2\"/>\n"
      "     </SegmentTimeline>\n"
      "  </SegmentTemplate>\n"
      "</Representation>\n";
  EXPECT_THAT(representation_->GetXml(), XmlNodeEqual(kExpectedXml));
}

INSTANTIATE_TEST_CASE_P(ApproximateSegmentTimelineTest,
                        ApproximateSegmentTimelineTest,
                        Bool());

class TimeShiftBufferDepthTest : public SegmentTimelineTestBase,
                                 public WithParamInterface<int64_t> {
 public:
  void SetUp() override {
    initial_start_time_ = GetParam();
    SegmentTimelineTestBase::SetUp();
  }

  MpdOptions* mutable_mpd_options() { return &mpd_options_; }

 protected:
  int64_t initial_start_time_;
};

// All segments have the same duration and size.
TEST_P(TimeShiftBufferDepthTest, Normal) {
  const int kTimeShiftBufferDepth = 10;  // 10 sec.
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  // Trick to make every segment 1 second long.
  const int64_t kDuration = kDefaultTimeScale;
  const uint64_t kSize = 10000;
  const int kRepeat = 1234;
  const int kLength = kRepeat;

  CHECK_EQ(kDuration / kDefaultTimeScale * kRepeat, kLength);

  AddSegments(initial_start_time_, kDuration, kSize, kRepeat);

  // There should only be the last 11 segments because timeshift is 10 sec and
  // each segment is 1 sec and the latest segments start time is "current
  // time" i.e., the latest segment does not count as part of timeshift buffer
  // depth.
  // Also note that S@r + 1 is the actual number of segments.
  const int kExpectedRepeatsLeft = kTimeShiftBufferDepth;
  const int kExpectedStartNumber = kRepeat - kExpectedRepeatsLeft + 1;

  const std::string expected_s_element = base::StringPrintf(
      kSElementTemplate,
      initial_start_time_ + kDuration * (kRepeat - kExpectedRepeatsLeft),
      kDuration, kExpectedRepeatsLeft);
  EXPECT_THAT(
      representation_->GetXml(),
      XmlNodeEqual(ExpectedXml(expected_s_element, kExpectedStartNumber)));
}

// TimeShiftBufferDepth is shorter than a segment. This should not discard the
// segment that can play TimeShiftBufferDepth.
// For example if TimeShiftBufferDepth = 1 min. and a 10 min segment was just
// added. Before that 9 min segment was added. The 9 min segment should not be
// removed from the MPD.
TEST_P(TimeShiftBufferDepthTest, TimeShiftBufferDepthShorterThanSegmentLength) {
  const int kTimeShiftBufferDepth = 10;  // 10 sec.
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  // Each duration is a second longer than timeShiftBufferDepth.
  const int64_t kDuration = kDefaultTimeScale * (kTimeShiftBufferDepth + 1);
  const int kSize = 10000;
  const int kRepeat = 1;

  AddSegments(initial_start_time_, kDuration, kSize, kRepeat);

  const std::string expected_s_element = base::StringPrintf(
      kSElementTemplate, initial_start_time_, kDuration, kRepeat);
  EXPECT_THAT(
      representation_->GetXml(),
      XmlNodeEqual(ExpectedXml(expected_s_element, kDefaultStartNumber)));
}

// More generic version the normal test.
TEST_P(TimeShiftBufferDepthTest, Generic) {
  const int kTimeShiftBufferDepth = 30;
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  const int64_t kDuration = kDefaultTimeScale;
  const int kSize = 10000;
  const int kRepeat = 1000;

  AddSegments(initial_start_time_, kDuration, kSize, kRepeat);
  const int64_t first_s_element_end_time =
      initial_start_time_ + kDuration * (kRepeat + 1);

  // Now add 2 kTimeShiftBufferDepth long segments.
  const int kNumMoreSegments = 2;
  const int kMoreSegmentsRepeat = kNumMoreSegments - 1;
  const int64_t kTimeShiftBufferDepthDuration =
      kDefaultTimeScale * kTimeShiftBufferDepth;
  AddSegments(first_s_element_end_time, kTimeShiftBufferDepthDuration, kSize,
              kMoreSegmentsRepeat);

  // Expect only the latest S element with 2 segments.
  const std::string expected_s_element =
      base::StringPrintf(kSElementTemplate, first_s_element_end_time,
                         kTimeShiftBufferDepthDuration, kMoreSegmentsRepeat);

  const int kExpectedRemovedSegments = kRepeat + 1;
  EXPECT_THAT(
      representation_->GetXml(),
      XmlNodeEqual(ExpectedXml(
          expected_s_element, kDefaultStartNumber + kExpectedRemovedSegments)));
}

// More than 1 S element in the result.
// Adds 100 one-second segments. Then add 21 two-second segments.
// This should have all of the two-second segments and 60 one-second
// segments. Note that it expects 60 segments from the first S element because
// the most recent segment added does not count
TEST_P(TimeShiftBufferDepthTest, MoreThanOneS) {
  const int kTimeShiftBufferDepth = 100;
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  const int kSize = 20000;

  const int64_t kOneSecondDuration = kDefaultTimeScale;
  const int kOneSecondSegmentRepeat = 99;
  AddSegments(initial_start_time_, kOneSecondDuration, kSize,
              kOneSecondSegmentRepeat);
  const int64_t first_s_element_end_time =
      initial_start_time_ + kOneSecondDuration * (kOneSecondSegmentRepeat + 1);

  const int64_t kTwoSecondDuration = 2 * kDefaultTimeScale;
  const int kTwoSecondSegmentRepeat = 20;
  AddSegments(first_s_element_end_time, kTwoSecondDuration, kSize,
              kTwoSecondSegmentRepeat);

  const int kExpectedRemovedSegments =
      (kOneSecondSegmentRepeat + 1 + kTwoSecondSegmentRepeat * 2) -
      kTimeShiftBufferDepth;

  std::string expected_s_element = base::StringPrintf(
      kSElementTemplate,
      initial_start_time_ + kOneSecondDuration * kExpectedRemovedSegments,
      kOneSecondDuration, kOneSecondSegmentRepeat - kExpectedRemovedSegments);
  expected_s_element +=
      base::StringPrintf(kSElementTemplate, first_s_element_end_time,
                         kTwoSecondDuration, kTwoSecondSegmentRepeat);

  EXPECT_THAT(
      representation_->GetXml(),
      XmlNodeEqual(ExpectedXml(
          expected_s_element, kDefaultStartNumber + kExpectedRemovedSegments)));
}

// Edge case where the last segment in S element should still be in the MPD.
// Example:
// Assuming timescale = 1 so that duration of 1 means 1 second.
// TimeShiftBufferDepth is 9 sec and we currently have
// <S t=0 d=1.5 r=1 />
// <S t=3 d=2 r=3 />
// and we add another contiguous 2 second segment.
// Then the first S element's last segment should still be in the MPD.
TEST_P(TimeShiftBufferDepthTest, UseLastSegmentInS) {
  const int kTimeShiftBufferDepth = 9;
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  const int64_t kDuration1 = static_cast<int64_t>(kDefaultTimeScale * 1.5);
  const int kSize = 20000;
  const int kRepeat1 = 1;

  AddSegments(initial_start_time_, kDuration1, kSize, kRepeat1);

  const int64_t first_s_element_end_time =
      initial_start_time_ + kDuration1 * (kRepeat1 + 1);

  const int64_t kTwoSecondDuration = 2 * kDefaultTimeScale;
  const int kTwoSecondSegmentRepeat = 4;

  AddSegments(first_s_element_end_time, kTwoSecondDuration, kSize,
              kTwoSecondSegmentRepeat);

  std::string expected_s_element = base::StringPrintf(
      kSElementTemplateWithoutR,
      initial_start_time_ + kDuration1,  // Expect one segment removed.
      kDuration1);

  expected_s_element +=
      base::StringPrintf(kSElementTemplate, first_s_element_end_time,
                         kTwoSecondDuration, kTwoSecondSegmentRepeat);
  EXPECT_THAT(representation_->GetXml(),
              XmlNodeEqual(ExpectedXml(expected_s_element, 2)));
}

// Gap between S elements but both should be included.
TEST_P(TimeShiftBufferDepthTest, NormalGap) {
  const int kTimeShiftBufferDepth = 10;
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  const int64_t kDuration = kDefaultTimeScale;
  const int kSize = 20000;
  const int kRepeat = 6;
  // CHECK here so that the when next S element is added with 1 segment, this S
  // element doesn't go away.
  CHECK_LT(kRepeat - 1u, static_cast<uint64_t>(kTimeShiftBufferDepth));
  CHECK_EQ(kDuration, kDefaultTimeScale);

  AddSegments(initial_start_time_, kDuration, kSize, kRepeat);

  const int64_t first_s_element_end_time =
      initial_start_time_ + kDuration * (kRepeat + 1);

  const int64_t gap_s_element_start_time = first_s_element_end_time + 1;
  AddSegments(gap_s_element_start_time, kDuration, kSize, /* no repeat */ 0);

  std::string expected_s_element = base::StringPrintf(
      kSElementTemplate, initial_start_time_, kDuration, kRepeat);
  expected_s_element += base::StringPrintf(kSElementTemplateWithoutR,
                                           gap_s_element_start_time, kDuration);

  EXPECT_THAT(
      representation_->GetXml(),
      XmlNodeEqual(ExpectedXml(expected_s_element, kDefaultStartNumber)));
}

// Timeshift is based on segment duration not on segment time.
TEST_P(TimeShiftBufferDepthTest, HugeGap) {
  const int kTimeShiftBufferDepth = 10;
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  const int64_t kDuration = kDefaultTimeScale;
  const int kSize = 20000;
  const int kRepeat = 6;
  AddSegments(initial_start_time_, kDuration, kSize, kRepeat);

  const int64_t first_s_element_end_time =
      initial_start_time_ + kDuration * (kRepeat + 1);

  // Big enough gap so first S element should not be there.
  const int64_t gap_s_element_start_time =
      first_s_element_end_time +
      (kTimeShiftBufferDepth + 1) * kDefaultTimeScale;
  const int kSecondSElementRepeat = 9;
  static_assert(
      kSecondSElementRepeat < static_cast<int64_t>(kTimeShiftBufferDepth),
      "second_s_element_repeat_must_be_less_than_time_shift_buffer_depth");
  AddSegments(gap_s_element_start_time, kDuration, kSize,
              kSecondSElementRepeat);

  std::string expected_s_element =
      base::StringPrintf(kSElementTemplateWithoutR,
                         initial_start_time_ + kRepeat * kDuration, kDuration) +
      base::StringPrintf(kSElementTemplate, gap_s_element_start_time, kDuration,
                         kSecondSElementRepeat);
  const int kExpectedRemovedSegments = kRepeat;
  EXPECT_THAT(
      representation_->GetXml(),
      XmlNodeEqual(ExpectedXml(
          expected_s_element, kDefaultStartNumber + kExpectedRemovedSegments)));
}

// Check if startNumber is working correctly.
TEST_P(TimeShiftBufferDepthTest, ManySegments) {
  const int kTimeShiftBufferDepth = 1;
  mutable_mpd_options()->mpd_params.time_shift_buffer_depth =
      kTimeShiftBufferDepth;

  const int64_t kDuration = kDefaultTimeScale;
  const int kSize = 20000;
  const int kRepeat = 10000;
  const int kTotalNumSegments = kRepeat + 1;
  AddSegments(initial_start_time_, kDuration, kSize, kRepeat);

  const int kExpectedSegmentsLeft = kTimeShiftBufferDepth + 1;
  const int kExpectedSegmentsRepeat = kExpectedSegmentsLeft - 1;
  const int kExpectedRemovedSegments =
      kTotalNumSegments - kExpectedSegmentsLeft;
  const int kExpectedStartNumber =
      kDefaultStartNumber + kExpectedRemovedSegments;

  std::string expected_s_element = base::StringPrintf(
      kSElementTemplate,
      initial_start_time_ + kExpectedRemovedSegments * kDuration, kDuration,
      kExpectedSegmentsRepeat);
  EXPECT_THAT(
      representation_->GetXml(),
      XmlNodeEqual(ExpectedXml(expected_s_element, kExpectedStartNumber)));
}

INSTANTIATE_TEST_CASE_P(InitialStartTime,
                        TimeShiftBufferDepthTest,
                        Values(0, 1000));

namespace {
const int kTimeShiftBufferDepth = 2;
const int kNumPreservedSegmentsOutsideLiveWindow = 3;
const int kMaxNumSegmentsAvailable =
    kTimeShiftBufferDepth + 1 + kNumPreservedSegmentsOutsideLiveWindow;

const char kSegmentTemplate[] = "memory://$Number$.mp4";
const char kSegmentTemplateUrl[] = "video/$Number$.mp4";
const char kStringPrintTemplate[] = "memory://%d.mp4";

const int64_t kInitialStartTime = 0;
const int64_t kDuration = kDefaultTimeScale;
const uint64_t kSize = 10;
const uint64_t kNoRepeat = 0;
}  // namespace

class RepresentationDeleteSegmentsTest : public SegmentTimelineTestBase {
 public:
  void SetUp() override {
    SegmentTimelineTestBase::SetUp();

    // Create 100 files with the template.
    for (int i = 1; i <= 100; ++i) {
      File::WriteStringToFile(
          base::StringPrintf(kStringPrintTemplate, i).c_str(), "dummy content");
    }

    MediaInfo media_info = ConvertToMediaInfo(GetDefaultMediaInfo());
    media_info.set_segment_template(kSegmentTemplate);
    media_info.set_segment_template_url(kSegmentTemplateUrl);
    representation_ =
        CreateRepresentation(media_info, kAnyRepresentationId, NoListener());
    ASSERT_TRUE(representation_->Init());

    mpd_options_.mpd_params.time_shift_buffer_depth = kTimeShiftBufferDepth;
    mpd_options_.mpd_params.preserved_segments_outside_live_window =
        kNumPreservedSegmentsOutsideLiveWindow;
  }

  bool SegmentDeleted(const std::string& segment_name) {
    std::unique_ptr<File, FileCloser> file_closer(
        File::Open(segment_name.c_str(), "r"));
    return file_closer.get() == nullptr;
  }
};

// Verify that no segments are deleted initially until there are more than
// |kMaxNumSegmentsAvailable| segments.
TEST_F(RepresentationDeleteSegmentsTest, NoSegmentsDeletedInitially) {
  for (int i = 0; i < kMaxNumSegmentsAvailable; ++i) {
    AddSegments(kInitialStartTime + i * kDuration, kDuration, kSize, kNoRepeat);
  }
  for (int i = 0; i < kMaxNumSegmentsAvailable; ++i) {
    EXPECT_FALSE(
        SegmentDeleted(base::StringPrintf(kStringPrintTemplate, i + 1)));
  }
}

TEST_F(RepresentationDeleteSegmentsTest, OneSegmentDeleted) {
  for (int i = 0; i <= kMaxNumSegmentsAvailable; ++i) {
    AddSegments(kInitialStartTime + i * kDuration, kDuration, kSize, kNoRepeat);
  }
  EXPECT_FALSE(SegmentDeleted(base::StringPrintf(kStringPrintTemplate, 2)));
  EXPECT_TRUE(SegmentDeleted(base::StringPrintf(kStringPrintTemplate, 1)));
}

// Verify that segments are deleted as expected with many non-repeating
// segments.
TEST_F(RepresentationDeleteSegmentsTest, ManyNonRepeatingSegments) {
  int many_segments = 50;
  for (int i = 0; i < many_segments; ++i) {
    AddSegments(kInitialStartTime + i * kDuration, kDuration, kSize, kNoRepeat);
  }
  const int last_available_segment_index =
      many_segments - kMaxNumSegmentsAvailable + 1;
  EXPECT_FALSE(SegmentDeleted(
      base::StringPrintf(kStringPrintTemplate, last_available_segment_index)));
  EXPECT_TRUE(SegmentDeleted(base::StringPrintf(
      kStringPrintTemplate, last_available_segment_index - 1)));
}

// Verify that segments are deleted as expected with many repeating segments.
TEST_F(RepresentationDeleteSegmentsTest, ManyRepeatingSegments) {
  const int kLoops = 4;
  const int kRepeat = 10;
  for (int i = 0; i < kLoops; ++i) {
    AddSegments(kInitialStartTime + i * kDuration * (kRepeat + 1), kDuration,
                kSize, kRepeat);
  }
  const int kNumSegments = kLoops * (kRepeat + 1);
  const int last_available_segment_index =
      kNumSegments - kMaxNumSegmentsAvailable + 1;
  EXPECT_FALSE(SegmentDeleted(
      base::StringPrintf(kStringPrintTemplate, last_available_segment_index)));
  EXPECT_TRUE(SegmentDeleted(base::StringPrintf(
      kStringPrintTemplate, last_available_segment_index - 1)));
}

}  // namespace shaka
