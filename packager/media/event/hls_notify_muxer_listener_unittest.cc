// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/hls/base/hls_notifier.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/event/hls_notify_muxer_listener.h"
#include "packager/media/event/muxer_listener_test_helper.h"

namespace shaka {
namespace media {

using ::testing::_;
using ::testing::Bool;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::Property;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::TestWithParam;

namespace {

class MockHlsNotifier : public hls::HlsNotifier {
 public:
  MockHlsNotifier() : HlsNotifier(HlsParams()) {}

  MOCK_METHOD0(Init, bool());
  MOCK_METHOD5(NotifyNewStream,
               bool(const MediaInfo& media_info,
                    const std::string& playlist_name,
                    const std::string& name,
                    const std::string& group_id,
                    uint32_t* stream_id));
  MOCK_METHOD2(NotifySampleDuration,
               bool(uint32_t stream_id, uint32_t sample_duration));
  MOCK_METHOD6(NotifyNewSegment,
               bool(uint32_t stream_id,
                    const std::string& segment_name,
                    uint64_t start_time,
                    uint64_t duration,
                    uint64_t start_byte_offset,
                    uint64_t size));
  MOCK_METHOD4(NotifyKeyFrame,
               bool(uint32_t stream_id,
                    uint64_t timestamp,
                    uint64_t start_byte_offset,
                    uint64_t size));
  MOCK_METHOD2(NotifyCueEvent, bool(uint32_t stream_id, uint64_t timestamp));
  MOCK_METHOD5(
      NotifyEncryptionUpdate,
      bool(uint32_t stream_id,
           const std::vector<uint8_t>& key_id,
           const std::vector<uint8_t>& system_id,
           const std::vector<uint8_t>& iv,
           const std::vector<uint8_t>& protection_system_specific_data));
  MOCK_METHOD0(Flush, bool());
};

// Doesn't really matter what the values are as long as it is a system ID (16
// bytes).
const uint8_t kAnySystemId[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

const uint8_t kAnyData[] = {
  0xFF, 0x78, 0xAA, 0x6B,
};

const uint64_t kSegmentStartOffset = 10000;
const uint64_t kSegmentStartTime = 19283;
const uint64_t kSegmentDuration = 98028;
const uint64_t kSegmentSize = 756739;
const int64_t kAnySegmentNumber = 10;

const uint64_t kCueStartTime = kSegmentStartTime;

const uint64_t kKeyFrameTimestamp = 20123;
const uint64_t kKeyFrameStartByteOffset = 3456;
const uint64_t kKeyFrameSize = 543234;

static_assert(kKeyFrameStartByteOffset < kSegmentSize, "");
static_assert(kKeyFrameStartByteOffset + kKeyFrameSize <= kSegmentSize, "");

// This value doesn't really affect the test, it's not used by the
// implementation.
const bool kInitialEncryptionInfo = true;
const bool kIFramesOnlyPlaylist = true;

const char kDefaultPlaylistName[] = "default_playlist.m3u8";
const char kDefaultName[] = "DEFAULTNAME";
const char kDefaultGroupId[] = "DEFAULTGROUPID";
const char kCharactersticA[] = "public.accessibility.transcribes-spoken-dialog";
const char kCharactersticB[] = "public.easy-to-read";

MATCHER_P(HasEncryptionScheme, expected_scheme, "") {
  *result_listener << "it has_protected_content: "
                   << arg.has_protected_content() << " has_protection_scheme: "
                   << arg.protected_content().has_protection_scheme()
                   << " protection_scheme: "
                   << arg.protected_content().protection_scheme();
  return arg.has_protected_content() &&
         arg.protected_content().has_protection_scheme() &&
         arg.protected_content().protection_scheme() == expected_scheme;
}

}  // namespace

class HlsNotifyMuxerListenerTest : public ::testing::Test {
 protected:
  HlsNotifyMuxerListenerTest()
      : listener_(kDefaultPlaylistName,
                  !kIFramesOnlyPlaylist,
                  kDefaultName,
                  kDefaultGroupId,
                  std::vector<std::string>{kCharactersticA, kCharactersticB},
                  &mock_notifier_) {}

  MuxerListener::MediaRanges GetMediaRanges(
      const std::vector<Range>& segment_ranges) {
    MuxerListener::MediaRanges ranges;
    // We don't care about init range and index range values.
    Range init_range;
    init_range.start = 0;
    init_range.end = 100;
    Range index_range;
    index_range.start = 101;
    index_range.end = 200;

    ranges.init_range = init_range;
    ranges.index_range = index_range;
    ranges.subsegment_ranges = segment_ranges;
    return ranges;
  }

  MockHlsNotifier mock_notifier_;
  HlsNotifyMuxerListener listener_;
};

// Verify that NotifyEncryptionUpdate() is not called before OnMediaStart() is
// called.
TEST_F(HlsNotifyMuxerListenerTest, OnEncryptionInfoReadyBeforeMediaStart) {
  std::vector<uint8_t> key_id(16, 0x05);
  std::vector<uint8_t> iv(16, 0x54);

  EXPECT_CALL(mock_notifier_, NotifyEncryptionUpdate(_, _, _, _, _)).Times(0);
  listener_.OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbcs, key_id,
                                  iv, GetDefaultKeySystemInfo());
}

TEST_F(HlsNotifyMuxerListenerTest, OnMediaStart) {
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);

  EXPECT_CALL(
      mock_notifier_,
      NotifyNewStream(Property(&MediaInfo::hls_characteristics,
                               ElementsAre(kCharactersticA, kCharactersticB)),
                      StrEq(kDefaultPlaylistName), StrEq("DEFAULTNAME"),
                      StrEq("DEFAULTGROUPID"), _))
      .WillOnce(Return(true));

  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
}

// OnEncryptionStart() should call MuxerListener::NotifyEncryptionUpdate() after
// OnEncryptionInfoReady() and OnMediaStart().
TEST_F(HlsNotifyMuxerListenerTest, OnEncryptionStart) {
  std::vector<uint8_t> system_id(kAnySystemId,
                                 kAnySystemId + arraysize(kAnySystemId));
  std::vector<uint8_t> pssh(kAnyData, kAnyData + arraysize(kAnyData));
  std::vector<uint8_t> key_id(16, 0x05);
  std::vector<uint8_t> iv(16, 0x54);

  EXPECT_CALL(mock_notifier_, NotifyEncryptionUpdate(_, _, _, _, _)).Times(0);
  listener_.OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbcs, key_id,
                                  iv, {{system_id, pssh}});
  ::testing::Mock::VerifyAndClearExpectations(&mock_notifier_);

  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";

  EXPECT_CALL(mock_notifier_, NotifyEncryptionUpdate(_, _, _, _, _)).Times(0);
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
  ::testing::Mock::VerifyAndClearExpectations(&mock_notifier_);

  EXPECT_CALL(mock_notifier_,
              NotifyEncryptionUpdate(_, key_id, system_id, iv, pssh))
      .WillOnce(Return(true));
  listener_.OnEncryptionStart();
}

// If OnEncryptionStart() is called before media start,
// HlsNotiifer::NotifyEncryptionUpdate() should be called by the end of
// OnMediaStart().
TEST_F(HlsNotifyMuxerListenerTest, OnEncryptionStartBeforeMediaStart) {
  std::vector<uint8_t> system_id(kAnySystemId,
                                 kAnySystemId + arraysize(kAnySystemId));
  std::vector<uint8_t> pssh(kAnyData, kAnyData + arraysize(kAnyData));
  std::vector<uint8_t> key_id(16, 0x05);
  std::vector<uint8_t> iv(16, 0x54);

  EXPECT_CALL(mock_notifier_, NotifyEncryptionUpdate(_, _, _, _, _)).Times(0);
  listener_.OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbcs, key_id,
                                  iv, {{system_id, pssh}});
  ::testing::Mock::VerifyAndClearExpectations(&mock_notifier_);

  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";

  // It doesn't really matter when this is called, could be called right away in
  // OnEncryptionStart() if that is possible. Just matters that it is called by
  // the time OnMediaStart() returns.
  EXPECT_CALL(mock_notifier_,
              NotifyEncryptionUpdate(_, key_id, system_id, iv, pssh))
      .WillOnce(Return(true));
  listener_.OnEncryptionStart();
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
}

// NotifyEncryptionUpdate() should not be called if NotifyNewStream() fails in
// OnMediaStart().
TEST_F(HlsNotifyMuxerListenerTest, NoEncryptionUpdateIfNotifyNewStreamFails) {
  std::vector<uint8_t> key_id(16, 0x05);
  std::vector<uint8_t> iv(16, 0x54);

  EXPECT_CALL(mock_notifier_, NotifyEncryptionUpdate(_, _, _, _, _)).Times(0);
  listener_.OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbcs, key_id,
                                  iv, GetDefaultKeySystemInfo());
  ::testing::Mock::VerifyAndClearExpectations(&mock_notifier_);

  EXPECT_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillOnce(Return(false));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";

  EXPECT_CALL(mock_notifier_, NotifyEncryptionUpdate(_, _, _, _, _)).Times(0);
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
}

// Verify that after OnMediaStart(), OnEncryptionInfoReady() calls
// NotifyEncryptionUpdate().
TEST_F(HlsNotifyMuxerListenerTest, OnEncryptionInfoReady) {
  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);

  std::vector<uint8_t> system_id(kAnySystemId,
                                 kAnySystemId + arraysize(kAnySystemId));
  std::vector<uint8_t> pssh(kAnyData, kAnyData + arraysize(kAnyData));
  std::vector<uint8_t> key_id(16, 0x05);
  std::vector<uint8_t> iv(16, 0x54);

  EXPECT_CALL(mock_notifier_,
              NotifyEncryptionUpdate(_, key_id, system_id, iv, pssh))
      .WillOnce(Return(true));
  listener_.OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cbcs, key_id,
                                  iv, {{system_id, pssh}});
}

// Verify that if protection scheme is specified in OnEncryptionInfoReady(),
// the information is copied to MediaInfo in OnMediaStart().
TEST_F(HlsNotifyMuxerListenerTest, OnEncryptionInfoReadyWithProtectionScheme) {
  std::vector<uint8_t> key_id(16, 0x05);
  std::vector<uint8_t> iv(16, 0x54);

  listener_.OnEncryptionInfoReady(kInitialEncryptionInfo, FOURCC_cenc, key_id,
                                  iv, GetDefaultKeySystemInfo());
  ::testing::Mock::VerifyAndClearExpectations(&mock_notifier_);

  ON_CALL(mock_notifier_,
          NotifyNewStream(HasEncryptionScheme("cenc"), _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;

  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
}

TEST_F(HlsNotifyMuxerListenerTest, OnSampleDurationReady) {
  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);

  EXPECT_CALL(mock_notifier_, NotifySampleDuration(_, 2340));
  listener_.OnSampleDurationReady(2340);
}

TEST_F(HlsNotifyMuxerListenerTest, OnNewSegmentAndCueEvent) {
  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);

  EXPECT_CALL(mock_notifier_, NotifyCueEvent(_, kCueStartTime));
  EXPECT_CALL(
      mock_notifier_,
      NotifyNewSegment(_, StrEq("new_segment_name10.ts"), kSegmentStartTime,
                       kSegmentDuration, _, kSegmentSize));
  listener_.OnCueEvent(kCueStartTime, "dummy cue data");
  listener_.OnNewSegment("new_segment_name10.ts", kSegmentStartTime,
                         kSegmentDuration, kSegmentSize, kAnySegmentNumber);
}

// Verify that the notifier is called for every segment in OnMediaEnd if
// segment_template is not set.
TEST_F(HlsNotifyMuxerListenerTest, NoSegmentTemplateOnMediaEnd) {
  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.output_file_name = "filename.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);

  listener_.OnCueEvent(kCueStartTime, "dummy cue data");
  listener_.OnNewSegment("filename.mp4", kSegmentStartTime, kSegmentDuration,
                         kSegmentSize, kAnySegmentNumber);

  EXPECT_CALL(mock_notifier_, NotifyCueEvent(_, kCueStartTime));
  EXPECT_CALL(
      mock_notifier_,
      NotifyNewSegment(_, StrEq("filename.mp4"), kSegmentStartTime,
                       kSegmentDuration, kSegmentStartOffset, kSegmentSize));
  listener_.OnMediaEnd(
      GetMediaRanges(
          {{kSegmentStartOffset, kSegmentStartOffset + kSegmentSize - 1}}),
      200000);
}

// Verify the event handling with multiple files, i.e. multiple OnMediaStart and
// OnMediaEnd calls.
TEST_F(HlsNotifyMuxerListenerTest, NoSegmentTemplateOnMediaEndTwice) {
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options1;
  muxer_options1.output_file_name = "filename1.mp4";
  MuxerOptions muxer_options2 = muxer_options1;
  muxer_options2.output_file_name = "filename2.mp4";

  InSequence in_sequence;

  // Event flow for first file.
  listener_.OnMediaStart(muxer_options1, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
  listener_.OnNewSegment("filename1.mp4", kSegmentStartTime, kSegmentDuration,
                         kSegmentSize, kAnySegmentNumber);
  listener_.OnCueEvent(kCueStartTime, "dummy cue data");

  EXPECT_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(mock_notifier_, NotifyNewSegment(_, StrEq("filename1.mp4"),
                                               kSegmentStartTime, _, _, _));
  EXPECT_CALL(mock_notifier_, NotifyCueEvent(_, kCueStartTime));
  listener_.OnMediaEnd(
      GetMediaRanges(
          {{kSegmentStartOffset, kSegmentStartOffset + kSegmentSize - 1}}),
      200000);

  // Event flow for second file.
  listener_.OnMediaStart(muxer_options2, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);
  listener_.OnNewSegment("filename2.mp4", kSegmentStartTime + kSegmentDuration,
                         kSegmentDuration, kSegmentSize, kAnySegmentNumber);
  EXPECT_CALL(mock_notifier_,
              NotifyNewSegment(_, StrEq("filename2.mp4"),
                               kSegmentStartTime + kSegmentDuration, _, _, _));
  listener_.OnMediaEnd(
      GetMediaRanges(
          {{kSegmentStartOffset, kSegmentStartOffset + kSegmentSize - 1}}),
      200000);
}

// Verify that when there is a mismatch in the number of calls to
// NotifyNewSegment and the number of segment ranges, it uses the min of the
// two.
TEST_F(HlsNotifyMuxerListenerTest,
       NoSegmentTemplateOnMediaEndSubsegmentSizeMismatch) {
  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.output_file_name = "filename.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);

  listener_.OnNewSegment("filename.mp4", kSegmentStartTime, kSegmentDuration,
                         kSegmentSize, kAnySegmentNumber);
  EXPECT_CALL(
      mock_notifier_,
      NotifyNewSegment(_, StrEq("filename.mp4"), kSegmentStartTime,
                       kSegmentDuration, kSegmentStartOffset, kSegmentSize));
  listener_.OnMediaEnd(
      GetMediaRanges(
          {{kSegmentStartOffset, kSegmentStartOffset + kSegmentSize - 1},
           {kSegmentStartOffset + kSegmentSize,
            kSegmentStartOffset + kSegmentSize * 2 - 1}}),
      200000);
}

class HlsNotifyMuxerListenerKeyFrameTest : public TestWithParam<bool> {
 public:
  HlsNotifyMuxerListenerKeyFrameTest()
      : listener_(kDefaultPlaylistName,
                  GetParam(),
                  kDefaultName,
                  kDefaultGroupId,
                  std::vector<std::string>(),  // no characteristics.
                  &mock_notifier_) {}

  MockHlsNotifier mock_notifier_;
  HlsNotifyMuxerListener listener_;
};

TEST_P(HlsNotifyMuxerListenerKeyFrameTest, WithSegmentTemplate) {
  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.segment_template = "$Number$.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);

  EXPECT_CALL(mock_notifier_,
              NotifyKeyFrame(_, kKeyFrameTimestamp, kKeyFrameStartByteOffset,
                             kKeyFrameSize))
      .Times(GetParam() ? 1 : 0);
  listener_.OnKeyFrame(kKeyFrameTimestamp, kKeyFrameStartByteOffset,
                       kKeyFrameSize);
}

// Verify that the notifier is called for every key frame in OnMediaEnd if
// segment_template is not set.
TEST_P(HlsNotifyMuxerListenerKeyFrameTest, NoSegmentTemplate) {
  ON_CALL(mock_notifier_, NotifyNewStream(_, _, _, _, _))
      .WillByDefault(Return(true));
  VideoStreamInfoParameters video_params = GetDefaultVideoStreamInfoParams();
  std::shared_ptr<StreamInfo> video_stream_info =
      CreateVideoStreamInfo(video_params);
  MuxerOptions muxer_options;
  muxer_options.output_file_name = "filename.mp4";
  listener_.OnMediaStart(muxer_options, *video_stream_info, 90000,
                         MuxerListener::kContainerMpeg2ts);

  listener_.OnKeyFrame(kKeyFrameTimestamp, kKeyFrameStartByteOffset,
                       kKeyFrameSize);
  listener_.OnNewSegment("filename.mp4", kSegmentStartTime, kSegmentDuration,
                         kSegmentSize, kAnySegmentNumber);

  EXPECT_CALL(mock_notifier_,
              NotifyKeyFrame(_, kKeyFrameTimestamp,
                             kSegmentStartOffset + kKeyFrameStartByteOffset,
                             kKeyFrameSize))
      .Times(GetParam() ? 1 : 0);
  EXPECT_CALL(
      mock_notifier_,
      NotifyNewSegment(_, StrEq("filename.mp4"), kSegmentStartTime,
                       kSegmentDuration, kSegmentStartOffset, kSegmentSize));

  MuxerListener::MediaRanges ranges;
  ranges.subsegment_ranges.push_back(
      {kSegmentStartOffset, kSegmentStartOffset + kSegmentSize - 1});
  listener_.OnMediaEnd(ranges, 200000);
}

INSTANTIATE_TEST_CASE_P(InstantiationName,
                        HlsNotifyMuxerListenerKeyFrameTest,
                        Bool());

}  // namespace media
}  // namespace shaka
