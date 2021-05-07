// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_BASE_MEDIA_HANDLER_TEST_BASE_H_
#define PACKAGER_MEDIA_BASE_MEDIA_HANDLER_TEST_BASE_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "packager/base/strings/string_number_conversions.h"
#include "packager/media/base/media_handler.h"
#include "packager/media/base/video_stream_info.h"

namespace shaka {
namespace media {

std::string BoolToString(bool value);
std::string ToPrettyString(const std::string& str);

bool TryMatchStreamDataType(const StreamDataType& actual,
                            const StreamDataType& expected,
                            ::testing::MatchResultListener* listener);

bool TryMatchStreamType(const StreamType& actual,
                        const StreamType& expected,
                        ::testing::MatchResultListener* listener);

template <typename T, typename M>
bool TryMatch(const T& value,
              const M& matcher,
              ::testing::MatchResultListener* listener,
              const char* value_name) {
  if (!ExplainMatchResult(matcher, value, listener)) {
    // Need a space at the start of the string in the case that
    // it gets combined with another string.
    *listener << " Mismatch on " << value_name;
    return false;
  }

  return true;
}

MATCHER_P(IsPsshInfoWithSystemId,
          system_id,
          std::string(negation ? "doesn't " : "") + " have system ID " +
              testing::PrintToString(system_id)) {
  *result_listener << "which is (" << testing::PrintToString(arg.system_id)
                   << ")";
  return arg.system_id == system_id;
}

MATCHER_P4(IsStreamInfo, stream_index, time_scale, encrypted, language, "") {
  if (!TryMatchStreamDataType(arg->stream_data_type,
                              StreamDataType::kStreamInfo, result_listener)) {
    return false;
  }

  const std::string is_encrypted_string =
      BoolToString(arg->stream_info->is_encrypted());

  *result_listener << "which is (" << arg->stream_index << ", "
                   << arg->stream_info->time_scale() << ", "
                   << is_encrypted_string << ", "
                   << arg->stream_info->language() << ")";

  return TryMatch(arg->stream_index, stream_index, result_listener,
                  "stream_index") &&
         TryMatch(arg->stream_info->time_scale(), time_scale, result_listener,
                  "time_scale") &&
         TryMatch(arg->stream_info->is_encrypted(), encrypted, result_listener,
                  "is_encrypted") &&
         TryMatch(arg->stream_info->language(), language, result_listener,
                  "language");
}

MATCHER_P3(IsVideoStream, stream_index, trick_play_factor, playback_rate, "") {
  if (!TryMatchStreamDataType(arg->stream_data_type,
                              StreamDataType::kStreamInfo, result_listener)) {
    return false;
  }

  if (!TryMatchStreamType(arg->stream_info->stream_type(), kStreamVideo,
                          result_listener)) {
    return false;
  }

  const VideoStreamInfo* info =
      static_cast<const VideoStreamInfo*>(arg->stream_info.get());

  *result_listener << "which is (" << arg->stream_index << ", "
                   << info->trick_play_factor() << ", " << info->playback_rate()
                   << ")";

  return TryMatch(arg->stream_index, stream_index, result_listener,
                  "stream_index") &&
         TryMatch(info->trick_play_factor(), trick_play_factor, result_listener,
                  "trick_play_factor") &&
         TryMatch(info->playback_rate(), playback_rate, result_listener,
                  "playback_rate");
}

MATCHER_P5(IsSegmentInfo,
           stream_index,
           start_timestamp,
           duration,
           subsegment,
           encrypted,
           "") {
  if (!TryMatchStreamDataType(arg->stream_data_type,
                              StreamDataType::kSegmentInfo, result_listener)) {
    return false;
  }

  const std::string is_subsegment_string =
      BoolToString(arg->segment_info->is_subsegment);
  const std::string is_encrypted_string =
      BoolToString(arg->segment_info->is_encrypted);

  *result_listener << "which is (" << arg->stream_index << ", "
                   << arg->segment_info->start_timestamp << ", "
                   << arg->segment_info->duration << ", "
                   << is_subsegment_string << ", " << is_encrypted_string
                   << ")";

  return TryMatch(arg->stream_index, stream_index, result_listener,
                  "stream_index") &&
         TryMatch(arg->segment_info->start_timestamp, start_timestamp,
                  result_listener, "start_timestamp") &&
         TryMatch(arg->segment_info->duration, duration, result_listener,
                  "duration") &&
         TryMatch(arg->segment_info->is_subsegment, subsegment, result_listener,
                  "is_subsegment") &&
         TryMatch(arg->segment_info->is_encrypted, encrypted, result_listener,
                  "is_encrypted");
}

MATCHER_P6(MatchEncryptionConfig,
           protection_scheme,
           crypt_byte_block,
           skip_byte_block,
           per_sample_iv_size,
           constant_iv,
           key_id,
           "") {
  const std::string constant_iv_hex =
      base::HexEncode(arg.constant_iv.data(), arg.constant_iv.size());
  const std::string key_id_hex =
      base::HexEncode(arg.key_id.data(), arg.key_id.size());
  const std::string protection_scheme_as_string =
      FourCCToString(arg.protection_scheme);
  // Convert to integers so that they will print as a number and not a uint8_t
  // (char).
  const int crypt_byte_as_int = static_cast<int>(arg.crypt_byte_block);
  const int skip_byte_as_int = static_cast<int>(arg.skip_byte_block);

  *result_listener << "which is (" << protection_scheme_as_string << ", "
                   << crypt_byte_as_int << ", " << skip_byte_as_int << ", "
                   << arg.per_sample_iv_size << ", " << constant_iv_hex << ", "
                   << key_id_hex << ")";

  return TryMatch(arg.protection_scheme, protection_scheme, result_listener,
                  "protection_scheme") &&
         TryMatch(arg.crypt_byte_block, crypt_byte_block, result_listener,
                  "crypt_byte_block") &&
         TryMatch(arg.skip_byte_block, skip_byte_block, result_listener,
                  "skip_byte_block") &&
         TryMatch(arg.per_sample_iv_size, per_sample_iv_size, result_listener,
                  "per_sample_iv_size") &&
         TryMatch(arg.constant_iv, constant_iv, result_listener,
                  "constant_iv") &&
         TryMatch(arg.key_id, key_id, result_listener, "key_id");
}

MATCHER_P5(IsMediaSample,
           stream_index,
           timestamp,
           duration,
           encrypted,
           keyframe,
           "") {
  if (!TryMatchStreamDataType(arg->stream_data_type,
                              StreamDataType::kMediaSample, result_listener)) {
    return false;
  }

  const std::string is_encrypted_string =
      BoolToString(arg->media_sample->is_encrypted());
  const std::string is_key_frame_string =
      BoolToString(arg->media_sample->is_key_frame());

  *result_listener << "which is (" << arg->stream_index << ", "
                   << arg->media_sample->dts() << ", "
                   << arg->media_sample->duration() << ", "
                   << is_encrypted_string << ", " << is_key_frame_string << ")";

  return TryMatch(arg->stream_index, stream_index, result_listener,
                  "stream_index") &&
         TryMatch(arg->media_sample->dts(), timestamp, result_listener,
                  "dts") &&
         TryMatch(arg->media_sample->duration(), duration, result_listener,
                  "duration") &&
         TryMatch(arg->media_sample->is_encrypted(), encrypted, result_listener,
                  "is_encrypted") &&
         TryMatch(arg->media_sample->is_key_frame(), keyframe, result_listener,
                  "is_key_frame");
}

MATCHER_P4(IsTextSample, stream_index, id, start_time, end_time, "") {
  if (!TryMatchStreamDataType(arg->stream_data_type,
                              StreamDataType::kTextSample, result_listener)) {
    return false;
  }

  *result_listener << "which is (" << arg->stream_index << ", "
                   << ToPrettyString(arg->text_sample->id()) << ", "
                   << arg->text_sample->start_time() << ", "
                   << arg->text_sample->EndTime() << ")";

  return TryMatch(arg->stream_index, stream_index, result_listener,
                  "stream_index") &&
         TryMatch(arg->text_sample->id(), id, result_listener, "id") &&
         TryMatch(arg->text_sample->start_time(), start_time, result_listener,
                  "start_time") &&
         TryMatch(arg->text_sample->EndTime(), end_time, result_listener,
                  "EndTime");
}

MATCHER_P2(IsCueEvent, stream_index, time_in_seconds, "") {
  if (!TryMatchStreamDataType(arg->stream_data_type, StreamDataType::kCueEvent,
                              result_listener)) {
    return false;
  }

  *result_listener << "which is (" << arg->stream_index << ", "
                   << arg->cue_event->time_in_seconds << ")";

  return TryMatch(arg->stream_index, stream_index, result_listener,
                  "stream_index") &&
         TryMatch(arg->cue_event->time_in_seconds, time_in_seconds,
                  result_listener, "time_in_seconds");
}

class FakeInputMediaHandler : public MediaHandler {
 public:
  using MediaHandler::Dispatch;
  using MediaHandler::FlushAllDownstreams;
  using MediaHandler::FlushDownstream;

 private:
  bool ValidateOutputStreamIndex(size_t index) const override;
  Status InitializeInternal() override;
  Status Process(std::unique_ptr<StreamData> stream_data) override;
};

class MockOutputMediaHandler : public MediaHandler {
 public:
  MOCK_METHOD1(OnProcess, void(const StreamData*));
  MOCK_METHOD1(OnFlush, void(size_t index));

 private:
  Status InitializeInternal() override;
  Status Process(std::unique_ptr<StreamData> stream_data) override;
  Status OnFlushRequest(size_t index) override;
};

class CachingMediaHandler : public MediaHandler {
 public:
  const std::vector<std::unique_ptr<StreamData>>& Cache() const {
    return stream_data_vector_;
  }

  // TODO(vaage) : Remove the use of clear in our tests as it can make flow
  //               of the test harder to understand.
  void Clear() { stream_data_vector_.clear(); }

 private:
  Status InitializeInternal() override;
  Status Process(std::unique_ptr<StreamData> stream_data) override;
  Status OnFlushRequest(size_t input_stream_index) override;
  bool ValidateOutputStreamIndex(size_t stream_index) const override;

  std::vector<std::unique_ptr<StreamData>> stream_data_vector_;
};

class MediaHandlerTestBase : public ::testing::Test {
 public:
  MediaHandlerTestBase() = default;

 protected:
  bool IsVideoCodec(Codec codec) const;

  std::unique_ptr<StreamInfo> GetVideoStreamInfo(uint32_t time_scale) const;

  std::unique_ptr<StreamInfo> GetVideoStreamInfo(uint32_t time_scale,
                                                 uint32_t width,
                                                 uint64_t height) const;

  std::unique_ptr<StreamInfo> GetVideoStreamInfo(uint32_t time_scale,
                                                 Codec codec) const;

  std::unique_ptr<StreamInfo> GetVideoStreamInfo(uint32_t time_scale,
                                                 Codec codec,
                                                 uint32_t width,
                                                 uint64_t height) const;

  std::unique_ptr<StreamInfo> GetAudioStreamInfo(uint32_t time_scale) const;

  std::unique_ptr<StreamInfo> GetAudioStreamInfo(uint32_t time_scale,
                                                 Codec codec) const;

  std::shared_ptr<MediaSample> GetMediaSample(int64_t timestamp,
                                              int64_t duration,
                                              bool is_keyframe) const;

  std::shared_ptr<MediaSample> GetMediaSample(int64_t timestamp,
                                              int64_t duration,
                                              bool is_keyframe,
                                              const uint8_t* data,
                                              size_t data_length) const;

  std::unique_ptr<SegmentInfo> GetSegmentInfo(int64_t start_timestamp,
                                              int64_t duration,
                                              bool is_subsegment,
                                              int64_t segment_number) const;

  std::unique_ptr<StreamInfo> GetTextStreamInfo(uint32_t timescale) const;

  std::unique_ptr<TextSample> GetTextSample(const std::string& id,
                                            int64_t start,
                                            int64_t end,
                                            const std::string& payload) const;

  std::unique_ptr<CueEvent> GetCueEvent(double time_in_seconds) const;

  // Connect and initialize all handlers.
  Status SetUpAndInitializeGraph(std::shared_ptr<MediaHandler> handler,
                                 size_t input_count,
                                 size_t output_count);

  // Get the input handler at |index|. The values of |index| will match the
  // call to |AddInput|.
  FakeInputMediaHandler* Input(size_t index);

  // Get the output handler at |index|. The values of |index| will match the
  // call to |AddOutput|.
  MockOutputMediaHandler* Output(size_t index);

 private:
  MediaHandlerTestBase(const MediaHandlerTestBase&) = delete;
  MediaHandlerTestBase& operator=(const MediaHandlerTestBase&) = delete;

  std::shared_ptr<MediaHandler> handler_;

  std::vector<std::shared_ptr<FakeInputMediaHandler>> inputs_;
  std::vector<std::shared_ptr<MockOutputMediaHandler>> outputs_;
};

class MediaHandlerGraphTestBase : public MediaHandlerTestBase {
 public:
  MediaHandlerGraphTestBase();

 protected:
  /// Setup a graph using |handler| with |num_inputs| and |num_outputs|.
  void SetUpGraph(size_t num_inputs,
                  size_t num_outputs,
                  std::shared_ptr<MediaHandler> handler);

  /// @return the output stream data vector from handler.
  const std::vector<std::unique_ptr<StreamData>>& GetOutputStreamDataVector()
      const;

  /// Clear the output stream data vector.
  void ClearOutputStreamDataVector();

  /// @return some random handler that can be used for testing.
  std::shared_ptr<MediaHandler> some_handler() { return some_handler_; }

  /// @return some a downstream handler that can be used for connecting.
  std::shared_ptr<CachingMediaHandler> next_handler() { return next_handler_; }

 private:
  MediaHandlerGraphTestBase(const MediaHandlerGraphTestBase&) = delete;
  MediaHandlerGraphTestBase& operator=(const MediaHandlerGraphTestBase&) =
      delete;

  // Downstream handler used in testing graph.
  std::shared_ptr<CachingMediaHandler> next_handler_;
  // Some random handler which can be used for testing.
  std::shared_ptr<MediaHandler> some_handler_;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_BASE_MEDIA_HANDLER_TEST_BASE_H_
