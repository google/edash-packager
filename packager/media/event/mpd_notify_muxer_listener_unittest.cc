// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/mpd_notify_muxer_listener.h"

#include <gmock/gmock.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#include "packager/base/stl_util.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/event/muxer_listener_test_helper.h"
#include "packager/mpd/base/content_protection_element.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/mock_mpd_notifier.h"
#include "packager/mpd/base/mpd_notifier.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

namespace shaka {

namespace {

// Can be any string, we just want to check that it is preserved in the
// protobuf.
const char kDefaultKeyId[] = "defaultkeyid";
const bool kInitialEncryptionInfo = true;
const bool kNonInitialEncryptionInfo = false;

// TODO(rkuroiwa): This is copied from mpd_builder_test_helper.cc. Make a
// common target that only has mpd_builder_test_helper and its dependencies
// so the two test targets can share this.
MediaInfo ConvertToMediaInfo(const std::string& media_info_string) {
  MediaInfo media_info;
  CHECK(::google::protobuf::TextFormat::ParseFromString(media_info_string,
                                                        &media_info));
  return media_info;
}

void SetDefaultLiveMuxerOptions(media::MuxerOptions* muxer_options) {
  muxer_options->output_file_name = "liveinit.mp4";
  muxer_options->segment_template = "live-$NUMBER$.mp4";
  muxer_options->temp_dir.clear();
}

const uint8_t kBogusIv[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x67, 0x83, 0xC3, 0x66, 0xEE, 0xAB, 0xB2, 0xF1,
};

}  // namespace

namespace media {

class MpdNotifyMuxerListenerTest : public ::testing::TestWithParam<MpdType> {
 public:

  void SetupForVod() {
    MpdOptions mpd_options;
    mpd_options.dash_profile = DashProfile::kOnDemand;
    // On-demand profile should be static.
    mpd_options.mpd_type = MpdType::kStatic;
    notifier_.reset(new MockMpdNotifier(mpd_options));
    listener_.reset(
        new MpdNotifyMuxerListener(notifier_.get()));
  }

  void SetupForLive() {
    MpdOptions mpd_options;
    mpd_options.dash_profile = DashProfile::kLive;
    // Live profile can be static or dynamic.
    mpd_options.mpd_type = GetParam();
    notifier_.reset(new MockMpdNotifier(mpd_options));
    listener_.reset(new MpdNotifyMuxerListener(notifier_.get()));
  }

  void FireOnMediaEndWithParams(const OnMediaEndParameters& params) {
    // On success, this writes the result to |temp_file_path_|.
    listener_->OnMediaEnd(params.media_ranges, params.duration_seconds);
  }

  std::unique_ptr<MpdNotifyMuxerListener> listener_;
  std::unique_ptr<MockMpdNotifier> notifier_;
};

MATCHER_P(EqualsProto, message, "") {
  *result_listener << arg.ShortDebugString();
  return ::google::protobuf::util::MessageDifferencer::Equals(arg, message);
}

MATCHER_P(ExpectMediaInfoEq, expected_text_format, "") {
  const MediaInfo expected = ConvertToMediaInfo(expected_text_format);
  *result_listener << arg.ShortDebugString();
  return ::google::protobuf::util::MessageDifferencer::Equals(arg, expected);
}

TEST_F(MpdNotifyMuxerListenerTest, VodClearContent) {
  SetupForVod();
  MuxerOptions muxer_options;
  SetDefaultMuxerOptions(&muxer_options);
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  EXPECT_CALL(*notifier_, NotifyNewContainer(_, _)).Times(0);
  listener_->OnMediaStart(muxer_options, *video_stream_info,
                          kDefaultReferenceTimeScale,
                          MuxerListener::kContainerMp4);
  ::testing::Mock::VerifyAndClearExpectations(notifier_.get());

  EXPECT_CALL(*notifier_, NotifyNewContainer(
                              ExpectMediaInfoEq(kExpectedDefaultMediaInfo), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*notifier_, Flush());
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());
}

// default_key_id and pssh are converted to string because when std::equal
// compares a negative char and uint8_t > 127, it considers them not equal.
MATCHER_P4(ProtectedContentEq, uuid, name, default_key_id, pssh, "") {
  const MediaInfo& actual_media_info = arg;
  EXPECT_TRUE(actual_media_info.has_protected_content());
  EXPECT_EQ(
      1,
      actual_media_info.protected_content().content_protection_entry().size());
  const std::string& actual_default_kid =
      actual_media_info.protected_content().default_key_id();
  const std::string expected_default_kid_string(default_key_id.begin(),
                                                default_key_id.end());
  if (actual_default_kid != expected_default_kid_string) {
    return false;
  }

  const MediaInfo_ProtectedContent_ContentProtectionEntry& entry =
      actual_media_info.protected_content().content_protection_entry(0);

  const std::string expcted_pssh_string(pssh.begin(), pssh.end());
  return entry.uuid() == uuid && entry.name_version() == name &&
         entry.pssh().size() == pssh.size() &&
         entry.pssh() == expcted_pssh_string;
}

TEST_F(MpdNotifyMuxerListenerTest, VodEncryptedContent) {
  SetupForVod();
  MuxerOptions muxer_options;
  SetDefaultMuxerOptions(&muxer_options);
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  const std::vector<uint8_t> default_key_id(
      kDefaultKeyId, kDefaultKeyId + arraysize(kDefaultKeyId) - 1);

  const std::string kExpectedMediaInfo =
      std::string(kExpectedDefaultMediaInfo) +
      "protected_content {\n"
      "  protection_scheme: 'cenc'\n"
      "  content_protection_entry {\n"
      "    uuid: '00010203-0405-0607-0809-0a0b0c0d0e0f'\n"
      "    pssh: '" + std::string(kExpectedDefaultPsshBox) + "'\n"
      "  }\n"
      "  default_key_id: 'defaultkeyid'\n"
      "  include_mspr_pro: 1\n"
      "}\n";

  EXPECT_CALL(*notifier_, NotifyNewContainer(_, _)).Times(0);

  std::vector<uint8_t> iv(kBogusIv, kBogusIv + arraysize(kBogusIv));
  listener_->OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cenc,
                                   default_key_id, iv,
                                   GetDefaultKeySystemInfo());

  listener_->OnMediaStart(muxer_options, *video_stream_info,
                          kDefaultReferenceTimeScale,
                          MuxerListener::kContainerMp4);
  ::testing::Mock::VerifyAndClearExpectations(notifier_.get());

  EXPECT_CALL(*notifier_,
              NotifyNewContainer(ExpectMediaInfoEq(kExpectedMediaInfo), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*notifier_, Flush());
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());
}

// Verify that calling OnSampleDurationReady() sets the frame duration in the
// media info, and the media info gets passed to NotifyNewContainer() with
// frame_duration == sample_duration.
TEST_F(MpdNotifyMuxerListenerTest, VodOnSampleDurationReady) {
  SetupForVod();
  MuxerOptions muxer_options;
  SetDefaultMuxerOptions(&muxer_options);
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  const uint32_t kSampleDuration = 1234u;
  const char kExpectedMediaInfo[] =
      "video_info {\n"
      "  frame_duration: 1234\n"  // Should match the constant above.
      "  codec: 'avc1.010101'\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "init_range {\n"
      "  begin: 0\n"
      "  end: 120\n"
      "}\n"
      "index_range {\n"
      "  begin: 121\n"
      "  end: 221\n"
      "}\n"
      "reference_time_scale: 1111\n"
      "container_type: 1\n"
      "media_file_name: 'test_output_file_name.mp4'\n"
      "media_duration_seconds: 10.5\n";
  const uint32_t kReferenceTimeScale = 1111u;  // Should match the protobuf.

  EXPECT_CALL(*notifier_, NotifyNewContainer(_, _)).Times(0);
  listener_->OnMediaStart(muxer_options, *video_stream_info,
                          kReferenceTimeScale, MuxerListener::kContainerMp4);
  listener_->OnSampleDurationReady(kSampleDuration);
  ::testing::Mock::VerifyAndClearExpectations(notifier_.get());

  EXPECT_CALL(*notifier_,
              NotifyNewContainer(ExpectMediaInfoEq(kExpectedMediaInfo), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*notifier_, Flush());
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());
}

// Verify that MpdNotifier::NotifyNewSegment() is called after
// NotifyNewContainer(), if OnNewSegment() is called.
TEST_F(MpdNotifyMuxerListenerTest, VodOnNewSegment) {
  SetupForVod();
  MuxerOptions muxer_options;
  SetDefaultMuxerOptions(&muxer_options);
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  const uint64_t kStartTime1 = 0u;
  const uint64_t kDuration1 = 1000u;
  const uint64_t kSegmentFileSize1 = 29812u;
  const int64_t kSegmentNumber1 = 1;
  const uint64_t kStartTime2 = 1001u;
  const uint64_t kDuration2 = 3787u;
  const uint64_t kSegmentFileSize2 = 83743u;
  const int64_t kSegmentNumber2 = 2;

  EXPECT_CALL(*notifier_, NotifyNewContainer(_, _)).Times(0);
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, _, _, _, _)).Times(0);
  listener_->OnMediaStart(muxer_options, *video_stream_info,
                          kDefaultReferenceTimeScale,
                          MuxerListener::kContainerMp4);
  listener_->OnNewSegment("", kStartTime1, kDuration1, kSegmentFileSize1,
                          kSegmentNumber1);
  listener_->OnCueEvent(kStartTime2, "dummy cue data");
  listener_->OnNewSegment("", kStartTime2, kDuration2, kSegmentFileSize2,
                          kSegmentNumber2);
  ::testing::Mock::VerifyAndClearExpectations(notifier_.get());

  InSequence s;
  EXPECT_CALL(*notifier_, NotifyNewContainer(
                              ExpectMediaInfoEq(kExpectedDefaultMediaInfo), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime1, kDuration1,
                                           kSegmentFileSize1, kSegmentNumber1));
  EXPECT_CALL(*notifier_, NotifyCueEvent(_, kStartTime2));
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime2, kDuration2,
                                           kSegmentFileSize2, kSegmentNumber2));
  EXPECT_CALL(*notifier_, Flush());
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());
}

// Verify the event handling with multiple files, i.e. multiple OnMediaStart and
// OnMediaEnd calls.
TEST_F(MpdNotifyMuxerListenerTest, VodMultipleFiles) {
  SetupForVod();
  MuxerOptions muxer_options1;
  SetDefaultMuxerOptions(&muxer_options1);
  muxer_options1.output_file_name = "test_output1.mp4";
  MuxerOptions muxer_options2 = muxer_options1;
  muxer_options2.output_file_name = "test_output2.mp4";

  MediaInfo expected_media_info1 =
      ConvertToMediaInfo(kExpectedDefaultMediaInfo);
  expected_media_info1.set_media_file_name("test_output1.mp4");
  MediaInfo expected_media_info2 = expected_media_info1;
  expected_media_info2.set_media_file_name("test_output2.mp4");

  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  const uint64_t kStartTime1 = 0u;
  const uint64_t kDuration1 = 1000u;
  const uint64_t kSegmentFileSize1 = 29812u;
  const int64_t kSegmentNumber1 = 1;
  const uint64_t kStartTime2 = 1001u;
  const uint64_t kDuration2 = 3787u;
  const uint64_t kSegmentFileSize2 = 83743u;
  const int64_t kSegmentNumber2 = 2;

  // Expectation for first file before OnMediaEnd.
  EXPECT_CALL(*notifier_, NotifyNewContainer(_, _)).Times(0);
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, _, _, _, _)).Times(0);
  listener_->OnMediaStart(muxer_options1, *video_stream_info,
                          kDefaultReferenceTimeScale,
                          MuxerListener::kContainerMp4);
  listener_->OnNewSegment("", kStartTime1, kDuration1, kSegmentFileSize1,
                          kSegmentNumber1);
  listener_->OnCueEvent(kStartTime2, "dummy cue data");
  ::testing::Mock::VerifyAndClearExpectations(notifier_.get());

  // Expectation for first file OnMediaEnd.
  InSequence s;
  EXPECT_CALL(*notifier_,
              NotifyNewContainer(EqualsProto(expected_media_info1), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime1, kDuration1,
                                           kSegmentFileSize1, kSegmentNumber1));
  EXPECT_CALL(*notifier_, NotifyCueEvent(_, kStartTime2));
  EXPECT_CALL(*notifier_, Flush());
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());

  // Expectation for second file before OnMediaEnd.
  listener_->OnMediaStart(muxer_options2, *video_stream_info,
                          kDefaultReferenceTimeScale,
                          MuxerListener::kContainerMp4);
  listener_->OnNewSegment("", kStartTime2, kDuration2, kSegmentFileSize2,
                          kSegmentNumber2);

  // Expectation for second file OnMediaEnd.
  EXPECT_CALL(*notifier_,
              NotifyMediaInfoUpdate(_, EqualsProto(expected_media_info2)));
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime2, kDuration2,
                                           kSegmentFileSize2, kSegmentNumber2));
  EXPECT_CALL(*notifier_, Flush());
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());
}

// Live without key rotation. Note that OnEncryptionInfoReady() is called before
// OnMediaStart() but no more calls.
TEST_P(MpdNotifyMuxerListenerTest, LiveNoKeyRotation) {
  SetupForLive();
  MuxerOptions muxer_options;
  SetDefaultLiveMuxerOptions(&muxer_options);
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  const std::string kExpectedMediaInfo =
      "video_info {\n"
      "  codec: \"avc1.010101\"\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "media_duration_seconds: 20.0\n"
      "init_segment_name: \"liveinit.mp4\"\n"
      "segment_template: \"live-$NUMBER$.mp4\"\n"
      "reference_time_scale: 1000\n"
      "container_type: CONTAINER_MP4\n"
      "protected_content {\n"
      "  default_key_id: \"defaultkeyid\"\n"
      "  content_protection_entry {\n"
      "    uuid: '00010203-0405-0607-0809-0a0b0c0d0e0f'\n"
      "    pssh: \"" + std::string(kExpectedDefaultPsshBox) + "\"\n"
      "  }\n"
      "  protection_scheme: 'cbcs'\n"
      "  include_mspr_pro: 1\n"
      "}\n";

  const uint64_t kStartTime1 = 0u;
  const uint64_t kDuration1 = 1000u;
  const uint64_t kSegmentFileSize1 = 29812u;
  const int64_t kSegmentNumber1 = 1;
  const uint64_t kStartTime2 = 1001u;
  const uint64_t kDuration2 = 3787u;
  const uint64_t kSegmentFileSize2 = 83743u;
  const int64_t kSegmentNumber2 = 2;

  const std::vector<uint8_t> default_key_id(
      kDefaultKeyId, kDefaultKeyId + arraysize(kDefaultKeyId) - 1);

  InSequence s;
  EXPECT_CALL(*notifier_, NotifyEncryptionUpdate(_, _, _, _)).Times(0);
  EXPECT_CALL(*notifier_,
              NotifyNewContainer(ExpectMediaInfoEq(kExpectedMediaInfo), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime1, kDuration1,
                                           kSegmentFileSize1, kSegmentNumber1));
  // Flush should only be called once in OnMediaEnd.
  if (GetParam() == MpdType::kDynamic)
    EXPECT_CALL(*notifier_, Flush());
  EXPECT_CALL(*notifier_, NotifyCueEvent(_, kStartTime2));
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime2, kDuration2,
                                           kSegmentFileSize2, kSegmentNumber2));
  if (GetParam() == MpdType::kDynamic)
    EXPECT_CALL(*notifier_, Flush());

  std::vector<uint8_t> iv(kBogusIv, kBogusIv + arraysize(kBogusIv));
  listener_->OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbcs,
                                   default_key_id, iv,
                                   GetDefaultKeySystemInfo());
  listener_->OnMediaStart(muxer_options, *video_stream_info,
                          kDefaultReferenceTimeScale,
                          MuxerListener::kContainerMp4);
  listener_->OnNewSegment("", kStartTime1, kDuration1, kSegmentFileSize1,
                          kSegmentNumber1);
  listener_->OnCueEvent(kStartTime2, "dummy cue data");
  listener_->OnNewSegment("", kStartTime2, kDuration2, kSegmentFileSize2,
                          kSegmentNumber2);
  ::testing::Mock::VerifyAndClearExpectations(notifier_.get());

  EXPECT_CALL(*notifier_, Flush())
      .Times(GetParam() == MpdType::kDynamic ? 0 : 1);
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());
}

// Live with key rotation. Note that OnEncryptionInfoReady() is called before
// and after OnMediaStart().
TEST_P(MpdNotifyMuxerListenerTest, LiveWithKeyRotation) {
  SetupForLive();
  MuxerOptions muxer_options;
  SetDefaultLiveMuxerOptions(&muxer_options);
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  // Note that this media info has protected_content with default key id.
  const char kExpectedMediaInfo[] =
      "video_info {\n"
      "  codec: \"avc1.010101\"\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  pixel_width: 1\n"
      "  pixel_height: 1\n"
      "}\n"
      "media_duration_seconds: 20.0\n"
      "init_segment_name: \"liveinit.mp4\"\n"
      "segment_template: \"live-$NUMBER$.mp4\"\n"
      "reference_time_scale: 1000\n"
      "container_type: CONTAINER_MP4\n"
      "protected_content {\n"
      "  default_key_id: \"defaultkeyid\"\n"
      "  protection_scheme: 'cbc1'\n"
      "  include_mspr_pro: 1\n"
      "}\n";

  const uint64_t kStartTime1 = 0u;
  const uint64_t kDuration1 = 1000u;
  const uint64_t kSegmentFileSize1 = 29812u;
  const int64_t kSegmentNumber1 = 1;
  const uint64_t kStartTime2 = 1001u;
  const uint64_t kDuration2 = 3787u;
  const uint64_t kSegmentFileSize2 = 83743u;
  const int64_t kSegmentNumber2 = 2;

  const std::vector<uint8_t> default_key_id(
      kDefaultKeyId, kDefaultKeyId + arraysize(kDefaultKeyId) - 1);

  InSequence s;
  EXPECT_CALL(*notifier_,
              NotifyNewContainer(ExpectMediaInfoEq(kExpectedMediaInfo), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*notifier_, NotifyEncryptionUpdate(_, _, _, _)).Times(1);
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime1, kDuration1,
                                           kSegmentFileSize1, kSegmentNumber1));
  // Flush should only be called once in OnMediaEnd.
  if (GetParam() == MpdType::kDynamic)
    EXPECT_CALL(*notifier_, Flush());
  EXPECT_CALL(*notifier_, NotifyNewSegment(_, kStartTime2, kDuration2,
                                           kSegmentFileSize2, kSegmentNumber2));
  if (GetParam() == MpdType::kDynamic)
    EXPECT_CALL(*notifier_, Flush());

  std::vector<uint8_t> iv(kBogusIv, kBogusIv + arraysize(kBogusIv));
  listener_->OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbc1,
                                   default_key_id, iv,
                                   std::vector<ProtectionSystemSpecificInfo>());
  listener_->OnMediaStart(muxer_options, *video_stream_info,
                          kDefaultReferenceTimeScale,
                          MuxerListener::kContainerMp4);
  listener_->OnEncryptionInfoReady(kNonInitialEncryptionInfo, FOURCC_cbc1,
                                   std::vector<uint8_t>(), iv,
                                   GetDefaultKeySystemInfo());
  listener_->OnNewSegment("", kStartTime1, kDuration1, kSegmentFileSize1,
                          kSegmentNumber1);
  listener_->OnNewSegment("", kStartTime2, kDuration2, kSegmentFileSize2,
                          kSegmentNumber2);
  ::testing::Mock::VerifyAndClearExpectations(notifier_.get());

  EXPECT_CALL(*notifier_, Flush())
      .Times(GetParam() == MpdType::kDynamic ? 0 : 1);
  FireOnMediaEndWithParams(GetDefaultOnMediaEndParams());
}

INSTANTIATE_TEST_CASE_P(StaticAndDynamic,
                        MpdNotifyMuxerListenerTest,
                        ::testing::Values(MpdType::kStatic, MpdType::kDynamic));

}  // namespace media
}  // namespace shaka
