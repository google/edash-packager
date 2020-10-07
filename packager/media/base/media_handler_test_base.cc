// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/base/media_handler_test_base.h"

#include "packager/media/base/audio_stream_info.h"
#include "packager/media/base/text_stream_info.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/status_test_util.h"

namespace {

const int kTrackId = 1;
const uint64_t kDuration = 10000;
const char kCodecString[] = "codec string";
const uint8_t kSampleBits = 1;
const uint8_t kNumChannels = 2;
const uint32_t kSamplingFrequency = 48000;
const uint64_t kSeekPrerollNs = 12345;
const uint64_t kCodecDelayNs = 56789;
const uint32_t kMaxBitrate = 13579;
const uint32_t kAvgBitrate = 13000;
const char kLanguage[] = "eng";
const uint16_t kWidth = 10u;
const uint16_t kHeight = 20u;
const uint32_t kPixelWidth = 2u;
const uint32_t kPixelHeight = 3u;
const uint8_t kTransferCharacteristics = 0;
const int16_t kTrickPlayFactor = 0;
const uint8_t kNaluLengthSize = 1u;
const bool kEncrypted = true;

// Use H264 code config.
const uint8_t kCodecConfig[]{
    // clang-format off
    // Header
    0x01, 0x64, 0x00, 0x1e, 0xff,
    // SPS count (ignore top three bits)
    0xe1,
    // SPS
    0x00, 0x19,  // Size
    0x67, 0x64, 0x00, 0x1e, 0xac, 0xd9, 0x40, 0xa0, 0x2f, 0xf9, 0x70, 0x11,
    0x00, 0x00, 0x03, 0x03, 0xe9, 0x00, 0x00, 0xea, 0x60, 0x0f, 0x16, 0x2d,
    0x96,
    // PPS count
    0x01,
    // PPS
    0x00, 0x06,  // Size
    0x68, 0xeb, 0xe3, 0xcb, 0x22, 0xc0,
    // clang-format on
};

// Mock data, we don't really care about what is inside.
const uint8_t kData[]{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
};

}  // namespace

namespace shaka {
namespace media {

std::string BoolToString(bool value) {
  return value ? "true" : "false";
}

bool TryMatchStreamDataType(const StreamDataType& actual,
                            const StreamDataType& expected,
                            ::testing::MatchResultListener* listener) {
  if (actual != expected) {
    std::string expected_as_string = StreamDataTypeToString(expected);
    std::string actual_as_string = StreamDataTypeToString(actual);

    *listener << "which is " << actual_as_string << " (expected "
              << expected_as_string << ")";
    return false;
  }

  return true;
}

bool TryMatchStreamType(const StreamType& actual,
                        const StreamType& expected,
                        ::testing::MatchResultListener* listener) {
  if (actual != expected) {
    std::string expected_as_string = StreamTypeToString(expected);
    std::string actual_as_string = StreamTypeToString(actual);

    *listener << "which is " << actual_as_string << " (expected "
              << expected_as_string << ")";
    return false;
  }

  return true;
}

std::string ToPrettyString(const std::string& str) {
  std::string out;

  // Opening quotation.
  out.push_back('"');

  for (char c : str) {
    if (isspace(c)) {
      // Make all white space characters spaces to avoid print issues in
      // the terminal.
      out.push_back(' ');
    } else if (isalnum(c)) {
      // If the character is alpha-numeric, then print it as is. Just using
      // these characters, it should be enough to understand the string.
      out.push_back(c);
    } else {
      // Replace all other characters with '.'. This is to avoid print issues
      // (e.g. \n) or readability issues (e.g. ").
      out.push_back('.');
    }
  }

  // Closing quotation.
  out.push_back('"');

  return out;
}

bool FakeInputMediaHandler::ValidateOutputStreamIndex(size_t index) const {
  return true;
}

Status FakeInputMediaHandler::InitializeInternal() {
  return Status::OK;
}

Status FakeInputMediaHandler::Process(std::unique_ptr<StreamData> stream_data) {
  return Status(error::INTERNAL_ERROR,
                "FakeInputMediaHandler should never be a downstream handler.");
}

Status MockOutputMediaHandler::InitializeInternal() {
  return Status::OK;
}

Status MockOutputMediaHandler::Process(
    std::unique_ptr<StreamData> stream_data) {
  OnProcess(stream_data.get());
  return Status::OK;
}

Status MockOutputMediaHandler::OnFlushRequest(size_t index) {
  OnFlush(index);
  return Status::OK;
}

Status CachingMediaHandler::InitializeInternal() {
  return Status::OK;
}

Status CachingMediaHandler::Process(std::unique_ptr<StreamData> stream_data) {
  stream_data_vector_.push_back(std::move(stream_data));
  return Status::OK;
}

Status CachingMediaHandler::OnFlushRequest(size_t input_stream_index) {
  return Status::OK;
}

bool CachingMediaHandler::ValidateOutputStreamIndex(size_t stream_index) const {
  return true;
}

bool MediaHandlerTestBase::IsVideoCodec(Codec codec) const {
  return codec >= kCodecVideo && codec < kCodecVideoMaxPlusOne;
}

std::unique_ptr<StreamInfo> MediaHandlerTestBase::GetVideoStreamInfo(
    uint32_t time_scale) const {
  return GetVideoStreamInfo(time_scale, kCodecVP9, kWidth, kHeight);
}

std::unique_ptr<StreamInfo> MediaHandlerTestBase::GetVideoStreamInfo(
    uint32_t time_scale,
    uint32_t width,
    uint64_t height) const {
  return GetVideoStreamInfo(time_scale, kCodecVP9, width, height);
}

std::unique_ptr<StreamInfo> MediaHandlerTestBase::GetVideoStreamInfo(
    uint32_t time_scale,
    Codec codec) const {
  return GetVideoStreamInfo(time_scale, codec, kWidth, kHeight);
}

std::unique_ptr<StreamInfo> MediaHandlerTestBase::GetVideoStreamInfo(
    uint32_t time_scale,
    Codec codec,
    uint32_t width,
    uint64_t height) const {
  return std::unique_ptr<VideoStreamInfo>(new VideoStreamInfo(
      kTrackId, time_scale, kDuration, codec, H26xStreamFormat::kUnSpecified,
      kCodecString, kCodecConfig, sizeof(kCodecConfig), width, height,
      kPixelWidth, kPixelHeight, kTransferCharacteristics, kTrickPlayFactor,
      kNaluLengthSize, kLanguage, !kEncrypted));
}

std::unique_ptr<StreamInfo> MediaHandlerTestBase::GetAudioStreamInfo(
    uint32_t time_scale) const {
  return GetAudioStreamInfo(time_scale, kCodecAAC);
}

std::unique_ptr<StreamInfo> MediaHandlerTestBase::GetAudioStreamInfo(
    uint32_t time_scale,
    Codec codec) const {
  return std::unique_ptr<AudioStreamInfo>(new AudioStreamInfo(
      kTrackId, time_scale, kDuration, codec, kCodecString, kCodecConfig,
      sizeof(kCodecConfig), kSampleBits, kNumChannels, kSamplingFrequency,
      kSeekPrerollNs, kCodecDelayNs, kMaxBitrate, kAvgBitrate, kLanguage,
      !kEncrypted));
}

std::shared_ptr<MediaSample> MediaHandlerTestBase::GetMediaSample(
    int64_t timestamp,
    int64_t duration,
    bool is_keyframe) const {
  return GetMediaSample(timestamp, duration, is_keyframe, kData, sizeof(kData));
}

std::shared_ptr<MediaSample> MediaHandlerTestBase::GetMediaSample(
    int64_t timestamp,
    int64_t duration,
    bool is_keyframe,
    const uint8_t* data,
    size_t data_length) const {
  std::shared_ptr<MediaSample> sample =
      MediaSample::CopyFrom(data, data_length, nullptr, 0, is_keyframe);
  sample->set_dts(timestamp);
  sample->set_pts(timestamp);
  sample->set_duration(duration);

  return sample;
}

std::unique_ptr<SegmentInfo> MediaHandlerTestBase::GetSegmentInfo(
    int64_t start_timestamp,
    int64_t duration,
    bool is_subsegment) const {
  std::unique_ptr<SegmentInfo> info(new SegmentInfo);
  info->start_timestamp = start_timestamp;
  info->duration = duration;
  info->is_subsegment = is_subsegment;
  info->segment_index = start_timestamp / duration;

  return info;
}

std::unique_ptr<StreamInfo> MediaHandlerTestBase::GetTextStreamInfo(
    uint32_t timescale) const {
  // None of this information is actually used by the text out handler.
  // The stream info is just needed to signal the start of the stream.
  return std::unique_ptr<StreamInfo>(
      new TextStreamInfo(0, timescale, 0, kUnknownCodec, "", "", 0, 0, ""));
}

std::unique_ptr<TextSample> MediaHandlerTestBase::GetTextSample(
    const std::string& id,
    int64_t start,
    int64_t end,
    const std::string& payload) const {
  std::unique_ptr<TextSample> sample(new TextSample);
  sample->set_id(id);
  sample->SetTime(start, end);
  sample->AppendPayload(payload);

  return sample;
}

std::unique_ptr<CueEvent> MediaHandlerTestBase::GetCueEvent(
    double time_in_seconds) const {
  std::unique_ptr<CueEvent> event(new CueEvent);
  event->time_in_seconds = time_in_seconds;

  return event;
}

Status MediaHandlerTestBase::SetUpAndInitializeGraph(
    std::shared_ptr<MediaHandler> handler,
    size_t input_count,
    size_t output_count) {
  DCHECK(handler);
  DCHECK_EQ(nullptr, handler_);
  DCHECK(inputs_.empty());
  DCHECK(outputs_.empty());

  handler_ = std::move(handler);

  Status status;

  // Add and connect all the requested inputs.
  for (size_t i = 0; i < input_count; i++) {
    inputs_.emplace_back(new FakeInputMediaHandler);
  }

  for (auto& input : inputs_) {
    status.Update(input->AddHandler(handler_));
  }

  if (!status.ok()) {
    return status;
  }

  // Add and connect all the requested outputs.
  for (size_t i = 0; i < output_count; i++) {
    outputs_.emplace_back(new testing::NiceMock<MockOutputMediaHandler>);
  }

  for (auto& output : outputs_) {
    status.Update(handler_->AddHandler(output));
  }

  if (!status.ok()) {
    return status;
  }

  // Initialize the graph.
  for (auto& input : inputs_) {
    status.Update(input->Initialize());
  }

  // In the case that there are no inputs, the start of the graph
  // is at |handler_| so it needs to be initialized or else the graph
  // won't be initialized.
  if (inputs_.empty()) {
    status.Update(handler_->Initialize());
  }

  return status;
}

FakeInputMediaHandler* MediaHandlerTestBase::Input(size_t index) {
  DCHECK_LT(index, inputs_.size());
  return inputs_[index].get();
}

MockOutputMediaHandler* MediaHandlerTestBase::Output(size_t index) {
  DCHECK_LT(index, outputs_.size());
  return outputs_[index].get();
}

MediaHandlerGraphTestBase::MediaHandlerGraphTestBase()
    : next_handler_(new CachingMediaHandler),
      some_handler_(new CachingMediaHandler) {}

void MediaHandlerGraphTestBase::SetUpGraph(
    size_t num_inputs,
    size_t num_outputs,
    std::shared_ptr<MediaHandler> handler) {
  // Input handler is not really used anywhere else except to validate number of
  // allowed inputs for the handler to be tested.
  auto input_handler = std::make_shared<CachingMediaHandler>();
  for (size_t i = 0; i < num_inputs; ++i)
    ASSERT_OK(input_handler->SetHandler(i, handler));
  // All outputs are routed to |next_handler_|.
  for (size_t i = 0; i < num_outputs; ++i)
    ASSERT_OK(handler->SetHandler(i, next_handler_));
}

const std::vector<std::unique_ptr<StreamData>>&
MediaHandlerGraphTestBase::GetOutputStreamDataVector() const {
  return next_handler_->Cache();
}

void MediaHandlerGraphTestBase::ClearOutputStreamDataVector() {
  next_handler_->Clear();
}

}  // namespace media
}  // namespace shaka
