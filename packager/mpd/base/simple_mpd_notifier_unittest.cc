// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gmock/gmock.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>

#include "packager/base/files/file_path.h"
#include "packager/base/files/file_util.h"
#include "packager/mpd/base/mock_mpd_builder.h"
#include "packager/mpd/base/mpd_builder.h"
#include "packager/mpd/base/mpd_options.h"
#include "packager/mpd/base/simple_mpd_notifier.h"
#include "packager/mpd/test/mpd_builder_test_helper.h"

namespace shaka {

using ::testing::_;
using ::testing::Eq;
using ::testing::Ref;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrEq;

namespace {
const uint32_t kDefaultPeriodId = 0u;
const double kDefaultPeriodStartTime = 0.0;
const uint32_t kDefaultTimeScale = 10;
const bool kContentProtectionInAdaptationSet = true;

MATCHER_P(EqualsProto, message, "") {
  return ::google::protobuf::util::MessageDifferencer::Equals(arg, message);
}

}  // namespace

class SimpleMpdNotifierTest : public ::testing::Test {
 protected:
  SimpleMpdNotifierTest()
      : default_mock_period_(
            new MockPeriod(kDefaultPeriodId, kDefaultPeriodStartTime)),
        default_mock_adaptation_set_(new MockAdaptationSet()) {}

  void SetUp() override {
    ASSERT_TRUE(base::CreateTemporaryFile(&temp_file_path_));
    empty_mpd_option_.mpd_params.mpd_output = temp_file_path_.AsUTF8Unsafe();

    // Three valid media info. The actual data does not matter.
    const char kValidMediaInfo[] =
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
    valid_media_info1_ = ConvertToMediaInfo(kValidMediaInfo);
    valid_media_info1_.set_reference_time_scale(kDefaultTimeScale);
    valid_media_info2_ = valid_media_info1_;
    valid_media_info2_.mutable_video_info()->set_width(960);
    valid_media_info3_ = valid_media_info1_;
    valid_media_info3_.mutable_video_info()->set_width(480);
  }

  void TearDown() override {
    base::DeleteFile(temp_file_path_, false /* non recursive, just 1 file */);
  }

  void SetMpdBuilder(SimpleMpdNotifier* notifier,
                     std::unique_ptr<MpdBuilder> mpd_builder) {
    notifier->SetMpdBuilderForTesting(std::move(mpd_builder));
  }

 protected:
  // Empty mpd options except with output path specified, so that
  // WriteMpdToFile() doesn't crash.
  MpdOptions empty_mpd_option_;
  const std::vector<std::string> empty_base_urls_;

  // Default mocks that can be used for the tests.
  std::unique_ptr<MockPeriod> default_mock_period_;
  std::unique_ptr<MockAdaptationSet> default_mock_adaptation_set_;

  // Three valid media info. The actual content does not matter.
  MediaInfo valid_media_info1_;
  MediaInfo valid_media_info2_;
  MediaInfo valid_media_info3_;

 private:
  base::FilePath temp_file_path_;
};

// Verify NotifyNewContainer() works as expected for VOD.
TEST_F(SimpleMpdNotifierTest, NotifyNewContainer) {
  SimpleMpdNotifier notifier(empty_mpd_option_);

  const uint32_t kRepresentationId = 1u;
  std::unique_ptr<MockMpdBuilder> mock_mpd_builder(new MockMpdBuilder());
  std::unique_ptr<MockRepresentation> mock_representation(
      new MockRepresentation(kRepresentationId));

  EXPECT_CALL(*mock_mpd_builder, GetOrCreatePeriod(_))
      .WillOnce(Return(default_mock_period_.get()));
  EXPECT_CALL(*default_mock_period_,
              GetOrCreateAdaptationSet(EqualsProto(valid_media_info1_), _))
      .WillOnce(Return(default_mock_adaptation_set_.get()));
  EXPECT_CALL(*default_mock_adaptation_set_,
              AddRepresentation(EqualsProto(valid_media_info1_)))
      .WillOnce(Return(mock_representation.get()));

  // This is for the Flush() below but adding expectation here because the next
  // std::move(lines) the pointer.
  EXPECT_CALL(*mock_mpd_builder, ToString(_)).WillOnce(Return(true));

  uint32_t unused_container_id;
  SetMpdBuilder(&notifier, std::move(mock_mpd_builder));
  EXPECT_TRUE(
      notifier.NotifyNewContainer(valid_media_info1_, &unused_container_id));
  EXPECT_TRUE(notifier.Flush());
}

TEST_F(SimpleMpdNotifierTest, NotifySampleDuration) {
  SimpleMpdNotifier notifier(empty_mpd_option_);

  const uint32_t kRepresentationId = 8u;
  std::unique_ptr<MockMpdBuilder> mock_mpd_builder(new MockMpdBuilder());
  std::unique_ptr<MockRepresentation> mock_representation(
      new MockRepresentation(kRepresentationId));

  EXPECT_CALL(*mock_mpd_builder, GetOrCreatePeriod(_))
      .WillOnce(Return(default_mock_period_.get()));
  EXPECT_CALL(*default_mock_period_, GetOrCreateAdaptationSet(_, _))
      .WillOnce(Return(default_mock_adaptation_set_.get()));
  EXPECT_CALL(*default_mock_adaptation_set_, AddRepresentation(_))
      .WillOnce(Return(mock_representation.get()));

  uint32_t container_id;
  SetMpdBuilder(&notifier, std::move(mock_mpd_builder));
  EXPECT_TRUE(notifier.NotifyNewContainer(valid_media_info1_, &container_id));
  EXPECT_EQ(kRepresentationId, container_id);

  const uint32_t kSampleDuration = 100;
  EXPECT_CALL(*mock_representation, SetSampleDuration(kSampleDuration));
  EXPECT_TRUE(
      notifier.NotifySampleDuration(kRepresentationId, kSampleDuration));
}

// This test is mainly for tsan. Using both the notifier and the MpdBuilder.
// Although locks in MpdBuilder have been removed,
// https://github.com/google/shaka-packager/issues/45
// This issue identified a bug where using SimpleMpdNotifier with multiple
// threads causes a deadlock.
TEST_F(SimpleMpdNotifierTest, NotifyNewContainerAndSampleDurationNoMock) {
  SimpleMpdNotifier notifier(empty_mpd_option_);
  uint32_t container_id;
  EXPECT_TRUE(notifier.NotifyNewContainer(valid_media_info1_, &container_id));
  const uint32_t kAnySampleDuration = 1000;
  EXPECT_TRUE(notifier.NotifySampleDuration(container_id, kAnySampleDuration));
  EXPECT_TRUE(notifier.Flush());
}

TEST_F(SimpleMpdNotifierTest, NotifyNewSegment) {
  SimpleMpdNotifier notifier(empty_mpd_option_);

  const uint32_t kRepresentationId = 447834u;
  const int64_t kSegmentNumber1 = 1;

  std::unique_ptr<MockMpdBuilder> mock_mpd_builder(new MockMpdBuilder());
  std::unique_ptr<MockRepresentation> mock_representation(
      new MockRepresentation(kRepresentationId));

  EXPECT_CALL(*mock_mpd_builder, GetOrCreatePeriod(_))
      .WillOnce(Return(default_mock_period_.get()));
  EXPECT_CALL(*default_mock_period_, GetOrCreateAdaptationSet(_, _))
      .WillOnce(Return(default_mock_adaptation_set_.get()));
  EXPECT_CALL(*default_mock_adaptation_set_, AddRepresentation(_))
      .WillOnce(Return(mock_representation.get()));

  uint32_t container_id;
  SetMpdBuilder(&notifier, std::move(mock_mpd_builder));
  EXPECT_TRUE(notifier.NotifyNewContainer(valid_media_info1_, &container_id));
  EXPECT_EQ(kRepresentationId, container_id);

  const uint64_t kStartTime = 0u;
  const uint32_t kSegmentDuration = 100u;
  const uint64_t kSegmentSize = 123456u;
  EXPECT_CALL(*mock_representation,
              AddNewSegment(kStartTime, kSegmentDuration, kSegmentSize,
                            kSegmentNumber1));

  EXPECT_TRUE(notifier.NotifyNewSegment(kRepresentationId, kStartTime,
                                        kSegmentDuration, kSegmentSize,
                                        kSegmentNumber1));
}

TEST_F(SimpleMpdNotifierTest, NotifyCueEvent) {
  SimpleMpdNotifier notifier(empty_mpd_option_);

  const uint32_t kRepresentationId = 123u;
  std::unique_ptr<MockMpdBuilder> mock_mpd_builder(new MockMpdBuilder());
  MockMpdBuilder* mock_mpd_builder_ptr = mock_mpd_builder.get();

  std::unique_ptr<MockPeriod> mock_period(
      new MockPeriod(kDefaultPeriodId, kDefaultPeriodStartTime));
  std::unique_ptr<MockAdaptationSet> mock_adaptation_set(
      new MockAdaptationSet());
  std::unique_ptr<MockRepresentation> mock_representation(
      new MockRepresentation(kRepresentationId));

  EXPECT_CALL(*mock_mpd_builder, GetOrCreatePeriod(Eq(0.0)))
      .WillOnce(Return(mock_period.get()));
  EXPECT_CALL(*mock_period,
              GetOrCreateAdaptationSet(EqualsProto(valid_media_info1_), _))
      .WillOnce(Return(mock_adaptation_set.get()));
  EXPECT_CALL(*mock_adaptation_set, AddRepresentation(_))
      .WillOnce(Return(mock_representation.get()));

  uint32_t container_id;
  SetMpdBuilder(&notifier, std::move(mock_mpd_builder));
  EXPECT_TRUE(notifier.NotifyNewContainer(valid_media_info1_, &container_id));
  EXPECT_EQ(kRepresentationId, container_id);

  const uint32_t kAnotherPeriodId = 2u;
  const double kArbitraryPeriodStartTime = 100;  // Value does not matter.
  std::unique_ptr<MockPeriod> mock_period2(
      new MockPeriod(kAnotherPeriodId, kArbitraryPeriodStartTime));
  std::unique_ptr<MockAdaptationSet> mock_adaptation_set2(
      new MockAdaptationSet());
  std::unique_ptr<MockRepresentation> mock_representation2(
      new MockRepresentation(kRepresentationId));

  const uint64_t kCueEventTimestamp = 1000;
  EXPECT_CALL(*mock_representation, GetMediaInfo())
      .WillOnce(ReturnRef(valid_media_info1_));
  EXPECT_CALL(*mock_mpd_builder_ptr,
              GetOrCreatePeriod(Eq(kCueEventTimestamp / kDefaultTimeScale)))
      .WillOnce(Return(mock_period2.get()));
  EXPECT_CALL(*mock_period2,
              GetOrCreateAdaptationSet(EqualsProto(valid_media_info1_), _))
      .WillOnce(Return(mock_adaptation_set2.get()));
  EXPECT_CALL(*mock_adaptation_set2,
              CopyRepresentation(Ref(*mock_representation)))
      .WillOnce(Return(mock_representation2.get()));
  EXPECT_TRUE(notifier.NotifyCueEvent(container_id, kCueEventTimestamp));
}

TEST_F(SimpleMpdNotifierTest,
       ContentProtectionInAdaptationSetUpdateEncryption) {
  MpdOptions mpd_options = empty_mpd_option_;
  mpd_options.mpd_params.generate_dash_if_iop_compliant_mpd =
      kContentProtectionInAdaptationSet;
  SimpleMpdNotifier notifier(mpd_options);

  const uint32_t kRepresentationId = 447834u;
  std::unique_ptr<MockMpdBuilder> mock_mpd_builder(new MockMpdBuilder());
  std::unique_ptr<MockRepresentation> mock_representation(
      new MockRepresentation(kRepresentationId));

  EXPECT_CALL(*mock_mpd_builder, GetOrCreatePeriod(_))
      .WillOnce(Return(default_mock_period_.get()));
  EXPECT_CALL(
      *default_mock_period_,
      GetOrCreateAdaptationSet(_, Eq(kContentProtectionInAdaptationSet)))
      .WillOnce(Return(default_mock_adaptation_set_.get()));
  EXPECT_CALL(*default_mock_adaptation_set_, AddRepresentation(_))
      .WillOnce(Return(mock_representation.get()));

  uint32_t container_id;
  SetMpdBuilder(&notifier, std::move(mock_mpd_builder));
  EXPECT_TRUE(notifier.NotifyNewContainer(valid_media_info1_, &container_id));

  ::testing::Mock::VerifyAndClearExpectations(
      default_mock_adaptation_set_.get());

  // "psshsomethingelse" as uint8 array.
  const uint8_t kBogusNewPssh[] = {0x70, 0x73, 0x73, 0x68, 0x73, 0x6f,
                                   0x6d, 0x65, 0x74, 0x68, 0x69, 0x6e,
                                   0x67, 0x65, 0x6c, 0x73, 0x65};
  const std::vector<uint8_t> kBogusNewPsshVector(
      kBogusNewPssh, kBogusNewPssh + arraysize(kBogusNewPssh));
  const char kBogusNewPsshInBase64[] = "cHNzaHNvbWV0aGluZ2Vsc2U=";

  EXPECT_CALL(*default_mock_adaptation_set_,
              UpdateContentProtectionPssh(StrEq("myuuid"),
                                          StrEq(kBogusNewPsshInBase64)));
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      container_id, "myuuid", std::vector<uint8_t>(), kBogusNewPsshVector));
}

TEST_F(SimpleMpdNotifierTest,
       ContentProtectionNotInAdaptationSetUpdateEncryption) {
  MpdOptions mpd_options = empty_mpd_option_;
  mpd_options.mpd_params.generate_dash_if_iop_compliant_mpd =
      !kContentProtectionInAdaptationSet;
  SimpleMpdNotifier notifier(mpd_options);

  const uint32_t kRepresentationId = 447834u;
  std::unique_ptr<MockMpdBuilder> mock_mpd_builder(new MockMpdBuilder());
  std::unique_ptr<MockRepresentation> mock_representation(
      new MockRepresentation(kRepresentationId));

  EXPECT_CALL(*mock_mpd_builder, GetOrCreatePeriod(_))
      .WillOnce(Return(default_mock_period_.get()));
  EXPECT_CALL(
      *default_mock_period_,
      GetOrCreateAdaptationSet(_, Eq(!kContentProtectionInAdaptationSet)))
      .WillOnce(Return(default_mock_adaptation_set_.get()));
  EXPECT_CALL(*default_mock_adaptation_set_, AddRepresentation(_))
      .WillOnce(Return(mock_representation.get()));

  uint32_t container_id;
  SetMpdBuilder(&notifier, std::move(mock_mpd_builder));
  EXPECT_TRUE(notifier.NotifyNewContainer(valid_media_info1_, &container_id));

  ::testing::Mock::VerifyAndClearExpectations(
      default_mock_adaptation_set_.get());

  // "psshsomethingelse" as uint8 array.
  const uint8_t kBogusNewPssh[] = {0x70, 0x73, 0x73, 0x68, 0x73, 0x6f,
                                   0x6d, 0x65, 0x74, 0x68, 0x69, 0x6e,
                                   0x67, 0x65, 0x6c, 0x73, 0x65};
  const std::vector<uint8_t> kBogusNewPsshVector(
      kBogusNewPssh, kBogusNewPssh + arraysize(kBogusNewPssh));
  const char kBogusNewPsshInBase64[] = "cHNzaHNvbWV0aGluZ2Vsc2U=";

  EXPECT_CALL(*mock_representation,
              UpdateContentProtectionPssh(StrEq("myuuid"),
                                          StrEq(kBogusNewPsshInBase64)));
  EXPECT_TRUE(notifier.NotifyEncryptionUpdate(
      container_id, "myuuid", std::vector<uint8_t>(), kBogusNewPsshVector));
}

// Test multiple media info with some belongs to the same AdaptationSets.
TEST_F(SimpleMpdNotifierTest, MultipleMediaInfo) {
  SimpleMpdNotifier notifier(empty_mpd_option_);
  std::unique_ptr<MockMpdBuilder> mock_mpd_builder(new MockMpdBuilder());

  std::unique_ptr<MockAdaptationSet> adaptation_set1(new MockAdaptationSet());
  std::unique_ptr<MockAdaptationSet> adaptation_set2(new MockAdaptationSet());

  std::unique_ptr<MockRepresentation> representation1(
      new MockRepresentation(1));
  std::unique_ptr<MockRepresentation> representation2(
      new MockRepresentation(2));
  std::unique_ptr<MockRepresentation> representation3(
      new MockRepresentation(3));

  EXPECT_CALL(*mock_mpd_builder, GetOrCreatePeriod(_))
      .Times(3)
      .WillRepeatedly(Return(default_mock_period_.get()));

  EXPECT_CALL(*default_mock_period_,
              GetOrCreateAdaptationSet(EqualsProto(valid_media_info1_), _))
      .WillOnce(Return(adaptation_set1.get()));
  EXPECT_CALL(*adaptation_set1,
              AddRepresentation(EqualsProto(valid_media_info1_)))
      .WillOnce(Return(representation1.get()));
  // Return the same adaptation set for |valid_media_info2_| and
  // |valid_media_info3_|. This results in AddRepresentation to be called twice
  // on |adaptation_set2|.
  EXPECT_CALL(*default_mock_period_,
              GetOrCreateAdaptationSet(EqualsProto(valid_media_info2_), _))
      .WillOnce(Return(adaptation_set2.get()));
  EXPECT_CALL(*adaptation_set2,
              AddRepresentation(EqualsProto(valid_media_info2_)))
      .WillOnce(Return(representation2.get()));
  EXPECT_CALL(*default_mock_period_,
              GetOrCreateAdaptationSet(EqualsProto(valid_media_info3_), _))
      .WillOnce(Return(adaptation_set2.get()));
  EXPECT_CALL(*adaptation_set2,
              AddRepresentation(EqualsProto(valid_media_info3_)))
      .WillOnce(Return(representation3.get()));

  uint32_t unused_container_id;
  SetMpdBuilder(&notifier, std::move(mock_mpd_builder));
  EXPECT_TRUE(
      notifier.NotifyNewContainer(valid_media_info1_, &unused_container_id));
  EXPECT_TRUE(
      notifier.NotifyNewContainer(valid_media_info2_, &unused_container_id));
  EXPECT_TRUE(
      notifier.NotifyNewContainer(valid_media_info3_, &unused_container_id));
}

}  // namespace shaka
