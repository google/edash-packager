// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>

#include <vector>

#include "packager/base/files/file_path.h"
#include "packager/base/files/file_util.h"
#include "packager/file/file.h"
#include "packager/media/base/fourccs.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/event/muxer_listener_test_helper.h"
#include "packager/media/event/vod_media_info_dump_muxer_listener.h"
#include "packager/mpd/base/media_info.pb.h"

namespace {
const bool kEnableEncryption = true;
// '_default_key_id_' (length 16).
const uint8_t kBogusDefaultKeyId[] = {0x5f, 0x64, 0x65, 0x66, 0x61, 0x75,
                                      0x6c, 0x74, 0x5f, 0x6b, 0x65, 0x79,
                                      0x5f, 0x69, 0x64, 0x5f};

const uint8_t kBogusIv[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x67, 0x83, 0xC3, 0x66, 0xEE, 0xAB, 0xB2, 0xF1,
};

const bool kInitialEncryptionInfo = true;
}  // namespace

namespace shaka {
namespace media {

namespace {

MATCHER_P(FileContentEqualsProto, expected_protobuf, "") {
  std::string temp_file_media_info_str;
  CHECK(File::ReadFileToString(arg.c_str(), &temp_file_media_info_str));
  CHECK(!temp_file_media_info_str.empty());

  MediaInfo expected_media_info;
  MediaInfo actual_media_info;
  typedef ::google::protobuf::TextFormat TextFormat;
  CHECK(TextFormat::ParseFromString(expected_protobuf, &expected_media_info));
  CHECK(TextFormat::ParseFromString(temp_file_media_info_str,
                                    &actual_media_info));

  *result_listener << actual_media_info.ShortDebugString();

  return ::google::protobuf::util::MessageDifferencer::Equals(
      actual_media_info, expected_media_info);
}

}  // namespace

class VodMediaInfoDumpMuxerListenerTest : public ::testing::Test {
 public:
  VodMediaInfoDumpMuxerListenerTest() {}
  ~VodMediaInfoDumpMuxerListenerTest() override {}

  void SetUp() override {
    ASSERT_TRUE(base::CreateTemporaryFile(&temp_file_path_));
    DLOG(INFO) << "Created temp file: " << temp_file_path_.value();

    listener_.reset(new VodMediaInfoDumpMuxerListener(temp_file_path_
                  .AsUTF8Unsafe()));
  }

  void TearDown() override {
    base::DeleteFile(temp_file_path_, false);
  }

  void FireOnMediaStartWithDefaultMuxerOptions(
      const StreamInfo& stream_info,
      bool enable_encryption) {
    MuxerOptions muxer_options;
    SetDefaultMuxerOptions(&muxer_options);
    const uint32_t kReferenceTimeScale = 1000;
    if (enable_encryption) {
      std::vector<uint8_t> bogus_default_key_id(
          kBogusDefaultKeyId,
          kBogusDefaultKeyId + arraysize(kBogusDefaultKeyId));
      std::vector<uint8_t> bogus_iv(kBogusIv, kBogusIv + arraysize(kBogusIv));

      listener_->OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cenc,
                                       bogus_default_key_id, bogus_iv,
                                       GetDefaultKeySystemInfo());
    }
    listener_->OnMediaStart(muxer_options, stream_info, kReferenceTimeScale,
                            MuxerListener::kContainerMp4);
  }

  void FireOnNewSegmentWithParams(const OnNewSegmentParameters& params) {
    listener_->OnNewSegment(params.file_name, params.start_time,
                            params.duration, params.segment_file_size, 0);
  }

  void FireOnMediaEndWithParams(const OnMediaEndParameters& params) {
    // On success, this writes the result to |temp_file_path_|.
    listener_->OnMediaEnd(params.media_ranges, params.duration_seconds);
  }

 protected:
  base::FilePath temp_file_path_;
  std::unique_ptr<VodMediaInfoDumpMuxerListener> listener_;

 private:
  DISALLOW_COPY_AND_ASSIGN(VodMediaInfoDumpMuxerListenerTest);
};

TEST_F(VodMediaInfoDumpMuxerListenerTest, UnencryptedStream_Normal) {
  std::shared_ptr<StreamInfo> stream_info =
      CreateVideoStreamInfo(GetDefaultVideoStreamInfoParams());

  FireOnMediaStartWithDefaultMuxerOptions(*stream_info, !kEnableEncryption);
  OnMediaEndParameters media_end_param = GetDefaultOnMediaEndParams();
  FireOnMediaEndWithParams(media_end_param);

  const char kExpectedProtobufOutput[] =
      "bandwidth: 0\n"
      "video_info {\n"
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
      "reference_time_scale: 1000\n"
      "container_type: 1\n"
      "media_file_name: 'test_output_file_name.mp4'\n"
      "media_duration_seconds: 10.5\n";
  EXPECT_THAT(temp_file_path_.AsUTF8Unsafe(),
              FileContentEqualsProto(kExpectedProtobufOutput));
}

TEST_F(VodMediaInfoDumpMuxerListenerTest, EncryptedStream_Normal) {
  std::shared_ptr<StreamInfo> stream_info =
      CreateVideoStreamInfo(GetDefaultVideoStreamInfoParams());
  FireOnMediaStartWithDefaultMuxerOptions(*stream_info, kEnableEncryption);
  OnMediaEndParameters media_end_param = GetDefaultOnMediaEndParams();
  FireOnMediaEndWithParams(media_end_param);

  const std::string kExpectedProtobufOutput =
      "bandwidth: 0\n"
      "video_info {\n"
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
      "reference_time_scale: 1000\n"
      "container_type: 1\n"
      "media_file_name: 'test_output_file_name.mp4'\n"
      "media_duration_seconds: 10.5\n"
      "protected_content {\n"
      "  content_protection_entry {\n"
      "    uuid: '00010203-0405-0607-0809-0a0b0c0d0e0f'\n"
      "    pssh: '" +
      std::string(kExpectedDefaultPsshBox) +
      "'\n"
      "  }\n"
      "  default_key_id: '_default_key_id_'\n"
      "  protection_scheme: 'cenc'\n"
      "}\n";

  EXPECT_THAT(temp_file_path_.AsUTF8Unsafe(),
              FileContentEqualsProto(kExpectedProtobufOutput));
}

// Verify that VideoStreamInfo with non-0 pixel_{width,height} is set in the
// generated MediaInfo.
TEST_F(VodMediaInfoDumpMuxerListenerTest, CheckPixelWidthAndHeightSet) {
  VideoStreamInfoParameters params = GetDefaultVideoStreamInfoParams();
  params.pixel_width = 8;
  params.pixel_height = 9;

  std::shared_ptr<StreamInfo> stream_info = CreateVideoStreamInfo(params);
  FireOnMediaStartWithDefaultMuxerOptions(*stream_info, !kEnableEncryption);
  OnMediaEndParameters media_end_param = GetDefaultOnMediaEndParams();
  FireOnMediaEndWithParams(media_end_param);

  const char kExpectedProtobufOutput[] =
      "bandwidth: 0\n"
      "video_info {\n"
      "  codec: 'avc1.010101'\n"
      "  width: 720\n"
      "  height: 480\n"
      "  time_scale: 10\n"
      "  pixel_width: 8\n"
      "  pixel_height: 9\n"
      "}\n"
      "init_range {\n"
      "  begin: 0\n"
      "  end: 120\n"
      "}\n"
      "index_range {\n"
      "  begin: 121\n"
      "  end: 221\n"
      "}\n"
      "reference_time_scale: 1000\n"
      "container_type: 1\n"
      "media_file_name: 'test_output_file_name.mp4'\n"
      "media_duration_seconds: 10.5\n";
  EXPECT_THAT(temp_file_path_.AsUTF8Unsafe(),
              FileContentEqualsProto(kExpectedProtobufOutput));
}

TEST_F(VodMediaInfoDumpMuxerListenerTest, CheckBandwidth) {
  VideoStreamInfoParameters params = GetDefaultVideoStreamInfoParams();

  std::shared_ptr<StreamInfo> stream_info = CreateVideoStreamInfo(params);
  FireOnMediaStartWithDefaultMuxerOptions(*stream_info, !kEnableEncryption);

  OnNewSegmentParameters new_segment_param;
  new_segment_param.segment_file_size = 100;
  new_segment_param.duration = 1000;
  FireOnNewSegmentWithParams(new_segment_param);
  new_segment_param.segment_file_size = 200;
  FireOnNewSegmentWithParams(new_segment_param);

  OnMediaEndParameters media_end_param = GetDefaultOnMediaEndParams();
  FireOnMediaEndWithParams(media_end_param);

  const char kExpectedProtobufOutput[] =
      "bandwidth: 1600\n"
      "video_info {\n"
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
      "reference_time_scale: 1000\n"
      "container_type: 1\n"
      "media_file_name: 'test_output_file_name.mp4'\n"
      "media_duration_seconds: 10.5\n";
  EXPECT_THAT(temp_file_path_.AsUTF8Unsafe(),
              FileContentEqualsProto(kExpectedProtobufOutput));
}

}  // namespace media
}  // namespace shaka
