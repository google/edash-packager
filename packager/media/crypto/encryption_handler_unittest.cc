// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/crypto/encryption_handler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/media/base/aes_cryptor.h"
#include "packager/media/base/media_handler_test_base.h"
#include "packager/media/base/mock_aes_cryptor.h"
#include "packager/media/base/protection_system_ids.h"
#include "packager/media/base/raw_key_source.h"
#include "packager/media/crypto/aes_encryptor_factory.h"
#include "packager/media/crypto/subsample_generator.h"
#include "packager/status_test_util.h"

namespace shaka {
namespace media {
namespace {

using ::testing::_;
using ::testing::ByMove;
using ::testing::Combine;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::UnorderedElementsAre;
using ::testing::Values;
using ::testing::ValuesIn;
using ::testing::WithParamInterface;

const size_t kStreamIndex = 0;
const uint32_t kTimeScale = 1000;
const char kAudioStreamLabel[] = "AUDIO";
const char kSdVideoStreamLabel[] = "SD";

const uint8_t kKeyId[]{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
};
const uint8_t kKey[]{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
};
const uint8_t kIv[]{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
};

// The default KID for key rotation is all 0s.
const uint8_t kKeyRotationDefaultKeyId[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

class MockKeySource : public RawKeySource {
 public:
  MOCK_METHOD2(GetKey,
               Status(const std::string& stream_label, EncryptionKey* key));
  MOCK_METHOD4(GetCryptoPeriodKey,
               Status(uint32_t crypto_period_index,
                      uint32_t crypto_period_duration_in_seconds,
                      const std::string& stream_label,
                      EncryptionKey* key));
};

class MockSubsampleGenerator : public SubsampleGenerator {
 public:
  MockSubsampleGenerator() : SubsampleGenerator(true) {}

  MOCK_METHOD2(Initialize,
               Status(FourCC protection_scheme, const StreamInfo& stream_info));
  MOCK_METHOD3(GenerateSubsamples,
               Status(const uint8_t* frame,
                      size_t frame_size,
                      std::vector<SubsampleEntry>* subsamples));
};

class MockAesEncryptorFactory : public AesEncryptorFactory {
 public:
  MOCK_METHOD6(CreateEncryptor,
               std::unique_ptr<AesCryptor>(FourCC protection_scheme,
                                           uint8_t crypt_byte_block,
                                           uint8_t skip_byte_block,
                                           Codec codec,
                                           const std::vector<uint8_t>& key,
                                           const std::vector<uint8_t>& iv));
};

}  // namespace

class EncryptionHandlerTest : public MediaHandlerGraphTestBase {
 public:
  void SetUp() override { SetUpEncryptionHandler(EncryptionParams()); }

  void SetUpEncryptionHandler(const EncryptionParams& encryption_params) {
    EncryptionParams new_encryption_params = encryption_params;
    if (!encryption_params.stream_label_func) {
      // Setup default stream label function.
      new_encryption_params.stream_label_func =
          [](const EncryptionParams::EncryptedStreamAttributes&
                 stream_attributes) {
            if (stream_attributes.stream_type ==
                EncryptionParams::EncryptedStreamAttributes::kAudio) {
              return kAudioStreamLabel;
            }
            return kSdVideoStreamLabel;
          };
    }
    encryption_handler_.reset(
        new EncryptionHandler(new_encryption_params, &mock_key_source_));
    SetUpGraph(1 /* one input */, 1 /* one output */, encryption_handler_);
    // Inject default subsamples to avoid parsing problems.
    const std::vector<SubsampleEntry> empty_subsamples;
    InjectSubsamples(empty_subsamples);
  }

  Status Process(std::unique_ptr<StreamData> stream_data) {
    return encryption_handler_->Process(std::move(stream_data));
  }

  EncryptionKey GetMockEncryptionKey() {
    EncryptionKey encryption_key;
    encryption_key.key_id.assign(kKeyId, kKeyId + sizeof(kKeyId));
    encryption_key.key_ids.emplace_back(encryption_key.key_id);
    encryption_key.key.assign(kKey, kKey + sizeof(kKey));
    encryption_key.iv.assign(kIv, kIv + sizeof(kIv));
    return encryption_key;
  }

  void InjectSubsamples(const std::vector<SubsampleEntry>& subsamples) {
    std::unique_ptr<MockSubsampleGenerator> mock_generator(
        new MockSubsampleGenerator);
    EXPECT_CALL(*mock_generator, GenerateSubsamples(_, _, _))
        .WillRepeatedly(
            DoAll(SetArgPointee<2>(subsamples), Return(Status::OK)));

    encryption_handler_->InjectSubsampleGeneratorForTesting(
        std::move(mock_generator));
  }

  void InjectEncryptorFactoryForTesting(
      std::unique_ptr<AesEncryptorFactory> encryptor_factory) {
    encryption_handler_->InjectEncryptorFactoryForTesting(
        std::move(encryptor_factory));
  }

 protected:
  std::shared_ptr<EncryptionHandler> encryption_handler_;
  StrictMock<MockKeySource> mock_key_source_;
};

TEST_F(EncryptionHandlerTest, Initialize) {
  ASSERT_OK(encryption_handler_->Initialize());
}

TEST_F(EncryptionHandlerTest, OnlyOneOutput) {
  // Connecting another handler will fail.
  ASSERT_OK(encryption_handler_->AddHandler(some_handler()));
  ASSERT_EQ(error::INVALID_ARGUMENT,
            encryption_handler_->Initialize().error_code());
}

TEST_F(EncryptionHandlerTest, OnlyOneInput) {
  ASSERT_OK(some_handler()->AddHandler(encryption_handler_));
  ASSERT_EQ(error::INVALID_ARGUMENT,
            encryption_handler_->Initialize().error_code());
}

TEST_F(EncryptionHandlerTest, GetKeyFailed) {
  const EncryptionKey mock_encryption_key = GetMockEncryptionKey();
  EXPECT_CALL(mock_key_source_, GetKey(_, _))
      .WillOnce(Return(Status(error::INVALID_ARGUMENT, "")));

  ASSERT_NOT_OK(Process(StreamData::FromStreamInfo(
      kStreamIndex, GetVideoStreamInfo(kTimeScale, kCodecH264))));
}

TEST_F(EncryptionHandlerTest, CreateEncryptorFailed) {
  const EncryptionKey mock_encryption_key = GetMockEncryptionKey();
  EXPECT_CALL(mock_key_source_, GetKey(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(mock_encryption_key), Return(Status::OK)));

  std::unique_ptr<MockAesEncryptorFactory> mock_encryptor_factory(
      new MockAesEncryptorFactory);
  EXPECT_CALL(*mock_encryptor_factory,
              CreateEncryptor(_, _, _, _, mock_encryption_key.key,
                              mock_encryption_key.iv))
      .WillOnce(Return(ByMove(nullptr)));
  InjectEncryptorFactoryForTesting(std::move(mock_encryptor_factory));

  ASSERT_NOT_OK(Process(StreamData::FromStreamInfo(
      kStreamIndex, GetVideoStreamInfo(kTimeScale, kCodecH264))));
}

namespace {

const bool kIsKeyFrame = true;
const bool kIsSubsegment = true;
const bool kEncrypted = true;
const int64_t kSegmentDuration = 1000;

// The contents of the data does not matter.
const uint8_t kData[] = {0x00, 0x01, 0x02, 0x03, 0x04,
                         0x05, 0x06, 0x07, 0x08, 0x09};
const size_t kDataSize = sizeof(kData);

}  // namespace

class EncryptionHandlerEncryptionTest
    : public EncryptionHandlerTest,
      public WithParamInterface<std::tr1::tuple<FourCC, Codec>> {
 public:
  void SetUp() override {
    protection_scheme_ = std::tr1::get<0>(GetParam());
    codec_ = std::tr1::get<1>(GetParam());
  }

  uint8_t GetExpectedCryptByteBlock() {
    switch (protection_scheme_) {
      case FOURCC_cenc:
      case FOURCC_cbc1:
        return 0;
      case FOURCC_cens:
      case FOURCC_cbcs:
      case kAppleSampleAesProtectionScheme:
        return codec_ == kCodecAAC ? 0 : 1;
      default:
        return 0;
    }
  }

  uint8_t GetExpectedSkipByteBlock() {
    // Always use full sample encryption for audio.
    if (codec_ == kCodecAAC)
      return 0;
    switch (protection_scheme_) {
      case FOURCC_cenc:
      case FOURCC_cbc1:
        return 0;
      case FOURCC_cens:
      case FOURCC_cbcs:
      case kAppleSampleAesProtectionScheme:
        return 9;
      default:
        return 0;
    }
  }

  uint8_t GetExpectedPerSampleIvSize() {
    switch (protection_scheme_) {
      case FOURCC_cenc:
      case FOURCC_cens:
      case FOURCC_cbc1:
        return sizeof(kIv);
      case FOURCC_cbcs:
      case kAppleSampleAesProtectionScheme:
        return 0;
      default:
        return 0;
    }
  }

  std::vector<uint8_t> GetExpectedConstantIv() {
    switch (protection_scheme_) {
      case FOURCC_cbcs:
      case kAppleSampleAesProtectionScheme:
        return std::vector<uint8_t>(std::begin(kIv), std::end(kIv));
      default:
        return std::vector<uint8_t>();
    }
  }

 protected:
  FourCC protection_scheme_;
  Codec codec_;
};

TEST_P(EncryptionHandlerEncryptionTest, VerifyEncryptorFactoryParams) {
  EncryptionParams encryption_params;
  encryption_params.protection_scheme = protection_scheme_;
  SetUpEncryptionHandler(encryption_params);

  const EncryptionKey mock_encryption_key = GetMockEncryptionKey();
  EXPECT_CALL(mock_key_source_, GetKey(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(mock_encryption_key), Return(Status::OK)));

  std::unique_ptr<MockAesCryptor> mock_encryptor(new MockAesCryptor);
  std::unique_ptr<MockAesEncryptorFactory> mock_encryptor_factory(
      new MockAesEncryptorFactory);
  EXPECT_CALL(*mock_encryptor_factory,
              CreateEncryptor(protection_scheme_, GetExpectedCryptByteBlock(),
                              GetExpectedSkipByteBlock(), codec_,
                              mock_encryption_key.key, mock_encryption_key.iv))
      .WillOnce(Return(ByMove(std::move(mock_encryptor))));
  InjectEncryptorFactoryForTesting(std::move(mock_encryptor_factory));

  if (IsVideoCodec(codec_)) {
    ASSERT_OK(Process(StreamData::FromStreamInfo(
        kStreamIndex, GetVideoStreamInfo(kTimeScale, codec_))));
  } else {
    ASSERT_OK(Process(StreamData::FromStreamInfo(
        kStreamIndex, GetAudioStreamInfo(kTimeScale, codec_))));
  }
}

TEST_P(EncryptionHandlerEncryptionTest, ClearLeadWithNoKeyRotation) {
  const double kClearLeadInSeconds = 1.5 * kSegmentDuration / kTimeScale;
  EncryptionParams encryption_params;
  encryption_params.protection_scheme = protection_scheme_;
  encryption_params.clear_lead_in_seconds = kClearLeadInSeconds;
  SetUpEncryptionHandler(encryption_params);

  const EncryptionKey mock_encryption_key = GetMockEncryptionKey();
  EXPECT_CALL(mock_key_source_, GetKey(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(mock_encryption_key), Return(Status::OK)));

  if (IsVideoCodec(codec_)) {
    ASSERT_OK(Process(StreamData::FromStreamInfo(
        kStreamIndex, GetVideoStreamInfo(kTimeScale, codec_))));
  } else {
    ASSERT_OK(Process(StreamData::FromStreamInfo(
        kStreamIndex, GetAudioStreamInfo(kTimeScale, codec_))));
  }

  EXPECT_THAT(
      GetOutputStreamDataVector(),
      ElementsAre(IsStreamInfo(kStreamIndex, kTimeScale, kEncrypted, _)));
  const StreamInfo* stream_info =
      GetOutputStreamDataVector().back()->stream_info.get();
  ASSERT_TRUE(stream_info);
  EXPECT_TRUE(stream_info->has_clear_lead());
  EXPECT_THAT(stream_info->encryption_config(),
              MatchEncryptionConfig(
                  protection_scheme_, GetExpectedCryptByteBlock(),
                  GetExpectedSkipByteBlock(), GetExpectedPerSampleIvSize(),
                  GetExpectedConstantIv(), mock_encryption_key.key_id));
  ClearOutputStreamDataVector();
  Mock::VerifyAndClearExpectations(&mock_key_source_);

  // There are three segments. Only the third segment is encrypted.
  for (int i = 0; i < 3; ++i) {
    // Use single-frame segment for testing.
    ASSERT_OK(Process(StreamData::FromMediaSample(
        kStreamIndex, GetMediaSample(i * kSegmentDuration, kSegmentDuration,
                                     kIsKeyFrame, kData, kDataSize))));
    ASSERT_OK(Process(StreamData::FromSegmentInfo(
        kStreamIndex, GetSegmentInfo(i * kSegmentDuration, kSegmentDuration,
                                     !kIsSubsegment, i + 1))));
    const bool is_encrypted = i == 2;
    const auto& output_stream_data = GetOutputStreamDataVector();
    EXPECT_THAT(output_stream_data,
                ElementsAre(IsMediaSample(kStreamIndex, i * kSegmentDuration,
                                          kSegmentDuration, is_encrypted, _),
                            IsSegmentInfo(kStreamIndex, i * kSegmentDuration,
                                          kSegmentDuration, !kIsSubsegment,
                                          is_encrypted)));
    if (is_encrypted) {
      const auto* media_sample = output_stream_data.front()->media_sample.get();
      const auto* decrypt_config = media_sample->decrypt_config();
      EXPECT_EQ(std::vector<uint8_t>(kKeyId, kKeyId + sizeof(kKeyId)),
                decrypt_config->key_id());
      EXPECT_EQ(std::vector<uint8_t>(kIv, kIv + sizeof(kIv)),
                decrypt_config->iv());
      EXPECT_TRUE(decrypt_config->subsamples().empty());
      EXPECT_EQ(protection_scheme_, decrypt_config->protection_scheme());
      EXPECT_EQ(GetExpectedCryptByteBlock(),
                decrypt_config->crypt_byte_block());
      EXPECT_EQ(GetExpectedSkipByteBlock(), decrypt_config->skip_byte_block());
    }
    EXPECT_FALSE(output_stream_data.back()
                     ->segment_info->key_rotation_encryption_config);
    ClearOutputStreamDataVector();
  }
}

TEST_P(EncryptionHandlerEncryptionTest, ClearLeadWithKeyRotation) {
  const double kClearLeadInSeconds = 1.5 * kSegmentDuration / kTimeScale;
  const int kSegmentsPerCryptoPeriod = 2;  // 2 segments.
  const double kCryptoPeriodDurationInSeconds =
      kSegmentsPerCryptoPeriod * kSegmentDuration / kTimeScale;
  EncryptionParams encryption_params;
  encryption_params.protection_scheme = protection_scheme_;
  encryption_params.clear_lead_in_seconds = kClearLeadInSeconds;
  encryption_params.crypto_period_duration_in_seconds =
      kCryptoPeriodDurationInSeconds;
  SetUpEncryptionHandler(encryption_params);

  if (IsVideoCodec(codec_)) {
    ASSERT_OK(Process(StreamData::FromStreamInfo(
        kStreamIndex, GetVideoStreamInfo(kTimeScale, codec_))));
  } else {
    ASSERT_OK(Process(StreamData::FromStreamInfo(
        kStreamIndex, GetAudioStreamInfo(kTimeScale, codec_))));
  }

  EXPECT_THAT(
      GetOutputStreamDataVector(),
      ElementsAre(IsStreamInfo(kStreamIndex, kTimeScale, kEncrypted, _)));
  const StreamInfo* stream_info =
      GetOutputStreamDataVector().back()->stream_info.get();
  ASSERT_TRUE(stream_info);
  EXPECT_TRUE(stream_info->has_clear_lead());
  const EncryptionConfig& encryption_config = stream_info->encryption_config();
  EXPECT_EQ(protection_scheme_, encryption_config.protection_scheme);
  EXPECT_EQ(GetExpectedCryptByteBlock(), encryption_config.crypt_byte_block);
  EXPECT_EQ(GetExpectedSkipByteBlock(), encryption_config.skip_byte_block);
  EXPECT_EQ(std::vector<uint8_t>(std::begin(kKeyRotationDefaultKeyId),
                                 std::end(kKeyRotationDefaultKeyId)),
            encryption_config.key_id);
  ClearOutputStreamDataVector();

  // There are five segments with the first two not encrypted.
  for (int i = 0; i < 5; ++i) {
    if ((i % kSegmentsPerCryptoPeriod) == 0) {
      EXPECT_CALL(mock_key_source_,
                  GetCryptoPeriodKey(i / kSegmentsPerCryptoPeriod,
                                     kCryptoPeriodDurationInSeconds, _, _))
          .WillOnce(DoAll(SetArgPointee<3>(GetMockEncryptionKey()),
                          Return(Status::OK)));
    }
    // Use single-frame segment for testing.
    ASSERT_OK(Process(StreamData::FromMediaSample(
        kStreamIndex, GetMediaSample(i * kSegmentDuration, kSegmentDuration,
                                     kIsKeyFrame, kData, kDataSize))));
    ASSERT_OK(Process(StreamData::FromSegmentInfo(
        kStreamIndex, GetSegmentInfo(i * kSegmentDuration, kSegmentDuration,
                                     !kIsSubsegment, i))));
    const bool is_encrypted = i >= 2;
    const auto& output_stream_data = GetOutputStreamDataVector();
    EXPECT_THAT(output_stream_data,
                ElementsAre(IsMediaSample(kStreamIndex, i * kSegmentDuration,
                                          kSegmentDuration, is_encrypted, _),
                            IsSegmentInfo(kStreamIndex, i * kSegmentDuration,
                                          kSegmentDuration, !kIsSubsegment,
                                          is_encrypted)));
    EXPECT_THAT(*output_stream_data.back()
                     ->segment_info->key_rotation_encryption_config,
                MatchEncryptionConfig(
                    protection_scheme_, GetExpectedCryptByteBlock(),
                    GetExpectedSkipByteBlock(), GetExpectedPerSampleIvSize(),
                    GetExpectedConstantIv(), GetMockEncryptionKey().key_id));
    Mock::VerifyAndClearExpectations(&mock_key_source_);
    ClearOutputStreamDataVector();
  }
}

INSTANTIATE_TEST_CASE_P(ProtectionSchemes,
                        EncryptionHandlerEncryptionTest,
                        Combine(Values(kAppleSampleAesProtectionScheme,
                                       FOURCC_cenc,
                                       FOURCC_cens,
                                       FOURCC_cbc1,
                                       FOURCC_cbcs),
                                Values(kCodecAAC, kCodecH264)));

struct SubsampleTestCase {
  std::vector<SubsampleEntry> subsamples;
  std::vector<uint8_t> expected_output;
};

inline bool operator==(const SubsampleEntry& lhs, const SubsampleEntry& rhs) {
  return lhs.clear_bytes == rhs.clear_bytes &&
         lhs.cipher_bytes == rhs.cipher_bytes;
}

namespace {

const int64_t kSampleDuration = 1000;

// This mock encryption increases every byte by 0x10. See the function below.
const SubsampleTestCase kSubsampleTestCases[] = {
    {
        std::vector<SubsampleEntry>(),  // No subsamples, i.e. full sample
                                        // encrypted.
        {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19},
    },
    {
        {{8, 2}},  // One subsample.
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x18, 0x19},
    },
    {
        {{6, 2}, {2, 0}},  // Two subsamples.
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x16, 0x17, 0x08, 0x09},
    },
    {
        {{6, 2}, {0, 2}},  // Two subsamples.
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x16, 0x17, 0x18, 0x19},
    },
};

bool MockEncrypt(const uint8_t* text,
                 size_t text_size,
                 uint8_t* crypt_text,
                 size_t* crypt_text_size) {
  *crypt_text_size = text_size;
  for (size_t i = 0; i < text_size; i++)
    crypt_text[i] = text[i] + 0x10;
  return true;
}

}  // namespace

class EncryptionHandlerSubsampleTest
    : public EncryptionHandlerTest,
      public WithParamInterface<SubsampleTestCase> {};

INSTANTIATE_TEST_CASE_P(SubsampleTestCases,
                        EncryptionHandlerSubsampleTest,
                        ValuesIn(kSubsampleTestCases));

TEST_P(EncryptionHandlerSubsampleTest, SubsampleTest) {
  std::unique_ptr<MockAesCryptor> mock_encryptor(new MockAesCryptor);
  EXPECT_CALL(*mock_encryptor, CryptInternal(_, _, _, _))
      .WillRepeatedly(Invoke(MockEncrypt));
  ASSERT_TRUE(mock_encryptor->SetIv(
      std::vector<uint8_t>(std::begin(kIv), std::end(kIv))));

  std::unique_ptr<MockAesEncryptorFactory> mock_encryptor_factory(
      new MockAesEncryptorFactory);
  EXPECT_CALL(*mock_encryptor_factory, CreateEncryptor(_, _, _, _, _, _))
      .WillOnce(Return(ByMove(std::move(mock_encryptor))));
  InjectEncryptorFactoryForTesting(std::move(mock_encryptor_factory));

  InjectSubsamples(GetParam().subsamples);

  EXPECT_CALL(mock_key_source_, GetKey(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(GetMockEncryptionKey()), Return(Status::OK)));

  ASSERT_OK(Process(StreamData::FromStreamInfo(
      kStreamIndex, GetVideoStreamInfo(kTimeScale, kCodecH264))));
  ASSERT_OK(Process(StreamData::FromMediaSample(
      kStreamIndex,
      GetMediaSample(0, kSampleDuration, kIsKeyFrame, kData, kDataSize))));

  const auto& output_stream_data = GetOutputStreamDataVector();
  EXPECT_THAT(output_stream_data,
              ElementsAre(IsStreamInfo(kStreamIndex, kTimeScale, kEncrypted, _),
                          IsMediaSample(kStreamIndex, 0, kSampleDuration,
                                        kEncrypted, _)));

  const MediaSample& sample = *output_stream_data.back()->media_sample;
  EXPECT_EQ(
      GetParam().expected_output,
      std::vector<uint8_t>(sample.data(), sample.data() + sample.data_size()));

  const DecryptConfig& decrypt_config = *sample.decrypt_config();
  EXPECT_EQ(GetParam().subsamples, decrypt_config.subsamples());
}

class EncryptionHandlerTrackTypeTest : public EncryptionHandlerTest {};

TEST_F(EncryptionHandlerTrackTypeTest, AudioTrackType) {
  EncryptionParams::EncryptedStreamAttributes captured_stream_attributes;
  EncryptionParams encryption_params;
  encryption_params.stream_label_func =
      [&captured_stream_attributes](
          const EncryptionParams::EncryptedStreamAttributes&
              stream_attributes) {
        captured_stream_attributes = stream_attributes;
        return kAudioStreamLabel;
      };
  SetUpEncryptionHandler(encryption_params);
  EXPECT_CALL(mock_key_source_, GetKey(kAudioStreamLabel, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(GetMockEncryptionKey()), Return(Status::OK)));
  ASSERT_OK(Process(StreamData::FromStreamInfo(
      kStreamIndex, GetAudioStreamInfo(kTimeScale))));
  EXPECT_EQ(EncryptionParams::EncryptedStreamAttributes::kAudio,
            captured_stream_attributes.stream_type);
}

TEST_F(EncryptionHandlerTrackTypeTest, VideoTrackType) {
  const int32_t kWidth = 12;
  const int32_t kHeight = 34;
  EncryptionParams::EncryptedStreamAttributes captured_stream_attributes;
  EncryptionParams encryption_params;
  encryption_params.stream_label_func =
      [&captured_stream_attributes](
          const EncryptionParams::EncryptedStreamAttributes&
              stream_attributes) {
        captured_stream_attributes = stream_attributes;
        return kSdVideoStreamLabel;
      };
  SetUpEncryptionHandler(encryption_params);
  EXPECT_CALL(mock_key_source_, GetKey(kSdVideoStreamLabel, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(GetMockEncryptionKey()), Return(Status::OK)));
  ASSERT_OK(Process(StreamData::FromStreamInfo(
      kStreamIndex, GetVideoStreamInfo(kTimeScale, kWidth, kHeight))));
  EXPECT_EQ(EncryptionParams::EncryptedStreamAttributes::kVideo,
            captured_stream_attributes.stream_type);
  EXPECT_EQ(captured_stream_attributes.oneof.video.width, kWidth);
  EXPECT_EQ(captured_stream_attributes.oneof.video.height, kHeight);
}

class EncryptionHandlerPsshTest : public EncryptionHandlerTest {};

TEST_F(EncryptionHandlerPsshTest, GeneratesPssh) {
  EncryptionParams encryption_params;
  encryption_params.protection_scheme = FOURCC_cenc;
  encryption_params.protection_systems =
      ProtectionSystem::kWidevine | ProtectionSystem::kPlayReady;
  SetUpEncryptionHandler(encryption_params);

  const EncryptionKey mock_encryption_key = GetMockEncryptionKey();
  EXPECT_CALL(mock_key_source_, GetKey(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(mock_encryption_key), Return(Status::OK)));

  ASSERT_OK(Process(StreamData::FromStreamInfo(
      kStreamIndex, GetVideoStreamInfo(kTimeScale, kCodecH264))));

  EXPECT_THAT(GetOutputStreamDataVector(),
              ElementsAre(IsStreamInfo(_, kTimeScale, kEncrypted, _)));
  const StreamInfo* stream_info =
      GetOutputStreamDataVector().back()->stream_info.get();

  std::vector<uint8_t> widevine_system_id(
      kWidevineSystemId, kWidevineSystemId + arraysize(kWidevineSystemId));
  std::vector<uint8_t> playready_system_id(
      kPlayReadySystemId, kPlayReadySystemId + arraysize(kPlayReadySystemId));
  ASSERT_THAT(
      stream_info->encryption_config().key_system_info,
      UnorderedElementsAre(IsPsshInfoWithSystemId(widevine_system_id),
                           IsPsshInfoWithSystemId(playready_system_id)));
}

TEST_F(EncryptionHandlerPsshTest, UsesKeyInfoFirst) {
  EncryptionParams encryption_params;
  encryption_params.protection_scheme = FOURCC_cenc;
  encryption_params.protection_systems = ProtectionSystem::kWidevine;
  SetUpEncryptionHandler(encryption_params);

  std::vector<uint8_t> widevine_system_id(
      kWidevineSystemId, kWidevineSystemId + arraysize(kWidevineSystemId));
  EncryptionKey mock_encryption_key = GetMockEncryptionKey();
  ProtectionSystemSpecificInfo protection_info;
  protection_info.system_id = widevine_system_id;
  mock_encryption_key.key_system_info.emplace_back(protection_info);
  EXPECT_CALL(mock_key_source_, GetKey(_, _))
      .WillOnce(
          DoAll(SetArgPointee<1>(mock_encryption_key), Return(Status::OK)));

  ASSERT_OK(Process(StreamData::FromStreamInfo(
      kStreamIndex, GetVideoStreamInfo(kTimeScale, kCodecH264))));

  EXPECT_THAT(GetOutputStreamDataVector(),
              ElementsAre(IsStreamInfo(_, kTimeScale, kEncrypted, _)));
  const StreamInfo* stream_info =
      GetOutputStreamDataVector().back()->stream_info.get();

  ASSERT_THAT(stream_info->encryption_config().key_system_info,
              ElementsAre(IsPsshInfoWithSystemId(widevine_system_id)));
  // Should use above info, not generate new info.
  ASSERT_TRUE(
      stream_info->encryption_config().key_system_info[0].psshs.empty());
}

}  // namespace media
}  // namespace shaka
