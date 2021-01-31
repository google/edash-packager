// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <gflags/gflags.h>
#include <memory>

#include "packager/base/base64.h"
#include "packager/base/files/file_path.h"
#include "packager/hls/base/mock_media_playlist.h"
#include "packager/hls/base/simple_hls_notifier.h"
#include "packager/media/base/protection_system_ids.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/base/widevine_pssh_data.pb.h"

DECLARE_bool(enable_legacy_widevine_hls_signaling);

namespace shaka {
namespace hls {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::Property;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::WithParamInterface;

namespace {
const char kMasterPlaylistName[] = "master.m3u8";
const char kDefaultAudioLanguage[] = "en";
const char kDefaultTextLanguage[] = "fr";
const bool kIsIndependentSegments = true;
const char kEmptyKeyUri[] = "";
const char kFairPlayKeyUri[] = "skd://www.license.com/getkey?key_id=testing";
const char kIdentityKeyUri[] = "https://www.license.com/getkey?key_id=testing";
const HlsPlaylistType kVodPlaylist = HlsPlaylistType::kVod;
const HlsPlaylistType kLivePlaylist = HlsPlaylistType::kLive;
const base::Time kRefTime = base::Time();

class MockMasterPlaylist : public MasterPlaylist {
 public:
  MockMasterPlaylist()
      : MasterPlaylist(kMasterPlaylistName,
                       kDefaultAudioLanguage,
                       kDefaultTextLanguage,
                       kIsIndependentSegments) {}

  MOCK_METHOD3(WriteMasterPlaylist,
               bool(const std::string& prefix,
                    const std::string& output_dir,
                    const std::list<MediaPlaylist*>& playlists));
};

class MockMediaPlaylistFactory : public MediaPlaylistFactory {
 public:
  MOCK_METHOD4(CreateMock,
               MediaPlaylist*(const HlsParams& hls_params,
                              const std::string& file_name,
                              const std::string& name,
                              const std::string& group_id));

  std::unique_ptr<MediaPlaylist> Create(const HlsParams& hls_params,
                                        const std::string& file_name,
                                        const std::string& name,
                                        const std::string& group_id) override {
    return std::unique_ptr<MediaPlaylist>(
        CreateMock(hls_params, file_name, name, group_id));
  }
};

const double kTestTimeShiftBufferDepth = 1800.0;
const char kTestPrefix[] = "http://testprefix.com/";
const char kAnyOutputDir[] = "anything";

const uint64_t kAnyStartTime = 10;
const uint64_t kAnyDuration = 1000;
const uint64_t kAnySize = 2000;

const char kCencProtectionScheme[] = "cenc";
const char kSampleAesProtectionScheme[] = "cbca";

}  // namespace

class SimpleHlsNotifierTest : public ::testing::Test {
 protected:
  SimpleHlsNotifierTest() : SimpleHlsNotifierTest(kVodPlaylist) {}

  SimpleHlsNotifierTest(HlsPlaylistType playlist_type)
      : widevine_system_id_(std::begin(media::kWidevineSystemId),
                            std::end(media::kWidevineSystemId)),
        common_system_id_(std::begin(media::kCommonSystemId),
                          std::end(media::kCommonSystemId)),
        fairplay_system_id_(std::begin(media::kFairPlaySystemId),
                            std::end(media::kFairPlaySystemId)) {
    hls_params_.playlist_type = kVodPlaylist;
    hls_params_.time_shift_buffer_depth = kTestTimeShiftBufferDepth;
    hls_params_.base_url = kTestPrefix;
    hls_params_.key_uri = kEmptyKeyUri;
    hls_params_.master_playlist_output =
        std::string(kAnyOutputDir) + "/" + kMasterPlaylistName;
  }

  void InjectMediaPlaylistFactory(std::unique_ptr<MediaPlaylistFactory> factory,
                                  SimpleHlsNotifier* notifier) {
    notifier->media_playlist_factory_ = std::move(factory);
  }

  void InjectMasterPlaylist(std::unique_ptr<MasterPlaylist> playlist,
                            SimpleHlsNotifier* notifier) {
    notifier->master_playlist_ = std::move(playlist);
  }

  size_t NumRegisteredMediaPlaylists(const SimpleHlsNotifier& notifier) {
    return notifier.stream_map_.size();
  }

  uint32_t SetupStream(const std::string& protection_scheme,
                       MockMediaPlaylist* mock_media_playlist,
                       SimpleHlsNotifier* notifier) {
    MediaInfo media_info;
    media_info.mutable_protected_content()->set_protection_scheme(
        protection_scheme);
    std::unique_ptr<MockMasterPlaylist> mock_master_playlist(
        new MockMasterPlaylist());
    std::unique_ptr<MockMediaPlaylistFactory> factory(
        new MockMediaPlaylistFactory());

    EXPECT_CALL(*mock_media_playlist, SetMediaInfo(_)).WillOnce(Return(true));
    EXPECT_CALL(*factory, CreateMock(_, _, _, _))
        .WillOnce(Return(mock_media_playlist));

    InjectMasterPlaylist(std::move(mock_master_playlist), notifier);
    InjectMediaPlaylistFactory(std::move(factory), notifier);
    EXPECT_TRUE(notifier->Init());
    uint32_t stream_id;
    EXPECT_TRUE(notifier->NotifyNewStream(media_info, "playlist.m3u8", "name",
                                          "groupid", &stream_id));
    return stream_id;
  }

  const std::vector<uint8_t> widevine_system_id_;
  const std::vector<uint8_t> common_system_id_;
  const std::vector<uint8_t> fairplay_system_id_;
  HlsParams hls_params_;
};

TEST_F(SimpleHlsNotifierTest, Init) {
  SimpleHlsNotifier notifier(hls_params_);
  EXPECT_TRUE(notifier.Init());
}

TEST_F(SimpleHlsNotifierTest, Flush) {
  SimpleHlsNotifier notifier(hls_params_);
  std::unique_ptr<MockMasterPlaylist> mock_master_playlist(
      new MockMasterPlaylist());
  EXPECT_CALL(*mock_master_playlist,
              WriteMasterPlaylist(StrEq(kTestPrefix), StrEq(kAnyOutputDir), _))
      .WillOnce(Return(true));
  InjectMasterPlaylist(std::move(mock_master_playlist), &notifier);
  EXPECT_TRUE(notifier.Init());
  EXPECT_TRUE(notifier.Flush());
}

TEST_F(SimpleHlsNotifierTest, NotifyNewStream) {
  std::unique_ptr<MockMasterPlaylist> mock_master_playlist(
      new MockMasterPlaylist());
  std::unique_ptr<MockMediaPlaylistFactory> factory(
      new MockMediaPlaylistFactory());

  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");

  EXPECT_CALL(*mock_media_playlist, SetMediaInfo(_)).WillOnce(Return(true));
  EXPECT_CALL(*factory, CreateMock(_, StrEq("video_playlist.m3u8"),
                                   StrEq("name"), StrEq("groupid")))
      .WillOnce(Return(mock_media_playlist));

  SimpleHlsNotifier notifier(hls_params_);

  InjectMasterPlaylist(std::move(mock_master_playlist), &notifier);
  InjectMediaPlaylistFactory(std::move(factory), &notifier);
  EXPECT_TRUE(notifier.Init());
  MediaInfo media_info;
  uint32_t stream_id;
  EXPECT_TRUE(notifier.NotifyNewStream(media_info, "video_playlist.m3u8",
                                       "name", "groupid", &stream_id));
  EXPECT_EQ(1u, NumRegisteredMediaPlaylists(notifier));
}

TEST_F(SimpleHlsNotifierTest, NotifyNewSegment) {
  std::unique_ptr<MockMasterPlaylist> mock_master_playlist(
      new MockMasterPlaylist());
  std::unique_ptr<MockMediaPlaylistFactory> factory(
      new MockMediaPlaylistFactory());

  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");

  EXPECT_CALL(*mock_media_playlist, SetMediaInfo(_)).WillOnce(Return(true));
  EXPECT_CALL(*factory, CreateMock(_, _, _, _))
      .WillOnce(Return(mock_media_playlist));

  const uint64_t kStartTime = 1328;
  const uint64_t kDuration = 398407;
  const uint64_t kSize = 6595840;
  const std::string segment_name = "segmentname";
  EXPECT_CALL(*mock_media_playlist,
              AddSegment(StrEq(kTestPrefix + segment_name), kStartTime,
                         kDuration, 203, kSize, kRefTime));

  const double kLongestSegmentDuration = 11.3;
  const uint32_t kTargetDuration = 12;  // ceil(kLongestSegmentDuration).
  EXPECT_CALL(*mock_media_playlist, GetLongestSegmentDuration())
      .WillOnce(Return(kLongestSegmentDuration));

  SimpleHlsNotifier notifier(hls_params_);
  MockMasterPlaylist* mock_master_playlist_ptr = mock_master_playlist.get();
  InjectMasterPlaylist(std::move(mock_master_playlist), &notifier);
  InjectMediaPlaylistFactory(std::move(factory), &notifier);
  EXPECT_TRUE(notifier.Init());
  MediaInfo media_info;
  uint32_t stream_id;
  EXPECT_TRUE(notifier.NotifyNewStream(media_info, "playlist.m3u8", "name",
                                       "groupid", &stream_id));

  EXPECT_TRUE(notifier.NotifyNewSegment(stream_id, segment_name, kStartTime,
                                        kDuration, 203, kSize));

  Mock::VerifyAndClearExpectations(mock_master_playlist_ptr);
  Mock::VerifyAndClearExpectations(mock_media_playlist);

  EXPECT_CALL(*mock_master_playlist_ptr,
              WriteMasterPlaylist(StrEq(kTestPrefix), StrEq(kAnyOutputDir),
                                  ElementsAre(mock_media_playlist)))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_media_playlist, SetTargetDuration(kTargetDuration))
      .Times(1);
  EXPECT_CALL(*mock_media_playlist,
              WriteToFile(StrEq(
                  base::FilePath::FromUTF8Unsafe(kAnyOutputDir)
                      .Append(base::FilePath::FromUTF8Unsafe("playlist.m3u8"))
                      .AsUTF8Unsafe())))
      .WillOnce(Return(true));
  EXPECT_TRUE(notifier.Flush());
}

TEST_F(SimpleHlsNotifierTest, NotifyKeyFrame) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kCencProtectionScheme, mock_media_playlist, &notifier);

  const uint64_t kTimestamp = 12345;
  const uint64_t kStartByteOffset = 888;
  const uint64_t kSize = 555;
  EXPECT_CALL(*mock_media_playlist,
              AddKeyFrame(kTimestamp, kStartByteOffset, kSize));
  EXPECT_TRUE(
      notifier.NotifyKeyFrame(stream_id, kTimestamp, kStartByteOffset, kSize));
}

TEST_F(SimpleHlsNotifierTest, NotifyNewSegmentWithoutStreamsRegistered) {
  SimpleHlsNotifier notifier(hls_params_);
  EXPECT_TRUE(notifier.Init());
  EXPECT_FALSE(notifier.NotifyNewSegment(1u, "anything", 0u, 0u, 0u, 0u));
}

TEST_F(SimpleHlsNotifierTest, NotifyEncryptionUpdateIdentityKey) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kSampleAesProtectionScheme, mock_media_playlist, &notifier);

  const std::vector<uint8_t> key_id(16, 0x23);
  const std::vector<uint8_t> iv(16, 0x45);
  const std::vector<uint8_t> dummy_pssh_data(10, 'p');

  std::string expected_key_uri_base64;
  base::Base64Encode(std::string(key_id.begin(), key_id.end()),
                     &expected_key_uri_base64);

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_key_uri_base64),
                  StrEq(""), StrEq("0x45454545454545454545454545454545"),
                  StrEq("identity"), _));
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      stream_id, key_id, common_system_id_, iv, dummy_pssh_data));
}

// Verify that the encryption scheme set in MediaInfo is passed to
// MediaPlaylist::AddEncryptionInfo().
TEST_F(SimpleHlsNotifierTest, EncryptionScheme) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  hls_params_.key_uri = kIdentityKeyUri;
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kCencProtectionScheme, mock_media_playlist, &notifier);

  const std::vector<uint8_t> key_id(16, 0x23);
  const std::vector<uint8_t> iv(16, 0x45);
  const std::vector<uint8_t> dummy_pssh_data(10, 'p');

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAesCenc,
                                StrEq(kIdentityKeyUri), StrEq(""),
                                StrEq("0x45454545454545454545454545454545"),
                                StrEq("identity"), _));
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      stream_id, key_id, common_system_id_, iv, dummy_pssh_data));
}

// Verify that the FairPlay systemID is correctly handled when constructing
// encryption info.
TEST_F(SimpleHlsNotifierTest, NotifyEncryptionUpdateFairPlay) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  hls_params_.playlist_type = kLivePlaylist;
  hls_params_.key_uri = kFairPlayKeyUri;
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kSampleAesProtectionScheme, mock_media_playlist, &notifier);
  const std::vector<uint8_t> key_id(16, 0x12);
  const std::vector<uint8_t> dummy_pssh_data(10, 'p');

  EXPECT_CALL(
      *mock_media_playlist,
      AddEncryptionInfo(MediaPlaylist::EncryptionMethod::kSampleAes,
                        StrEq(kFairPlayKeyUri), StrEq(""), StrEq(""),
                        StrEq("com.apple.streamingkeydelivery"), StrEq("1")));
  EXPECT_TRUE(
      notifier.NotifyEncryptionUpdate(stream_id, key_id, fairplay_system_id_,
                                      std::vector<uint8_t>(), dummy_pssh_data));
}

TEST_F(SimpleHlsNotifierTest, NotifyEncryptionUpdateWithoutStreamsRegistered) {
  std::vector<uint8_t> system_id;
  std::vector<uint8_t> iv;
  std::vector<uint8_t> pssh_data;
  std::vector<uint8_t> key_id;
  SimpleHlsNotifier notifier(hls_params_);
  EXPECT_TRUE(notifier.Init());
  EXPECT_FALSE(
      notifier.NotifyEncryptionUpdate(1238u, key_id, system_id, iv, pssh_data));
}

TEST_F(SimpleHlsNotifierTest, NotifyCueEvent) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kCencProtectionScheme, mock_media_playlist, &notifier);

  EXPECT_CALL(*mock_media_playlist, AddPlacementOpportunity());
  const uint64_t kCueEventTimestamp = 12345;
  EXPECT_TRUE(notifier.NotifyCueEvent(stream_id, kCueEventTimestamp));
}

struct RebaseUrlTestData {
  // Base URL is the prefix of segment URL and media playlist URL if it is
  // specified; otherwise, relative URL is used for the relavent URLs.
  std::string base_url;
  // A local path to a directory where the master playlist should output.
  std::string master_playlist_dir;
  // Media playlist path. This may be relative or absolute.
  std::string playlist_path;
  // Expected relative playlist path. It is path_relative_to(master_directory).
  std::string expected_relative_playlist_path;
  // Media segment path. This may be relative or absolute.
  std::string segment_path;
  // Expected segment URL in the media playlist:
  //   - If |base_url| is specified, it is |base_url| +
  //     |relative path of segment_path from master_playlist_dir|.
  //   - Otherwise, it is
  //     |relative path of segment_path from directory that contains
  //     playlist_path|.
  std::string expected_segment_url;
  // Init media segment path. This may be relative or absolute.
  std::string init_segment_path;
  // Expected init segment URL in the media playlist:
  //   - If |base_url| is specified, it is |base_url| +
  //     |relative path of init_segment_path from master_playlist_dir|.
  //   - Otherwise, it is
  //     |relative path of init_segment_path from directory that contains
  //     playlist_path|.
  std::string expected_init_segment_url;
};

class SimpleHlsNotifierRebaseUrlTest
    : public SimpleHlsNotifierTest,
      public WithParamInterface<RebaseUrlTestData> {
 protected:
  void SetUp() override { test_data_ = GetParam(); }

  RebaseUrlTestData test_data_;
};

TEST_P(SimpleHlsNotifierRebaseUrlTest, Test) {
  hls_params_.base_url = test_data_.base_url;
  hls_params_.master_playlist_output =
      test_data_.master_playlist_dir + kMasterPlaylistName;
  SimpleHlsNotifier test_notifier(hls_params_);

  std::unique_ptr<MockMasterPlaylist> mock_master_playlist(
      new MockMasterPlaylist());
  std::unique_ptr<MockMediaPlaylistFactory> factory(
      new MockMediaPlaylistFactory());

  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist(test_data_.expected_relative_playlist_path, "", "");

  EXPECT_CALL(
      *mock_media_playlist,
      SetMediaInfo(Property(&MediaInfo::init_segment_url,
                            StrEq(test_data_.expected_init_segment_url))))
      .WillOnce(Return(true));

  if (!test_data_.expected_segment_url.empty()) {
    EXPECT_CALL(*mock_media_playlist,
                AddSegment(test_data_.expected_segment_url, _, _, _, _, kRefTime));
  }
  EXPECT_CALL(*factory,
              CreateMock(_, StrEq(test_data_.expected_relative_playlist_path),
                         StrEq("name"), StrEq("groupid")))
      .WillOnce(Return(mock_media_playlist));

  InjectMasterPlaylist(std::move(mock_master_playlist), &test_notifier);
  InjectMediaPlaylistFactory(std::move(factory), &test_notifier);
  EXPECT_TRUE(test_notifier.Init());

  MediaInfo media_info;
  if (!test_data_.init_segment_path.empty())
    media_info.set_init_segment_name(test_data_.init_segment_path);
  uint32_t stream_id;
  EXPECT_TRUE(test_notifier.NotifyNewStream(
      media_info, test_data_.playlist_path, "name", "groupid", &stream_id));
  if (!test_data_.segment_path.empty()) {
    EXPECT_TRUE(test_notifier.NotifyNewSegment(
        stream_id, test_data_.segment_path, kAnyStartTime, kAnyDuration, 0,
        kAnySize));
  }
}

INSTANTIATE_TEST_CASE_P(
    RebaseUrl,
    SimpleHlsNotifierRebaseUrlTest,
    ::testing::Values(
        // Verify relative segment path.
        RebaseUrlTestData{"http://testprefix.com/", "master_directory/",
                          "video_playlist.m3u8", "video_playlist.m3u8",
                          "master_directory/path/to/media1.ts",
                          "http://testprefix.com/path/to/media1.ts",
                          "" /* init segment path */,
                          "" /* expected init segment url */},
        // Verify relative init segment path.
        RebaseUrlTestData{"http://testprefix.com/", "master_directory/",
                          "video_playlist.m3u8", "video_playlist.m3u8",
                          "" /* segment path */, "" /* expected segment url */,
                          "master_directory/path/to/init.mp4",
                          "http://testprefix.com/path/to/init.mp4"},
        // Verify segment url relative to playlist.
        RebaseUrlTestData{
            "" /* no base url */, "master_directory/",
            "video/video_playlist.m3u8", "video/video_playlist.m3u8",
            "master_directory/video/path/to/media1.m4s", "path/to/media1.m4s",
            "master_directory/video/path/to/init.mp4", "path/to/init.mp4"},
        // Verify absolute directory.
        RebaseUrlTestData{
            "http://testprefix.com/", "/tmp/something/", "video_playlist.m3u8",
            "video_playlist.m3u8", "/tmp/something/media1.ts",
            "http://testprefix.com/media1.ts", "" /* init segment path */,
            "" /* expected init segment url */},
        // Verify absolute directory, but media in a different directory.
        // Note that we don't really expect this in practice.
        RebaseUrlTestData{
            "http://testprefix.com/", "/tmp/something/", "video_playlist.m3u8",
            "video_playlist.m3u8", "/var/somewhereelse/media1.ts",
            "http://testprefix.com//var/somewhereelse/media1.ts",
            "" /* init segment path */, "" /* expected init segment url */
        },
        // Verify absolute directory, absolute media playlist path.
        RebaseUrlTestData{
            "http://testprefix.com/", "/tmp/something/",
            "/tmp/something/video/video_playlist.m3u8",
            "video/video_playlist.m3u8", "/tmp/something/video/media1.ts",
            "http://testprefix.com/video/media1.ts", "" /* init segment path */,
            "" /* expected init segment url */
        },
        // Same as above, but without base_url.
        RebaseUrlTestData{
            "" /* no base url */, "/tmp/something/",
            "/tmp/something/video/video_playlist.m3u8",
            "video/video_playlist.m3u8", "/tmp/something/video/media1.ts",
            "media1.ts", "" /* init segment path */,
            "" /* expected init segment url */
        }));

class LiveOrEventSimpleHlsNotifierTest
    : public SimpleHlsNotifierTest,
      public WithParamInterface<HlsPlaylistType> {
 protected:
  LiveOrEventSimpleHlsNotifierTest() : SimpleHlsNotifierTest(GetParam()) {
    expected_playlist_type_ = GetParam();
  }

  HlsPlaylistType expected_playlist_type_;
};

TEST_P(LiveOrEventSimpleHlsNotifierTest, NotifyNewSegment) {
  std::unique_ptr<MockMasterPlaylist> mock_master_playlist(
      new MockMasterPlaylist());
  std::unique_ptr<MockMediaPlaylistFactory> factory(
      new MockMediaPlaylistFactory());

  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");

  EXPECT_CALL(*mock_media_playlist, SetMediaInfo(_)).WillOnce(Return(true));
  EXPECT_CALL(*factory, CreateMock(_, _, _, _))
      .WillOnce(Return(mock_media_playlist));

  const uint64_t kStartTime = 1328;
  const uint64_t kDuration = 398407;
  const uint64_t kSize = 6595840;
  const std::string segment_name = "segmentname";
  EXPECT_CALL(*mock_media_playlist,
              AddSegment(StrEq(kTestPrefix + segment_name), kStartTime,
                         kDuration, _, kSize, kRefTime));

  const double kLongestSegmentDuration = 11.3;
  const uint32_t kTargetDuration = 12;  // ceil(kLongestSegmentDuration).
  EXPECT_CALL(*mock_media_playlist, GetLongestSegmentDuration())
      .WillOnce(Return(kLongestSegmentDuration));

  EXPECT_CALL(*mock_master_playlist,
              WriteMasterPlaylist(StrEq(kTestPrefix), StrEq(kAnyOutputDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_media_playlist, SetTargetDuration(kTargetDuration))
      .Times(1);
  EXPECT_CALL(*mock_media_playlist,
              WriteToFile(StrEq(
                  base::FilePath::FromUTF8Unsafe(kAnyOutputDir)
                      .Append(base::FilePath::FromUTF8Unsafe("playlist.m3u8"))
                      .AsUTF8Unsafe())))
      .WillOnce(Return(true));

  hls_params_.playlist_type = GetParam();
  SimpleHlsNotifier notifier(hls_params_);
  InjectMasterPlaylist(std::move(mock_master_playlist), &notifier);
  InjectMediaPlaylistFactory(std::move(factory), &notifier);
  EXPECT_TRUE(notifier.Init());
  MediaInfo media_info;
  uint32_t stream_id;
  EXPECT_TRUE(notifier.NotifyNewStream(media_info, "playlist.m3u8", "name",
                                       "groupid", &stream_id));

  EXPECT_TRUE(notifier.NotifyNewSegment(stream_id, segment_name, kStartTime,
                                        kDuration, 0, kSize));
}

TEST_P(LiveOrEventSimpleHlsNotifierTest, NotifyNewSegmentsWithMultipleStreams) {
  const uint64_t kStartTime = 1328;
  const uint64_t kDuration = 398407;
  const uint64_t kSize = 6595840;

  InSequence in_sequence;

  std::unique_ptr<MockMasterPlaylist> mock_master_playlist(
      new MockMasterPlaylist());
  std::unique_ptr<MockMediaPlaylistFactory> factory(
      new MockMediaPlaylistFactory());

  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist1 =
      new MockMediaPlaylist("playlist1.m3u8", "", "");
  MockMediaPlaylist* mock_media_playlist2 =
      new MockMediaPlaylist("playlist2.m3u8", "", "");

  EXPECT_CALL(*factory, CreateMock(_, StrEq("playlist1.m3u8"), _, _))
      .WillOnce(Return(mock_media_playlist1));
  EXPECT_CALL(*mock_media_playlist1, SetMediaInfo(_)).WillOnce(Return(true));
  EXPECT_CALL(*factory, CreateMock(_, StrEq("playlist2.m3u8"), _, _))
      .WillOnce(Return(mock_media_playlist2));
  EXPECT_CALL(*mock_media_playlist2, SetMediaInfo(_)).WillOnce(Return(true));

  hls_params_.playlist_type = GetParam();
  SimpleHlsNotifier notifier(hls_params_);
  MockMasterPlaylist* mock_master_playlist_ptr = mock_master_playlist.get();
  InjectMasterPlaylist(std::move(mock_master_playlist), &notifier);
  InjectMediaPlaylistFactory(std::move(factory), &notifier);
  EXPECT_TRUE(notifier.Init());

  MediaInfo media_info;
  uint32_t stream_id1;
  EXPECT_TRUE(notifier.NotifyNewStream(media_info, "playlist1.m3u8", "name",
                                       "groupid", &stream_id1));
  uint32_t stream_id2;
  EXPECT_TRUE(notifier.NotifyNewStream(media_info, "playlist2.m3u8", "name",
                                       "groupid", &stream_id2));

  EXPECT_CALL(*mock_media_playlist1, AddSegment(_, _, _, _, _, kRefTime)).Times(1);
  const double kLongestSegmentDuration = 11.3;
  const uint32_t kTargetDuration = 12;  // ceil(kLongestSegmentDuration).
  EXPECT_CALL(*mock_media_playlist1, GetLongestSegmentDuration())
      .WillOnce(Return(kLongestSegmentDuration));

  // SetTargetDuration and update all playlists as target duration is updated.
  EXPECT_CALL(*mock_media_playlist1, SetTargetDuration(kTargetDuration))
      .Times(1);
  EXPECT_CALL(*mock_media_playlist1,
              WriteToFile(StrEq(
                  base::FilePath::FromUTF8Unsafe(kAnyOutputDir)
                      .Append(base::FilePath::FromUTF8Unsafe("playlist1.m3u8"))
                      .AsUTF8Unsafe())))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_media_playlist2, SetTargetDuration(kTargetDuration))
      .Times(1);
  EXPECT_CALL(*mock_media_playlist2,
              WriteToFile(StrEq(
                  base::FilePath::FromUTF8Unsafe(kAnyOutputDir)
                      .Append(base::FilePath::FromUTF8Unsafe("playlist2.m3u8"))
                      .AsUTF8Unsafe())))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *mock_master_playlist_ptr,
      WriteMasterPlaylist(
          _, _, ElementsAre(mock_media_playlist1, mock_media_playlist2)))
      .WillOnce(Return(true));
  EXPECT_TRUE(notifier.NotifyNewSegment(stream_id1, "segment_name", kStartTime,
                                        kDuration, 0, kSize));

  EXPECT_CALL(*mock_media_playlist2, AddSegment(_, _, _, _, _, kRefTime)).Times(1);
  EXPECT_CALL(*mock_media_playlist2, GetLongestSegmentDuration())
      .WillOnce(Return(kLongestSegmentDuration));
  // Not updating other playlists as target duration does not change.
  EXPECT_CALL(*mock_media_playlist2,
              WriteToFile(StrEq(
                  base::FilePath::FromUTF8Unsafe(kAnyOutputDir)
                      .Append(base::FilePath::FromUTF8Unsafe("playlist2.m3u8"))
                      .AsUTF8Unsafe())))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_master_playlist_ptr, WriteMasterPlaylist(_, _, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(notifier.NotifyNewSegment(stream_id2, "segment_name", kStartTime,
                                        kDuration, 0, kSize));
}

INSTANTIATE_TEST_CASE_P(PlaylistTypes,
                        LiveOrEventSimpleHlsNotifierTest,
                        ::testing::Values(HlsPlaylistType::kLive,
                                          HlsPlaylistType::kEvent));

class WidevineSimpleHlsNotifierTest : public SimpleHlsNotifierTest,
                                      public WithParamInterface<bool> {
 protected:
  WidevineSimpleHlsNotifierTest()
      : enable_legacy_widevine_hls_signaling_(GetParam()) {
    FLAGS_enable_legacy_widevine_hls_signaling =
        enable_legacy_widevine_hls_signaling_;
  }

  bool enable_legacy_widevine_hls_signaling_ = false;
};

TEST_P(WidevineSimpleHlsNotifierTest, NotifyEncryptionUpdate) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kSampleAesProtectionScheme, mock_media_playlist, &notifier);

  const std::vector<uint8_t> iv(16, 0x45);

  media::WidevinePsshData widevine_pssh_data;
  widevine_pssh_data.set_provider("someprovider");
  widevine_pssh_data.set_content_id("contentid");
  const uint8_t kAnyKeyId[] = {
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
  };
  std::vector<uint8_t> any_key_id(kAnyKeyId, kAnyKeyId + arraysize(kAnyKeyId));
  widevine_pssh_data.add_key_id()->assign(kAnyKeyId,
                                          kAnyKeyId + arraysize(kAnyKeyId));
  std::string widevine_pssh_data_str = widevine_pssh_data.SerializeAsString();

  EXPECT_TRUE(!widevine_pssh_data_str.empty());
  std::vector<uint8_t> pssh_data(widevine_pssh_data_str.begin(),
                                 widevine_pssh_data_str.end());

  media::PsshBoxBuilder pssh_builder;
  pssh_builder.set_pssh_data(pssh_data);
  pssh_builder.set_system_id(widevine_system_id_.data(),
                             widevine_system_id_.size());
  pssh_builder.add_key_id(any_key_id);

  const char kExpectedJson[] =
      R"({"key_ids":["11223344112233441122334411223344"],)"
      R"("provider":"someprovider","content_id":"Y29udGVudGlk"})";
  std::string expected_json_base64;
  base::Base64Encode(kExpectedJson, &expected_json_base64);

  std::string expected_pssh_base64;
  const std::vector<uint8_t> pssh_box = pssh_builder.CreateBox();
  base::Base64Encode(std::string(pssh_box.begin(), pssh_box.end()),
                     &expected_pssh_base64);

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_json_base64),
                  StrEq(""), StrEq("0x45454545454545454545454545454545"),
                  StrEq("com.widevine"), _))
      .Times(enable_legacy_widevine_hls_signaling_ ? 1 : 0);
  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_pssh_base64),
                  StrEq("0x11223344112233441122334411223344"),
                  StrEq("0x45454545454545454545454545454545"),
                  StrEq("urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"), _));
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      stream_id, any_key_id, widevine_system_id_, iv, pssh_box));
}

// Verify that key_ids in pssh is optional.
TEST_P(WidevineSimpleHlsNotifierTest, NotifyEncryptionUpdateNoKeyidsInPssh) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kSampleAesProtectionScheme, mock_media_playlist, &notifier);

  const std::vector<uint8_t> iv(16, 0x45);

  media::WidevinePsshData widevine_pssh_data;
  widevine_pssh_data.set_provider("someprovider");
  widevine_pssh_data.set_content_id("contentid");
  std::string widevine_pssh_data_str = widevine_pssh_data.SerializeAsString();
  EXPECT_TRUE(!widevine_pssh_data_str.empty());
  std::vector<uint8_t> pssh_data(widevine_pssh_data_str.begin(),
                                 widevine_pssh_data_str.end());

  const char kExpectedJson[] =
      R"({"key_ids":["11223344112233441122334411223344"],)"
      R"("provider":"someprovider","content_id":"Y29udGVudGlk"})";
  std::string expected_json_base64;
  base::Base64Encode(kExpectedJson, &expected_json_base64);

  media::PsshBoxBuilder pssh_builder;
  pssh_builder.set_pssh_data(pssh_data);
  pssh_builder.set_system_id(widevine_system_id_.data(),
                             widevine_system_id_.size());

  std::string expected_pssh_base64;
  const std::vector<uint8_t> pssh_box = pssh_builder.CreateBox();
  base::Base64Encode(std::string(pssh_box.begin(), pssh_box.end()),
                     &expected_pssh_base64);

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_json_base64),
                  StrEq(""), StrEq("0x45454545454545454545454545454545"),
                  StrEq("com.widevine"), _))
      .Times(enable_legacy_widevine_hls_signaling_ ? 1 : 0);
  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_pssh_base64),
                  StrEq("0x11223344112233441122334411223344"),
                  StrEq("0x45454545454545454545454545454545"),
                  StrEq("urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"), _));
  const uint8_t kAnyKeyId[] = {
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
  };
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      stream_id,
      std::vector<uint8_t>(kAnyKeyId, kAnyKeyId + arraysize(kAnyKeyId)),
      widevine_system_id_, iv, pssh_box));
}

// Verify that when there are multiple key IDs in PSSH, the key ID that is
// passed to NotifyEncryptionUpdate() is the first key ID in the json format.
// Also verify that content_id is optional.
TEST_P(WidevineSimpleHlsNotifierTest, MultipleKeyIdsNoContentIdInPssh) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  uint32_t stream_id =
      SetupStream(kSampleAesProtectionScheme, mock_media_playlist, &notifier);

  std::vector<uint8_t> iv(16, 0x45);

  media::WidevinePsshData widevine_pssh_data;
  widevine_pssh_data.set_provider("someprovider");
  const uint8_t kFirstKeyId[] = {
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
  };
  const uint8_t kSecondKeyId[] = {
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
      0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  };
  std::vector<uint8_t> first_keyid(kFirstKeyId,
                                   kFirstKeyId + arraysize(kFirstKeyId));
  std::vector<uint8_t> second_keyid(kSecondKeyId,
                                    kSecondKeyId + arraysize(kSecondKeyId));

  widevine_pssh_data.add_key_id()->assign(kFirstKeyId,
                                          kFirstKeyId + arraysize(kFirstKeyId));
  widevine_pssh_data.add_key_id()->assign(
      kSecondKeyId, kSecondKeyId + arraysize(kSecondKeyId));
  std::string widevine_pssh_data_str = widevine_pssh_data.SerializeAsString();
  EXPECT_TRUE(!widevine_pssh_data_str.empty());
  std::vector<uint8_t> pssh_data(widevine_pssh_data_str.begin(),
                                 widevine_pssh_data_str.end());

  media::PsshBoxBuilder pssh_builder;
  pssh_builder.set_pssh_data(pssh_data);
  pssh_builder.set_system_id(widevine_system_id_.data(),
                             widevine_system_id_.size());
  pssh_builder.add_key_id(first_keyid);
  pssh_builder.add_key_id(second_keyid);

  const char kExpectedJson[] =
      R"({)"
      R"("key_ids":["22222222222222222222222222222222",)"
      R"("11111111111111111111111111111111"],)"
      R"("provider":"someprovider"})";
  std::string expected_json_base64;
  base::Base64Encode(kExpectedJson, &expected_json_base64);

  std::string expected_pssh_base64;
  const std::vector<uint8_t> pssh_box = pssh_builder.CreateBox();
  base::Base64Encode(std::string(pssh_box.begin(), pssh_box.end()),
                     &expected_pssh_base64);

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_json_base64),
                  StrEq(""), StrEq("0x45454545454545454545454545454545"),
                  StrEq("com.widevine"), _))
      .Times(enable_legacy_widevine_hls_signaling_ ? 1 : 0);

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_pssh_base64),
                  StrEq("0x22222222222222222222222222222222"),
                  StrEq("0x45454545454545454545454545454545"),
                  StrEq("urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"), _));

  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      stream_id,
      // Use the second key id here so that it will be thre first one in the
      // key_ids array in the JSON.
      second_keyid, widevine_system_id_, iv, pssh_box));
}

// If using 'cenc' with Widevine, don't output the json form.
TEST_P(WidevineSimpleHlsNotifierTest, CencEncryptionScheme) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kCencProtectionScheme, mock_media_playlist, &notifier);

  const std::vector<uint8_t> iv(16, 0x45);

  media::WidevinePsshData widevine_pssh_data;
  widevine_pssh_data.set_provider("someprovider");
  widevine_pssh_data.set_content_id("contentid");
  const uint8_t kAnyKeyId[] = {
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
  };
  std::vector<uint8_t> any_key_id(kAnyKeyId, kAnyKeyId + arraysize(kAnyKeyId));
  widevine_pssh_data.add_key_id()->assign(kAnyKeyId,
                                          kAnyKeyId + arraysize(kAnyKeyId));
  std::string widevine_pssh_data_str = widevine_pssh_data.SerializeAsString();

  EXPECT_TRUE(!widevine_pssh_data_str.empty());
  std::vector<uint8_t> pssh_data(widevine_pssh_data_str.begin(),
                                 widevine_pssh_data_str.end());

  std::string expected_pssh_base64;
  const std::vector<uint8_t> pssh_box = {'p', 's', 's', 'h'};
  base::Base64Encode(std::string(pssh_box.begin(), pssh_box.end()),
                     &expected_pssh_base64);

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_pssh_base64),
                  StrEq("0x11223344112233441122334411223344"),
                  StrEq("0x45454545454545454545454545454545"),
                  StrEq("urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"), _));
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      stream_id, any_key_id, widevine_system_id_, iv, pssh_box));
}

TEST_P(WidevineSimpleHlsNotifierTest, NotifyEncryptionUpdateEmptyIv) {
  // Pointer released by SimpleHlsNotifier.
  MockMediaPlaylist* mock_media_playlist =
      new MockMediaPlaylist("playlist.m3u8", "", "");
  SimpleHlsNotifier notifier(hls_params_);
  const uint32_t stream_id =
      SetupStream(kSampleAesProtectionScheme, mock_media_playlist, &notifier);

  media::WidevinePsshData widevine_pssh_data;
  widevine_pssh_data.set_provider("someprovider");
  widevine_pssh_data.set_content_id("contentid");
  const uint8_t kAnyKeyId[] = {
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
      0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44,
  };
  std::vector<uint8_t> any_key_id(kAnyKeyId, kAnyKeyId + arraysize(kAnyKeyId));
  widevine_pssh_data.add_key_id()->assign(kAnyKeyId,
                                          kAnyKeyId + arraysize(kAnyKeyId));
  std::string widevine_pssh_data_str = widevine_pssh_data.SerializeAsString();
  EXPECT_TRUE(!widevine_pssh_data_str.empty());
  std::vector<uint8_t> pssh_data(widevine_pssh_data_str.begin(),
                                 widevine_pssh_data_str.end());

  const char kExpectedJson[] =
      R"({"key_ids":["11223344112233441122334411223344"],)"
      R"("provider":"someprovider","content_id":"Y29udGVudGlk"})";
  std::string expected_json_base64;
  base::Base64Encode(kExpectedJson, &expected_json_base64);

  media::PsshBoxBuilder pssh_builder;
  pssh_builder.set_pssh_data(pssh_data);
  pssh_builder.set_system_id(widevine_system_id_.data(),
                             widevine_system_id_.size());
  pssh_builder.add_key_id(any_key_id);

  EXPECT_CALL(*mock_media_playlist,
              AddEncryptionInfo(
                  _, StrEq("data:text/plain;base64," + expected_json_base64),
                  StrEq(""), StrEq(""), StrEq("com.widevine"), StrEq("1")))
      .Times(enable_legacy_widevine_hls_signaling_ ? 1 : 0);

  EXPECT_CALL(
      *mock_media_playlist,
      AddEncryptionInfo(
          _,
          StrEq("data:text/plain;base64,"
                "AAAAS3Bzc2gAAAAA7e+"
                "LqXnWSs6jyCfc1R0h7QAAACsSEBEiM0QRIjNEESIzRBEiM0QaDHNvb"
                "WVwcm92aWRlciIJY29udGVudGlk"),
          StrEq("0x11223344112233441122334411223344"), StrEq(""),
          StrEq("urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"), StrEq("1")));
  std::vector<uint8_t> pssh_as_vec = pssh_builder.CreateBox();
  std::string pssh_in_string(pssh_as_vec.begin(), pssh_as_vec.end());
  std::string base_64_encoded_pssh;
  base::Base64Encode(pssh_in_string, &base_64_encoded_pssh);
  LOG(INFO) << base_64_encoded_pssh;

  std::vector<uint8_t> empty_iv;
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      stream_id,
      std::vector<uint8_t>(kAnyKeyId, kAnyKeyId + arraysize(kAnyKeyId)),
      widevine_system_id_, empty_iv, pssh_builder.CreateBox()));
}

INSTANTIATE_TEST_CASE_P(WidevineEnableDisableLegacyWidevineHls,
                        WidevineSimpleHlsNotifierTest,
                        ::testing::Bool());

}  // namespace hls
}  // namespace shaka
