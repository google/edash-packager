// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_BASE_MEDIA_HANDLER_H_
#define PACKAGER_MEDIA_BASE_MEDIA_HANDLER_H_

#include <map>
#include <memory>
#include <utility>

#include "packager/media/base/media_sample.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/base/text_sample.h"
#include "packager/status.h"

namespace shaka {
namespace media {

enum class StreamDataType {
  kUnknown,
  kStreamInfo,
  kMediaSample,
  kTextSample,
  kSegmentInfo,
  kScte35Event,
  kCueEvent,
};

std::string StreamDataTypeToString(StreamDataType type);

// Scte35Event represents cuepoint markers in input streams. It will be used
// to represent out of band cuepoint markers too.
struct Scte35Event {
  std::string id;
  // Segmentation type id from SCTE35 segmentation descriptor.
  int type = 0;
  double start_time_in_seconds = 0;
  double duration_in_seconds = 0;
  std::string cue_data;
};

enum class CueEventType { kCueIn, kCueOut, kCuePoint };

// In server-based model, Chunking Handler consolidates SCTE-35 events and
// generates CueEvent before an ad is about to be inserted.
struct CueEvent {
  CueEventType type = CueEventType::kCuePoint;
  double time_in_seconds;
  std::string cue_data;
};

struct SegmentInfo {
  bool is_subsegment = false;
  bool is_encrypted = false;
  int64_t start_timestamp = -1;
  int64_t duration = 0;
  int64_t segment_number = 1;
  // This is only available if key rotation is enabled. Note that we may have
  // a |key_rotation_encryption_config| even if the segment is not encrypted,
  // which is the case for clear lead.
  std::shared_ptr<EncryptionConfig> key_rotation_encryption_config;
};

// TODO(kqyang): Should we use protobuf?
struct StreamData {
  size_t stream_index = static_cast<size_t>(-1);
  StreamDataType stream_data_type = StreamDataType::kUnknown;

  std::shared_ptr<const StreamInfo> stream_info;
  std::shared_ptr<const MediaSample> media_sample;
  std::shared_ptr<const TextSample> text_sample;
  std::shared_ptr<const SegmentInfo> segment_info;
  std::shared_ptr<const Scte35Event> scte35_event;
  std::shared_ptr<const CueEvent> cue_event;

  static std::unique_ptr<StreamData> FromStreamInfo(
      size_t stream_index,
      std::shared_ptr<const StreamInfo> stream_info) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->stream_data_type = StreamDataType::kStreamInfo;
    stream_data->stream_info = std::move(stream_info);
    return stream_data;
  }

  static std::unique_ptr<StreamData> FromMediaSample(
      size_t stream_index,
      std::shared_ptr<const MediaSample> media_sample) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->stream_data_type = StreamDataType::kMediaSample;
    stream_data->media_sample = std::move(media_sample);
    return stream_data;
  }

  static std::unique_ptr<StreamData> FromTextSample(
      size_t stream_index,
      std::shared_ptr<const TextSample> text_sample) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->stream_data_type = StreamDataType::kTextSample;
    stream_data->text_sample = std::move(text_sample);
    return stream_data;
  }

  static std::unique_ptr<StreamData> FromSegmentInfo(
      size_t stream_index,
      std::shared_ptr<const SegmentInfo> segment_info) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->stream_data_type = StreamDataType::kSegmentInfo;
    stream_data->segment_info = std::move(segment_info);
    return stream_data;
  }

  static std::unique_ptr<StreamData> FromScte35Event(
      size_t stream_index,
      std::shared_ptr<const Scte35Event> scte35_event) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->stream_data_type = StreamDataType::kScte35Event;
    stream_data->scte35_event = std::move(scte35_event);
    return stream_data;
  }

  static std::unique_ptr<StreamData> FromCueEvent(
      size_t stream_index,
      std::shared_ptr<const CueEvent> cue_event) {
    std::unique_ptr<StreamData> stream_data(new StreamData);
    stream_data->stream_index = stream_index;
    stream_data->stream_data_type = StreamDataType::kCueEvent;
    stream_data->cue_event = std::move(cue_event);
    return stream_data;
  }
};

/// MediaHandler is the base media processing unit. Media handlers transform
/// the input streams and propagate the outputs to downstream media handlers.
/// There are three different types of media handlers:
///   1) Single input single output
///      This is the most basic handler. It only supports one input and one
///      output with both index as 0.
///   2) Multiple inputs multiple outputs
///      The number of outputs must be equal to the number of inputs. The
///      output stream at a specific index comes from the input stream at the
///      same index. Different streams usually share a common resource, although
///      they may be independent. One example of this is encryption handler.
///   3) Single input multiple outputs
///      The input stream is split into multiple output streams. One example
///      of this is the replicator media handler.
/// Other types of media handlers are disallowed and not supported.
class MediaHandler {
 public:
  MediaHandler() = default;
  virtual ~MediaHandler() = default;

  /// Connect downstream handler at the specified output stream index.
  Status SetHandler(size_t output_stream_index,
                    std::shared_ptr<MediaHandler> handler);

  /// Connect downstream handler to the next available output stream index.
  Status AddHandler(std::shared_ptr<MediaHandler> handler) {
    return SetHandler(next_output_stream_index_, handler);
  }

  /// Initialize the handler and downstream handlers. Note that it should be
  /// called after setting up the graph before running the graph.
  Status Initialize();

  /// Validate if the handler is connected to its upstream handler.
  bool IsConnected() { return num_input_streams_ > 0; }

  static Status Chain(const std::vector<std::shared_ptr<MediaHandler>>& list);

 protected:
  /// Internal implementation of initialize. Note that it should only initialize
  /// the MediaHandler itself. Downstream handlers are handled in Initialize().
  virtual Status InitializeInternal() = 0;

  /// Process the incoming stream data. Note that (1) stream_data.stream_index
  /// should be the input stream index; (2) The implementation needs to call
  /// DispatchXxx to dispatch the processed stream data to the downstream
  /// handlers after finishing processing if needed.
  virtual Status Process(std::unique_ptr<StreamData> stream_data) = 0;

  /// Event handler for flush request at the specific input stream index.
  virtual Status OnFlushRequest(size_t input_stream_index);

  /// Validate if the stream at the specified index actually exists.
  virtual bool ValidateOutputStreamIndex(size_t stream_index) const;

  /// Dispatch the stream data to downstream handlers. Note that
  /// stream_data.stream_index should be the output stream index.
  Status Dispatch(std::unique_ptr<StreamData> stream_data) const;

  /// Dispatch the stream info to downstream handlers.
  Status DispatchStreamInfo(
      size_t stream_index,
      std::shared_ptr<const StreamInfo> stream_info) const {
    return Dispatch(
        StreamData::FromStreamInfo(stream_index, std::move(stream_info)));
  }

  /// Dispatch the media sample to downstream handlers.
  Status DispatchMediaSample(
      size_t stream_index,
      std::shared_ptr<const MediaSample> media_sample) const {
    return Dispatch(
        StreamData::FromMediaSample(stream_index, std::move(media_sample)));
  }

  /// Dispatch the text sample to downstream handlers.
  // DispatchTextSample should only be override for testing.
  Status DispatchTextSample(
      size_t stream_index,
      std::shared_ptr<const TextSample> text_sample) const {
    return Dispatch(
        StreamData::FromTextSample(stream_index, std::move(text_sample)));
  }

  /// Dispatch the segment info to downstream handlers.
  Status DispatchSegmentInfo(
      size_t stream_index,
      std::shared_ptr<const SegmentInfo> segment_info) const {
    return Dispatch(
        StreamData::FromSegmentInfo(stream_index, std::move(segment_info)));
  }

  /// Dispatch the scte35 event to downstream handlers.
  Status DispatchScte35Event(
      size_t stream_index,
      std::shared_ptr<const Scte35Event> scte35_event) const {
    return Dispatch(
        StreamData::FromScte35Event(stream_index, std::move(scte35_event)));
  }

  /// Dispatch the cue event to downstream handlers.
  Status DispatchCueEvent(size_t stream_index,
                          std::shared_ptr<const CueEvent> cue_event) const {
    return Dispatch(
        StreamData::FromCueEvent(stream_index, std::move(cue_event)));
  }

  /// Flush the downstream connected at the specified output stream index.
  Status FlushDownstream(size_t output_stream_index);

  /// Flush all connected downstream handlers.
  Status FlushAllDownstreams();

  bool initialized() { return initialized_; }
  size_t num_input_streams() const { return num_input_streams_; }
  size_t next_output_stream_index() const { return next_output_stream_index_; }
  const std::map<size_t, std::pair<std::shared_ptr<MediaHandler>, size_t>>&
  output_handlers() {
    return output_handlers_;
  }

 private:
  MediaHandler(const MediaHandler&) = delete;
  MediaHandler& operator=(const MediaHandler&) = delete;

  bool initialized_ = false;
  // Number of input streams.
  size_t num_input_streams_ = 0;
  // The next available output stream index, used by AddHandler.
  size_t next_output_stream_index_ = 0;
  // output stream index -> {output handler, output handler input stream index}
  // map.
  std::map<size_t, std::pair<std::shared_ptr<MediaHandler>, size_t>>
      output_handlers_;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_BASE_MEDIA_HANDLER_H_
