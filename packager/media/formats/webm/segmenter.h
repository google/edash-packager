// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_WEBM_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_WEBM_SEGMENTER_H_

#include <memory>

#include "packager/base/optional.h"
#include "packager/media/base/range.h"
#include "packager/media/formats/webm/mkv_writer.h"
#include "packager/media/formats/webm/seek_head.h"
#include "packager/status.h"
#include "packager/third_party/libwebm/src/mkvmuxer.hpp"

namespace shaka {
namespace media {

struct MuxerOptions;

class AudioStreamInfo;
class MediaSample;
class MuxerListener;
class ProgressListener;
class StreamInfo;
class VideoStreamInfo;

namespace webm {

class Segmenter {
 public:
  explicit Segmenter(const MuxerOptions& options);
  virtual ~Segmenter();

  /// Initialize the segmenter.
  /// Calling other public methods of this class without this method returning
  /// Status::OK results in an undefined behavior.
  /// @param info The stream info for the stream being segmented.
  /// @param muxer_listener receives muxer events. Can be NULL.
  /// @return OK on success, an error status otherwise.
  Status Initialize(const StreamInfo& info,
                    ProgressListener* progress_listener,
                    MuxerListener* muxer_listener);

  /// Finalize the segmenter.
  /// @return OK on success, an error status otherwise.
  Status Finalize();

  /// Add sample to the indicated stream.
  /// @param sample points to the sample to be added.
  /// @return OK on success, an error status otherwise.
  Status AddSample(const MediaSample& sample);

  /// Finalize the (sub)segment.
  virtual Status FinalizeSegment(uint64_t start_timestamp,
                                 uint64_t duration_timestamp,
                                 bool is_subsegment,
                                 int64_t segment_index) = 0;

  /// @return true if there is an initialization range, while setting @a start
  ///         and @a end; or false if initialization range does not apply.
  virtual bool GetInitRangeStartAndEnd(uint64_t* start, uint64_t* end) = 0;

  /// @return true if there is an index byte range, while setting @a start
  ///         and @a end; or false if index byte range does not apply.
  virtual bool GetIndexRangeStartAndEnd(uint64_t* start, uint64_t* end) = 0;

  // Returns an empty vector if there are no specific ranges for the segments,
  // e.g. the media is in multiple files.
  // Otherwise, a vector of ranges for the media segments are returned.
  virtual std::vector<Range> GetSegmentRanges() = 0;

  /// @return The total length, in seconds, of segmented media files.
  float GetDurationInSeconds() const;

 protected:
  /// Converts the given time in ISO BMFF timestamp to WebM timecode.
  uint64_t FromBmffTimestamp(uint64_t bmff_timestamp);
  /// Converts the given time in WebM timecode to ISO BMFF timestamp.
  uint64_t FromWebMTimecode(uint64_t webm_timecode);
  /// Writes the Segment header to @a writer.
  Status WriteSegmentHeader(uint64_t file_size, MkvWriter* writer);
  /// Creates a Cluster object with the given parameters.
  Status SetCluster(uint64_t start_webm_timecode,
                    uint64_t position,
                    MkvWriter* writer);

  /// Update segmentation progress using ProgressListener.
  void UpdateProgress(uint64_t progress);
  void set_progress_target(uint64_t target) { progress_target_ = target; }

  const MuxerOptions& options() const { return options_; }
  mkvmuxer::Cluster* cluster() { return cluster_.get(); }
  mkvmuxer::Cues* cues() { return &cues_; }
  MuxerListener* muxer_listener() { return muxer_listener_; }
  SeekHead* seek_head() { return &seek_head_; }

  int track_id() const { return track_id_; }
  uint64_t segment_payload_pos() const { return segment_payload_pos_; }

  uint64_t duration() const { return duration_; }

  virtual Status DoInitialize() = 0;
  virtual Status DoFinalize() = 0;

 private:
  Status InitializeAudioTrack(const AudioStreamInfo& info,
                              mkvmuxer::AudioTrack* track);
  Status InitializeVideoTrack(const VideoStreamInfo& info,
                              mkvmuxer::VideoTrack* track);

  // Writes the previous frame to the file.
  Status WriteFrame(bool write_duration);

  // This is called when there needs to be a new (sub)segment.
  // In single-segment mode, a Cluster is a segment and there is no subsegment.
  // In multi-segment mode, a new file is a segment and the clusters in the file
  // are subsegments.
  virtual Status NewSegment(uint64_t start_timestamp, bool is_subsegment) = 0;

  // Store the previous sample so we know which one is the last frame.
  std::shared_ptr<const MediaSample> prev_sample_;
  // The reference frame timestamp; used to populate the ReferenceBlock element
  // when writing non-keyframe BlockGroups.
  uint64_t reference_frame_timestamp_ = 0;

  const MuxerOptions& options_;

  std::unique_ptr<mkvmuxer::Cluster> cluster_;
  mkvmuxer::Cues cues_;
  SeekHead seek_head_;
  mkvmuxer::SegmentInfo segment_info_;
  mkvmuxer::Tracks tracks_;

  MuxerListener* muxer_listener_ = nullptr;
  ProgressListener* progress_listener_ = nullptr;
  uint64_t progress_target_ = 0;
  uint64_t accumulated_progress_ = 0;
  uint64_t first_timestamp_ = 0;
  int64_t sample_duration_ = 0;
  // The position (in bytes) of the start of the Segment payload in the init
  // file.  This is also the size of the header before the SeekHead.
  uint64_t segment_payload_pos_ = 0;

  // Indicate whether a new segment needed to be created, which is always true
  // in the beginning.
  bool new_segment_ = true;
  // Indicate whether a new subsegment needed to be created.
  bool new_subsegment_ = false;
  int track_id_ = 0;

  // The subset of information that we need from StreamInfo
  bool is_encrypted_ = false;
  uint64_t time_scale_ = 0;
  uint64_t duration_ = 0;

  DISALLOW_COPY_AND_ASSIGN(Segmenter);
};

}  // namespace webm
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_WEBM_SEGMENTER_H_
