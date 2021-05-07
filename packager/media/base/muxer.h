// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Defines the muxer interface.

#ifndef PACKAGER_MEDIA_BASE_MUXER_H_
#define PACKAGER_MEDIA_BASE_MUXER_H_

#include <memory>
#include <vector>

#include "packager/base/time/clock.h"
#include "packager/media/base/media_handler.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/event/progress_listener.h"
#include "packager/status.h"

namespace shaka {
namespace media {

class MediaSample;

/// Muxer is responsible for taking elementary stream samples and producing
/// media containers. An optional KeySource can be provided to Muxer
/// to generate encrypted outputs.
class Muxer : public MediaHandler {
 public:
  explicit Muxer(const MuxerOptions& options);
  virtual ~Muxer();

  /// Cancel a muxing job in progress. Will cause @a Run to exit with an error
  /// status of type CANCELLED.
  void Cancel();

  /// Set a MuxerListener event handler for this object.
  /// @param muxer_listener should not be NULL.
  void SetMuxerListener(std::unique_ptr<MuxerListener> muxer_listener);

  /// Set a ProgressListener event handler for this object.
  /// @param progress_listener should not be NULL.
  void SetProgressListener(std::unique_ptr<ProgressListener> progress_listener);

  const std::vector<std::shared_ptr<const StreamInfo>>& streams() const {
    return streams_;
  }

  /// Inject clock, mainly used for testing.
  /// The injected clock will be used to generate the creation time-stamp and
  /// modification time-stamp of the muxer output.
  /// If no clock is injected, the code uses base::Time::Now() to generate the
  /// time-stamps.
  /// @param clock is the Clock to be injected.
  void set_clock(base::Clock* clock) {
    clock_ = clock;
  }

 protected:
  /// @name MediaHandler implementation overrides.
  /// @{
  Status InitializeInternal() override { return Status::OK; }
  Status Process(std::unique_ptr<StreamData> stream_data) override;
  Status OnFlushRequest(size_t input_stream_index) override;
  /// @}

  const MuxerOptions& options() const { return options_; }
  MuxerListener* muxer_listener() { return muxer_listener_.get(); }
  ProgressListener* progress_listener() { return progress_listener_.get(); }
  base::Clock* clock() { return clock_; }

 private:
  Muxer(const Muxer&) = delete;
  Muxer& operator=(const Muxer&) = delete;

  // Initialize the muxer. InitializeMuxer may be called multiple times with
  // |options()| updated between calls, which is used to support separate file
  // per Representation per Period for Ad Insertion.
  virtual Status InitializeMuxer() = 0;

  // Final clean up.
  virtual Status Finalize() = 0;

  // Add a new media sample.  This does nothing by default; so subclasses that
  // handle media samples will need to replace this.
  virtual Status AddMediaSample(size_t stream_id, const MediaSample& sample);

  // Add a new text sample.  This does nothing by default; so subclasses that
  // handle text samples will need to replace this.
  virtual Status AddTextSample(size_t stream_id, const TextSample& sample);

  // Finalize the segment or subsegment.
  virtual Status FinalizeSegment(
      size_t stream_id,
      const SegmentInfo& segment_info) = 0;

  // Re-initialize Muxer. Could be called on StreamInfo or CueEvent.
  // |timestamp| may be used to set the output file name.
  Status ReinitializeMuxer(int64_t timestamp);

  MuxerOptions options_;
  std::vector<std::shared_ptr<const StreamInfo>> streams_;
  std::vector<uint8_t> current_key_id_;
  bool encryption_started_ = false;
  bool cancelled_ = false;

  std::unique_ptr<MuxerListener> muxer_listener_;
  std::unique_ptr<ProgressListener> progress_listener_;
  // An external injected clock, can be NULL.
  base::Clock* clock_ = nullptr;

  // In VOD single segment case with Ad Cues, |output_file_name| is allowed to
  // be a template. In this case, there will be NumAdCues + 1 files generated.
  std::string output_file_template_;
  size_t output_file_index_ = 1;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_BASE_MUXER_H_
