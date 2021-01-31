// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/base/time/time.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/file/file.h"
#include "packager/file/file_closer.h"
#include "packager/file/file_test_util.h"
#include "packager/hls/base/media_playlist.h"
#include "packager/version/version.h"

namespace shaka {
namespace hls {

using ::testing::_;
using ::testing::ElementsAreArray;
using ::testing::ReturnArg;
using ::testing::Values;
using ::testing::WithParamInterface;

namespace {

const char kDefaultPlaylistFileName[] = "default_playlist.m3u8";
const double kTimeShiftBufferDepth = 20;
const uint64_t kTimeScale = 90000;
const uint64_t kMBytes = 1000000;
const uint64_t kZeroByteOffset = 0;
const base::Time kRefTime = base::Time();

// Converts timestamp to ISO/IEC 8601:2004 date/time representation,
//  such as YYYY-MM-DDThh:mm:ss.SSSZ
std::string timeToString(base::Time ref_time) {
  const char date_time_tmpl[] = "%4d-%02d-%02dT%02d:%02d:%02d.%03dZ";
  base::Time::Exploded time;
  ref_time.UTCExplode(&time);
  return base::StringPrintf(
      date_time_tmpl,
      time.year, time.month, time.day_of_month,
      time.hour, time.minute, time.second, time.millisecond);
}

MATCHER_P(MatchesString, expected_string, "") {
  const std::string arg_string(static_cast<const char*>(arg));
  *result_listener << "which is " << arg_string.size()
                   << " long and the content is " << arg_string;
  return expected_string == std::string(static_cast<const char*>(arg));
}

}  // namespace

class MediaPlaylistTest : public ::testing::Test {
 protected:
  MediaPlaylistTest() : MediaPlaylistTest(HlsPlaylistType::kVod) {}

  MediaPlaylistTest(HlsPlaylistType type)
      : default_file_name_(kDefaultPlaylistFileName),
        default_name_("default_name"),
        default_group_id_("default_group_id") {
    hls_params_.playlist_type = type;
    hls_params_.time_shift_buffer_depth = kTimeShiftBufferDepth;
    media_playlist_.reset(new MediaPlaylist(hls_params_, default_file_name_,
                                            default_name_, default_group_id_));
  }

  void SetUp() override {
    SetPackagerVersionForTesting("test");

    MediaInfo::VideoInfo* video_info =
        valid_video_media_info_.mutable_video_info();
    video_info->set_codec("avc1");
    video_info->set_time_scale(kTimeScale);
    video_info->set_frame_duration(3000);
    video_info->set_width(1280);
    video_info->set_height(720);
    video_info->set_pixel_width(1);
    video_info->set_pixel_height(1);

    valid_video_media_info_.set_reference_time_scale(kTimeScale);
  }

  HlsParams* mutable_hls_params() { return &hls_params_; }

  const std::string default_file_name_;
  const std::string default_name_;
  const std::string default_group_id_;
  HlsParams hls_params_;
  std::unique_ptr<MediaPlaylist> media_playlist_;

  MediaInfo valid_video_media_info_;
};

class MediaPlaylistMultiSegmentTest : public MediaPlaylistTest {
 protected:
  MediaPlaylistMultiSegmentTest() : MediaPlaylistTest() {}
  // This constructor is for Live and Event playlist tests.
  MediaPlaylistMultiSegmentTest(HlsPlaylistType type)
      : MediaPlaylistTest(type) {}

  void SetUp() override {
    MediaPlaylistTest::SetUp();
    // This is just set to be consistent with the multisegment format and used
    // as a switch in MediaPlaylist.
    // The template string doesn't really matter.
    valid_video_media_info_.set_segment_template_url("file$Number$.ts");
  }
};

class MediaPlaylistSingleSegmentTest : public MediaPlaylistTest {
 protected:
  MediaPlaylistSingleSegmentTest() : MediaPlaylistTest() {}
};

// Verify that SetMediaInfo() fails if timescale is not present.
TEST_F(MediaPlaylistMultiSegmentTest, NoTimeScale) {
  MediaInfo media_info;
  EXPECT_FALSE(media_playlist_->SetMediaInfo(media_info));
}

TEST_F(MediaPlaylistMultiSegmentTest, SetMediaInfoText) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);
  MediaInfo::TextInfo* text_info = media_info.mutable_text_info();
  text_info->set_codec("wvtt");
  EXPECT_TRUE(media_playlist_->SetMediaInfo(media_info));
}

TEST_F(MediaPlaylistMultiSegmentTest, SetMediaInfo) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);
  MediaInfo::VideoInfo* video_info = media_info.mutable_video_info();
  video_info->set_width(1280);
  video_info->set_height(720);
  EXPECT_TRUE(media_playlist_->SetMediaInfo(media_info));
}

// Verify that AddSegment works (not crash).
TEST_F(MediaPlaylistMultiSegmentTest, AddSegment) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  media_playlist_->AddSegment("file1.ts", 900000, 0, kZeroByteOffset, 1000000, kRefTime);
}

// Verify that it returns the display resolution.
TEST_F(MediaPlaylistMultiSegmentTest, GetDisplayResolution) {
  // A real case using sintel video.
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);
  MediaInfo::VideoInfo* video_info = media_info.mutable_video_info();
  video_info->set_width(1920);
  video_info->set_height(818);
  video_info->set_pixel_width(1636);
  video_info->set_pixel_height(1635);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  uint32_t width = 0;
  uint32_t height = 0;
  EXPECT_TRUE(media_playlist_->GetDisplayResolution(&width, &height));
  EXPECT_EQ(1921u, width);
  EXPECT_EQ(818u, height);
}

TEST_F(MediaPlaylistSingleSegmentTest, InitRange) {
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:0\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"501@0\"\n"
      "#EXT-X-ENDLIST\n";
  valid_video_media_info_.set_media_file_url("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(0);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistSingleSegmentTest, InitRangeWithOffset) {
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:0\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"485@16\"\n"
      "#EXT-X-ENDLIST\n";
  valid_video_media_info_.set_media_file_url("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(16);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Closest to the normal use case where there is an init range and then
// subsegment ranges. There is index range between the subsegment and init
// range.
TEST_F(MediaPlaylistSingleSegmentTest, AddSegmentByteRange) {
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:10\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"501@0\"\n"
      "#EXTINF:10.000,\n"
      "#EXT-X-BYTERANGE:1000000@1000\n"
      "file.mp4\n"
      "#EXTINF:10.000,\n"
      "#EXT-X-BYTERANGE:2000000\n"
      "file.mp4\n"
      "#EXT-X-ENDLIST\n";
  valid_video_media_info_.set_media_file_url("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(0);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  media_playlist_->AddSegment("file.mp4", 0, 10 * kTimeScale, 1000,
                              1 * kMBytes, kRefTime);
  media_playlist_->AddSegment("file.mp4", 10 * kTimeScale, 10 * kTimeScale,
                              1001000, 2 * kMBytes, kRefTime);

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistSingleSegmentTest, AddSegmentByteRangeWithExtXProgramDateTime) {
  hls_params_.add_ext_x_program_date_time = true;
  base::Time reference_time;
  ASSERT_TRUE(base::Time::FromUTCString("2020-09-11 13:37:00.000 UTC",
                                        &reference_time));

  media_playlist_.reset(new MediaPlaylist(hls_params_, default_file_name_,
                                          default_name_, default_group_id_));

  valid_video_media_info_.set_media_file_url("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(0);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  uint64_t start_time = 60 * kTimeScale;
  uint64_t duration = 10 * kTimeScale;
  base::Time expected_date = reference_time + base::TimeDelta::FromSeconds(60);

  media_playlist_->AddSegment("file.mp4", start_time, duration, 1000,
                              1 * kMBytes, reference_time);
  media_playlist_->AddSegment("file.mp4", start_time + duration, duration,
                              1001000, 2 * kMBytes, reference_time);

  // create a discontinuity
  base::Time expected_date2 = expected_date + base::TimeDelta::FromSeconds(60);
  media_playlist_->AddSegment("file2.mp4", 0, duration,
                              0, 1 * kMBytes, expected_date2);

  std::string kExpectedOutput = base::StringPrintf(
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:10\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"501@0\"\n"
      "#EXT-X-PROGRAM-DATE-TIME:%s\n"
      "#EXTINF:10.000,\n"
      "#EXT-X-BYTERANGE:1000000@1000\n"
      "file.mp4\n"
      "#EXTINF:10.000,\n"
      "#EXT-X-BYTERANGE:2000000\n"
      "file.mp4\n"
      "#EXT-X-DISCONTINUITY\n"
      "#EXT-X-PROGRAM-DATE-TIME:%s\n"
      "#EXTINF:10.000,\n"
      "#EXT-X-BYTERANGE:1000000@0\n"
      "file2.mp4\n"
      "#EXT-X-ENDLIST\n",
          timeToString(expected_date).c_str(),
          timeToString(expected_date2).c_str());

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Verify that AddEncryptionInfo works (not crash).
TEST_F(MediaPlaylistMultiSegmentTest, AddEncryptionInfo) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0xabcedf", "", "");
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFile) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:0\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// If bitrate (bandwidth) is not set in the MediaInfo, use it.
TEST_F(MediaPlaylistMultiSegmentTest, UseBitrateInMediaInfo) {
  valid_video_media_info_.set_bandwidth(8191);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  EXPECT_EQ(8191u, media_playlist_->MaxBitrate());
}

// If bitrate (bandwidth) is not set in the MediaInfo, then calculate from the
// segments.
TEST_F(MediaPlaylistMultiSegmentTest, GetBitrateFromSegments) {
  valid_video_media_info_.clear_bandwidth();
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);

  EXPECT_EQ(2000000u, media_playlist_->MaxBitrate());
  EXPECT_EQ(1600000u, media_playlist_->AvgBitrate());
}

TEST_F(MediaPlaylistMultiSegmentTest, GetLongestSegmentDuration) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  media_playlist_->AddSegment("file3.ts", 40 * kTimeScale, 14 * kTimeScale,
                              kZeroByteOffset, 3 * kMBytes, kRefTime);

  EXPECT_NEAR(30.0, media_playlist_->GetLongestSegmentDuration(), 0.01);
}

TEST_F(MediaPlaylistMultiSegmentTest, SetTargetDuration) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  media_playlist_->SetTargetDuration(20);
  const std::string kExpectedOutput =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithSegments) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest,
       WriteToFileWithSegmentsAndPlacementOpportunity) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddPlacementOpportunity();
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXT-X-PLACEMENT-OPPORTUNITY\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithEncryptionInfo) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0x12345678", "com.widevine", "1/2/4");
  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithEncryptionInfoEmptyIv) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "", "",
      "com.widevine", "");
  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Verify that EXT-X-DISCONTINUITY is inserted before EXT-X-KEY.
TEST_F(MediaPlaylistMultiSegmentTest, WriteToFileWithClearLead) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0x12345678", "com.widevine", "1/2/4");
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXT-X-DISCONTINUITY\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, GetLanguage) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  // Check conversions from long to short form.
  media_info.mutable_audio_info()->set_language("eng");
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ("en", media_playlist_->language());  // short form

  media_info.mutable_audio_info()->set_language("eng-US");
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ("en-US", media_playlist_->language());  // region preserved

  media_info.mutable_audio_info()->set_language("apa");
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ("apa", media_playlist_->language());  // no short form exists
}

TEST_F(MediaPlaylistMultiSegmentTest, GetNumChannels) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  // Returns 0 by default if not audio.
  EXPECT_EQ(0, media_playlist_->GetNumChannels());

  media_info.mutable_audio_info()->set_num_channels(2);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(2, media_playlist_->GetNumChannels());

  media_info.mutable_audio_info()->set_num_channels(8);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(8, media_playlist_->GetNumChannels());
}

TEST_F(MediaPlaylistMultiSegmentTest, GetEC3JocComplexity) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  // Returns 0 by default if not audio.
  EXPECT_EQ(0, media_playlist_->GetEC3JocComplexity());

  media_info.mutable_audio_info()->mutable_codec_specific_data()->
    set_ec3_joc_complexity(16);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(16, media_playlist_->GetEC3JocComplexity());

  media_info.mutable_audio_info()->mutable_codec_specific_data()->
    set_ec3_joc_complexity(6);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(6, media_playlist_->GetEC3JocComplexity());
}

TEST_F(MediaPlaylistMultiSegmentTest, GetAC4ImsFlag) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  // Returns false by default if not audio.
  EXPECT_EQ(false, media_playlist_->GetAC4ImsFlag());

  media_info.mutable_audio_info()->mutable_codec_specific_data()->
    set_ac4_ims_flag(false);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(false, media_playlist_->GetAC4ImsFlag());

  media_info.mutable_audio_info()->mutable_codec_specific_data()->
    set_ac4_ims_flag(true);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(true, media_playlist_->GetAC4ImsFlag());
}

TEST_F(MediaPlaylistMultiSegmentTest, GetAC4CbiFlag) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  // Returns false by default if not audio.
  EXPECT_EQ(false, media_playlist_->GetAC4CbiFlag());

  media_info.mutable_audio_info()->mutable_codec_specific_data()->
    set_ac4_cbi_flag(false);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(false, media_playlist_->GetAC4CbiFlag());

  media_info.mutable_audio_info()->mutable_codec_specific_data()->
    set_ac4_cbi_flag(true);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(true, media_playlist_->GetAC4CbiFlag());
}

TEST_F(MediaPlaylistMultiSegmentTest, Characteristics) {
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);

  static const char* kCharacteristics[] = {"some.characteristic",
                                           "another.characteristic"};

  media_info.add_hls_characteristics(kCharacteristics[0]);
  media_info.add_hls_characteristics(kCharacteristics[1]);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_THAT(media_playlist_->characteristics(),
              ElementsAreArray(kCharacteristics));
}

TEST_F(MediaPlaylistMultiSegmentTest, InitSegment) {
  valid_video_media_info_.set_reference_time_scale(90000);
  valid_video_media_info_.set_init_segment_url("init_segment.mp4");
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.mp4", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.mp4", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);

  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-MAP:URI=\"init_segment.mp4\"\n"
      "#EXTINF:10.000,\n"
      "file1.mp4\n"
      "#EXTINF:30.000,\n"
      "file2.mp4\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

// Verify that kSampleAesCenc is handled correctly.
TEST_F(MediaPlaylistMultiSegmentTest, SampleAesCenc) {
  valid_video_media_info_.set_reference_time_scale(90000);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAesCenc, "http://example.com", "",
      "0x12345678", "com.widevine", "1/2/4");

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES-CTR,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(MediaPlaylistMultiSegmentTest, MultipleEncryptionInfo) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0x12345678", "com.widevine", "1/2/4");

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedc", "0x12345678", "com.widevine.someother", "1");

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:30\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfedc,IV=0x12345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:30.000,\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

class LiveMediaPlaylistTest : public MediaPlaylistMultiSegmentTest {
 protected:
  LiveMediaPlaylistTest()
      : MediaPlaylistMultiSegmentTest(HlsPlaylistType::kLive) {}
};

TEST_F(LiveMediaPlaylistTest, Basic) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(LiveMediaPlaylistTest, TimeShifted) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);
  media_playlist_->AddSegment("file3.ts", 30 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-MEDIA-SEQUENCE:1\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n"
      "#EXTINF:20.000,\n"
      "file3.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(LiveMediaPlaylistTest, TimeShiftedWithEncryptionInfo) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0x12345678", "com.widevine", "1/2/4");
  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedc", "0x12345678", "com.widevine.someother", "1");

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);
  media_playlist_->AddSegment("file3.ts", 30 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-MEDIA-SEQUENCE:1\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x12345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfedc,IV=0x12345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n"
      "#EXTINF:20.000,\n"
      "file3.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(LiveMediaPlaylistTest, TimeShiftedWithEncryptionInfoShifted) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0x12345678", "com.widevine", "1/2/4");
  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedc", "0x12345678", "com.widevine.someother", "1");

  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0x22345678", "com.widevine", "1/2/4");
  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfedd", "0x22345678", "com.widevine.someother", "1");

  media_playlist_->AddSegment("file3.ts", 30 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);

  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://example.com", "",
      "0x32345678", "com.widevine", "1/2/4");
  media_playlist_->AddEncryptionInfo(
      MediaPlaylist::EncryptionMethod::kSampleAes, "http://mydomain.com",
      "0xfede", "0x32345678", "com.widevine.someother", "1");

  media_playlist_->AddSegment("file4.ts", 50 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-MEDIA-SEQUENCE:2\n"
      "#EXT-X-DISCONTINUITY-SEQUENCE:1\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x22345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfedd,IV=0x22345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:20.000,\n"
      "file3.ts\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://example.com\",IV=0x32345678,KEYFORMATVERSIONS=\"1/2/4\","
      "KEYFORMAT=\"com.widevine\"\n"
      "#EXT-X-KEY:METHOD=SAMPLE-AES,"
      "URI=\"http://mydomain.com\",KEYID=0xfede,IV=0x32345678,"
      "KEYFORMATVERSIONS=\"1\","
      "KEYFORMAT=\"com.widevine.someother\"\n"
      "#EXTINF:20.000,\n"
      "file4.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

class EventMediaPlaylistTest : public MediaPlaylistMultiSegmentTest {
 protected:
  EventMediaPlaylistTest()
      : MediaPlaylistMultiSegmentTest(HlsPlaylistType::kEvent) {}
};

TEST_F(EventMediaPlaylistTest, Basic) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 20 * kTimeScale,
                              kZeroByteOffset, 2 * kMBytes, kRefTime);
  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:20\n"
      "#EXT-X-PLAYLIST-TYPE:EVENT\n"
      "#EXTINF:10.000,\n"
      "file1.ts\n"
      "#EXTINF:20.000,\n"
      "file2.ts\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

class IFrameMediaPlaylistTest : public MediaPlaylistTest {};

TEST_F(IFrameMediaPlaylistTest, MediaPlaylistType) {
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  EXPECT_EQ(MediaPlaylist::MediaPlaylistStreamType::kVideo,
            media_playlist_->stream_type());
  media_playlist_->AddKeyFrame(0, 1000, 2345);
  // Playlist stream type is updated to I-Frames only after seeing
  // |AddKeyFrame|.
  EXPECT_EQ(MediaPlaylist::MediaPlaylistStreamType::kVideoIFramesOnly,
            media_playlist_->stream_type());
}

TEST_F(IFrameMediaPlaylistTest, SingleSegment) {
  valid_video_media_info_.set_media_file_url("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(0);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  media_playlist_->AddKeyFrame(0, 1000, 2345);
  media_playlist_->AddKeyFrame(2 * kTimeScale, 5000, 6345);
  media_playlist_->AddSegment("file.mp4", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddKeyFrame(11 * kTimeScale, kMBytes + 1000, 2345);
  media_playlist_->AddKeyFrame(15 * kTimeScale, kMBytes + 3345, 12345);
  media_playlist_->AddSegment("file.mp4", 10 * kTimeScale, 10 * kTimeScale,
                              1001000, 2 * kMBytes, kRefTime);

  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:9\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-I-FRAMES-ONLY\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"501@0\"\n"
      "#EXTINF:2.000,\n"
      "#EXT-X-BYTERANGE:2345@1000\n"
      "file.mp4\n"
      "#EXTINF:9.000,\n"
      "#EXT-X-BYTERANGE:6345@5000\n"
      "file.mp4\n"
      "#EXTINF:4.000,\n"
      "#EXT-X-BYTERANGE:2345@1001000\n"
      "file.mp4\n"
      "#EXTINF:5.000,\n"
      "#EXT-X-BYTERANGE:12345\n"
      "file.mp4\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(IFrameMediaPlaylistTest, SingleSegmentWithExtXProgramDateTime) {
  hls_params_.add_ext_x_program_date_time = true;
  base::Time reference_time;
  ASSERT_TRUE(base::Time::FromUTCString("2020-09-11 13:37:00.000 UTC",
                                        &reference_time));
  media_playlist_.reset(new MediaPlaylist(hls_params_, default_file_name_,
                                          default_name_, default_group_id_));

  valid_video_media_info_.set_media_file_url("file.mp4");
  valid_video_media_info_.mutable_init_range()->set_begin(0);
  valid_video_media_info_.mutable_init_range()->set_end(500);

  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  media_playlist_->AddKeyFrame(0, 1000, 2345);
  media_playlist_->AddKeyFrame(2 * kTimeScale, 5000, 6345);
  media_playlist_->AddSegment("file.mp4", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, reference_time);
  media_playlist_->AddKeyFrame(11 * kTimeScale, kMBytes + 1000, 2345);
  media_playlist_->AddKeyFrame(15 * kTimeScale, kMBytes + 3345, 12345);
  media_playlist_->AddSegment("file.mp4", 10 * kTimeScale, 10 * kTimeScale,
                              1001000, 2 * kMBytes, reference_time);

  const std::string kExpectedOutput = base::StringPrintf(
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:9\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-I-FRAMES-ONLY\n"
      "#EXT-X-MAP:URI=\"file.mp4\",BYTERANGE=\"501@0\"\n"
      "#EXT-X-PROGRAM-DATE-TIME:%s\n"
      "#EXTINF:2.000,\n"
      "#EXT-X-BYTERANGE:2345@1000\n"
      "file.mp4\n"
      "#EXTINF:9.000,\n"
      "#EXT-X-BYTERANGE:6345@5000\n"
      "file.mp4\n"
      "#EXTINF:4.000,\n"
      "#EXT-X-BYTERANGE:2345@1001000\n"
      "file.mp4\n"
      "#EXTINF:5.000,\n"
      "#EXT-X-BYTERANGE:12345\n"
      "file.mp4\n"
      "#EXT-X-ENDLIST\n",
          timeToString(reference_time).c_str());

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(IFrameMediaPlaylistTest, MultiSegment) {
  valid_video_media_info_.set_reference_time_scale(90000);
  valid_video_media_info_.set_segment_template_url("file$Number$.ts");
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddKeyFrame(0, 1000, 2345);
  media_playlist_->AddKeyFrame(2 * kTimeScale, 5000, 6345);
  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddKeyFrame(11 * kTimeScale, 1000, 2345);
  media_playlist_->AddKeyFrame(15 * kTimeScale, 3345, 12345);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);

  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:25\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-I-FRAMES-ONLY\n"
      "#EXTINF:2.000,\n"
      "#EXT-X-BYTERANGE:2345@1000\n"
      "file1.ts\n"
      "#EXTINF:9.000,\n"
      "#EXT-X-BYTERANGE:6345@5000\n"
      "file1.ts\n"
      "#EXTINF:4.000,\n"
      "#EXT-X-BYTERANGE:2345@1000\n"
      "file2.ts\n"
      "#EXTINF:25.000,\n"
      "#EXT-X-BYTERANGE:12345\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

TEST_F(IFrameMediaPlaylistTest, MultiSegmentWithPlacementOpportunity) {
  valid_video_media_info_.set_reference_time_scale(90000);
  valid_video_media_info_.set_segment_template_url("file$Number$.ts");
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

  media_playlist_->AddKeyFrame(0, 1000, 2345);
  media_playlist_->AddKeyFrame(2 * kTimeScale, 5000, 6345);
  media_playlist_->AddSegment("file1.ts", 0, 10 * kTimeScale, kZeroByteOffset,
                              kMBytes, kRefTime);
  media_playlist_->AddPlacementOpportunity();
  media_playlist_->AddKeyFrame(11 * kTimeScale, 1000, 2345);
  media_playlist_->AddKeyFrame(15 * kTimeScale, 3345, 12345);
  media_playlist_->AddSegment("file2.ts", 10 * kTimeScale, 30 * kTimeScale,
                              kZeroByteOffset, 5 * kMBytes, kRefTime);

  const char kExpectedOutput[] =
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "## Generated with https://github.com/google/shaka-packager version "
      "test\n"
      "#EXT-X-TARGETDURATION:25\n"
      "#EXT-X-PLAYLIST-TYPE:VOD\n"
      "#EXT-X-I-FRAMES-ONLY\n"
      "#EXTINF:2.000,\n"
      "#EXT-X-BYTERANGE:2345@1000\n"
      "file1.ts\n"
      "#EXTINF:9.000,\n"
      "#EXT-X-BYTERANGE:6345@5000\n"
      "file1.ts\n"
      "#EXT-X-PLACEMENT-OPPORTUNITY\n"
      "#EXTINF:4.000,\n"
      "#EXT-X-BYTERANGE:2345@1000\n"
      "file2.ts\n"
      "#EXTINF:25.000,\n"
      "#EXT-X-BYTERANGE:12345\n"
      "file2.ts\n"
      "#EXT-X-ENDLIST\n";

  const char kMemoryFilePath[] = "memory://media.m3u8";
  EXPECT_TRUE(media_playlist_->WriteToFile(kMemoryFilePath));
  ASSERT_FILE_STREQ(kMemoryFilePath, kExpectedOutput);
}

namespace {
const int kNumPreservedSegmentsOutsideLiveWindow = 3;
const int kMaxNumSegmentsAvailable =
    kTimeShiftBufferDepth + 1 + kNumPreservedSegmentsOutsideLiveWindow;

const char kSegmentTemplateNumber[] = "memory://$Number$.mp4";
const char kSegmentTemplateNumberUrl[] = "video/$Number$.mp4";
const char kStringPrintTemplate[] = "memory://%d.mp4";
const char kIgnoredSegmentName[] = "ignored_segment_name";

const char kSegmentTemplateTime[] = "memory://$Time$.mp4";
const char kSegmentTemplateTimeUrl[] = "video/$Time$.mp4";

const uint64_t kInitialStartTime = 0;
const uint64_t kDuration = kTimeScale;
}  // namespace

class MediaPlaylistDeleteSegmentsTest
    : public LiveMediaPlaylistTest,
      public WithParamInterface<std::pair<std::string, std::string>> {
 public:
  void SetUp() override {
    LiveMediaPlaylistTest::SetUp();

    std::tie(segment_template_, segment_template_url_) = GetParam();

    // Create 100 files with the template.
    for (int i = 0; i < 100; ++i) {
      File::WriteStringToFile(GetSegmentName(i).c_str(), "dummy content");
    }

    valid_video_media_info_.set_segment_template(segment_template_);
    valid_video_media_info_.set_segment_template_url(segment_template_url_);
    ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));

    mutable_hls_params()->preserved_segments_outside_live_window =
        kNumPreservedSegmentsOutsideLiveWindow;
  }

  int GetTime(int index) const { return kInitialStartTime + index * kDuration; }

  std::string GetSegmentName(int index) {
    if (segment_template_.find("$Time$") != std::string::npos)
      return base::StringPrintf(kStringPrintTemplate, GetTime(index));
    return base::StringPrintf(kStringPrintTemplate, index + 1);
  }

  bool SegmentDeleted(const std::string& segment_name) {
    std::unique_ptr<File, FileCloser> file_closer(
        File::Open(segment_name.c_str(), "r"));
    return file_closer.get() == nullptr;
  }

 private:
  std::string segment_template_;
  std::string segment_template_url_;
};

// Verify that no segments are deleted initially until there are more than
// |kMaxNumSegmentsAvailable| segments.
TEST_P(MediaPlaylistDeleteSegmentsTest, NoSegmentsDeletedInitially) {
  for (int i = 0; i < kMaxNumSegmentsAvailable; ++i) {
    media_playlist_->AddSegment(kIgnoredSegmentName, GetTime(i), kDuration,
                                kZeroByteOffset, kMBytes, kRefTime);
  }
  for (int i = 0; i < kMaxNumSegmentsAvailable; ++i) {
    EXPECT_FALSE(SegmentDeleted(GetSegmentName(i)));
  }
}

TEST_P(MediaPlaylistDeleteSegmentsTest, OneSegmentDeleted) {
  for (int i = 0; i <= kMaxNumSegmentsAvailable; ++i) {
    media_playlist_->AddSegment(kIgnoredSegmentName, GetTime(i), kDuration,
                                kZeroByteOffset, kMBytes, kRefTime);
  }
  EXPECT_FALSE(SegmentDeleted(GetSegmentName(1)));
  EXPECT_TRUE(SegmentDeleted(GetSegmentName(0)));
}

TEST_P(MediaPlaylistDeleteSegmentsTest, ManySegments) {
  int many_segments = 50;
  for (int i = 0; i < many_segments; ++i) {
    media_playlist_->AddSegment(kIgnoredSegmentName, GetTime(i), kDuration,
                                kZeroByteOffset, kMBytes, kRefTime);
  }
  const int last_available_segment_index =
      many_segments - kMaxNumSegmentsAvailable;
  EXPECT_FALSE(SegmentDeleted(GetSegmentName(last_available_segment_index)));
  EXPECT_TRUE(SegmentDeleted(GetSegmentName(last_available_segment_index - 1)));
}

INSTANTIATE_TEST_CASE_P(
    TimeOrNumber,
    MediaPlaylistDeleteSegmentsTest,
    Values(std::make_pair(kSegmentTemplateNumber, kSegmentTemplateNumberUrl),
           std::make_pair(kSegmentTemplateTime, kSegmentTemplateTimeUrl)));

class MediaPlaylistCodecTest
    : public MediaPlaylistTest,
      public WithParamInterface<std::pair<std::string, std::string>> {};

TEST_P(MediaPlaylistCodecTest, AdjustVideoCodec) {
  std::string input_codec, expected_output_codec;
  std::tie(input_codec, expected_output_codec) = GetParam();

  valid_video_media_info_.mutable_video_info()->set_codec(input_codec);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(valid_video_media_info_));
  ASSERT_EQ(media_playlist_->codec(), expected_output_codec);
}

INSTANTIATE_TEST_CASE_P(
    Codecs,
    MediaPlaylistCodecTest,
    Values(std::make_pair("avc1.4d401e", "avc1.4d401e"),
           // Replace avc3 with avc1.
           std::make_pair("avc3.4d401e", "avc1.4d401e"),
           std::make_pair("hvc1.2.4.L63.90", "hvc1.2.4.L63.90"),
           // Replace hev1 with hvc1.
           std::make_pair("hev1.2.4.L63.90", "hvc1.2.4.L63.90"),
           std::make_pair("dvh1.05.08", "dvh1.05.08"),
           // Replace dvhe with dvh1.
           std::make_pair("dvhe.05.08", "dvh1.05.08")));

struct VideoRangeTestData {
  std::string codec;
  int transfer_characteristics;
  std::string expected_video_range;
};

class MediaPlaylistVideoRangeTest
    : public MediaPlaylistTest,
      public WithParamInterface<VideoRangeTestData> {};

TEST_P(MediaPlaylistVideoRangeTest, GetVideoRange) {
  const VideoRangeTestData& test_data = GetParam();
  MediaInfo media_info;
  media_info.set_reference_time_scale(kTimeScale);
  MediaInfo::VideoInfo* video_info = media_info.mutable_video_info();
  video_info->set_codec(test_data.codec);
  video_info->set_transfer_characteristics(test_data.transfer_characteristics);
  ASSERT_TRUE(media_playlist_->SetMediaInfo(media_info));
  EXPECT_EQ(test_data.expected_video_range, media_playlist_->GetVideoRange());
}

INSTANTIATE_TEST_CASE_P(VideoRanges,
                        MediaPlaylistVideoRangeTest,
                        Values(VideoRangeTestData{"hvc1.2.4.L63.90", 0, ""},
                               VideoRangeTestData{"hvc1.2.4.L63.90", 1, "SDR"},
                               VideoRangeTestData{"hvc1.2.4.L63.90", 16, "PQ"},
                               VideoRangeTestData{"hvc1.2.4.L63.90", 18, "PQ"},
                               VideoRangeTestData{"dvh1.05.08", 0, "PQ"}));

}  // namespace hls
}  // namespace shaka
