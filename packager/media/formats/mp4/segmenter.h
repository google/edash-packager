// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP4_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_MP4_SEGMENTER_H_

#include <map>
#include <memory>
#include <vector>

#include "packager/base/optional.h"
#include "packager/media/base/fourccs.h"
#include "packager/media/base/range.h"
#include "packager/media/formats/mp4/box_definitions.h"
#include "packager/status.h"

namespace shaka {
namespace media {

struct EncryptionConfig;
struct MuxerOptions;
struct SegmentInfo;

class BufferWriter;
class MediaSample;
class MuxerListener;
class ProgressListener;
class StreamInfo;

namespace mp4 {

class Fragmenter;
struct KeyFrameInfo;

/// This class defines the Segmenter which is responsible for organizing
/// fragments into segments/subsegments and package them into a MP4 file.
/// Inherited by MultiSegmentSegmenter and SingleSegmentSegmenter.
/// SingleSegmentSegmenter defines the Segmenter for DASH Video-On-Demand with
/// a single segment for each media presentation while MultiSegmentSegmenter
/// handles all other cases including DASH live profile.
class Segmenter {
 public:
  Segmenter(const MuxerOptions& options,
            std::unique_ptr<FileType> ftyp,
            std::unique_ptr<Movie> moov);
  virtual ~Segmenter();

  /// Initialize the segmenter.
  /// Calling other public methods of this class without this method returning
  /// Status::OK results in an undefined behavior.
  /// @param streams contains the vector of StreamInfos for initialization.
  /// @param muxer_listener receives muxer events. Can be NULL.
  /// @param progress_listener receives progress updates. Can be NULL.
  /// @return OK on success, an error status otherwise.
  Status Initialize(
      const std::vector<std::shared_ptr<const StreamInfo>>& streams,
      MuxerListener* muxer_listener,
      ProgressListener* progress_listener);

  /// Finalize the segmenter.
  /// @return OK on success, an error status otherwise.
  Status Finalize();

  /// Add sample to the indicated stream.
  /// @param stream_id is the zero-based stream index.
  /// @param sample points to the sample to be added.
  /// @return OK on success, an error status otherwise.
  Status AddSample(size_t stream_id, const MediaSample& sample);

  /// Finalize the segment / subsegment.
  /// @param stream_id is the zero-based stream index.
  /// @param is_subsegment indicates if it is a subsegment (fragment).
  /// @return OK on success, an error status otherwise.
  Status FinalizeSegment(size_t stream_id, const SegmentInfo& segment_info);

  // TODO(rkuroiwa): Change these Get*Range() methods to return
  // base::Optional<Range> as well.
  /// @return true if there is an initialization range, while setting @a offset
  ///         and @a size; or false if initialization range does not apply.
  virtual bool GetInitRange(size_t* offset, size_t* size) = 0;

  /// @return true if there is an index byte range, while setting @a offset
  ///         and @a size; or false if index byte range does not apply.
  virtual bool GetIndexRange(size_t* offset, size_t* size) = 0;

  // Returns an empty vector if there are no specific ranges for the segments,
  // e.g. the media is in multiple files.
  // Otherwise, a vector of ranges for the media segments are returned.
  virtual std::vector<Range> GetSegmentRanges() = 0;

  uint32_t GetReferenceTimeScale() const;

  /// @return The total length, in seconds, of segmented media files.
  double GetDuration() const;

  /// @return The sample duration in the timescale of the media.
  ///         Returns 0 if no samples are added yet.
  int64_t sample_duration() const { return sample_durations_[1]; }

 protected:
  /// Update segmentation progress using ProgressListener.
  void UpdateProgress(uint64_t progress);
  /// Set progress to 100%.
  void SetComplete();

  const MuxerOptions& options() const { return options_; }
  FileType* ftyp() { return ftyp_.get(); }
  Movie* moov() { return moov_.get(); }
  BufferWriter* fragment_buffer() { return fragment_buffer_.get(); }
  SegmentIndex* sidx() { return sidx_.get(); }
  MuxerListener* muxer_listener() { return muxer_listener_; }
  uint64_t progress_target() { return progress_target_; }
  const std::vector<KeyFrameInfo>& key_frame_infos() const {
    return key_frame_infos_;
  }

  void set_progress_target(uint64_t progress_target) {
    progress_target_ = progress_target;
  }

 private:
  virtual Status DoInitialize() = 0;
  virtual Status DoFinalize() = 0;
  virtual Status DoFinalizeSegment() = 0;

  uint32_t GetReferenceStreamId();

  void FinalizeFragmentForKeyRotation(
      size_t stream_id,
      bool fragment_encrypted,
      const EncryptionConfig& encryption_config);

  const MuxerOptions& options_;
  std::unique_ptr<FileType> ftyp_;
  std::unique_ptr<Movie> moov_;
  std::unique_ptr<MovieFragment> moof_;
  std::unique_ptr<BufferWriter> fragment_buffer_;
  std::unique_ptr<SegmentIndex> sidx_;
  std::vector<std::unique_ptr<Fragmenter>> fragmenters_;
  MuxerListener* muxer_listener_ = nullptr;
  ProgressListener* progress_listener_ = nullptr;
  uint64_t progress_target_ = 0u;
  uint64_t accumulated_progress_ = 0u;
  size_t sample_durations_[2] = {0, 0};
  uint32_t num_samples_ = 0u;
  std::vector<uint64_t> stream_durations_;
  std::vector<KeyFrameInfo> key_frame_infos_;

  DISALLOW_COPY_AND_ASSIGN(Segmenter);
};

}  // namespace mp4
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_MP4_SEGMENTER_H_
