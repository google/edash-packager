// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Event handler for events fired by Muxer.

#ifndef PACKAGER_MEDIA_EVENT_MUXER_LISTENER_H_
#define PACKAGER_MEDIA_EVENT_MUXER_LISTENER_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "packager/base/optional.h"
#include "packager/media/base/fourccs.h"
#include "packager/media/base/range.h"

namespace shaka {
namespace media {

struct MuxerOptions;
struct ProtectionSystemSpecificInfo;
class StreamInfo;

/// MuxerListener is an event handler that can be registered to a muxer.
/// A MuxerListener cannot be shared amongst muxer instances, in other words,
/// every muxer instance either owns a unique MuxerListener instance.
/// This also assumes that there is one media stream per muxer.
class MuxerListener {
 public:
  enum ContainerType {
    kContainerUnknown = 0,
    kContainerMp4,
    kContainerMpeg2ts,
    kContainerWebM,
    kContainerText,
    kContainerPackedAudio,
  };

  /// Structure for specifying ranges within a media file. This is mainly for
  /// VOD content where OnMediaEnd() is actually used for finalization e.g.
  /// writing out manifests.
  struct MediaRanges {
    /// Range of the initialization section of a segment.
    base::Optional<Range> init_range;
    /// Range of the index section of a segment.
    base::Optional<Range> index_range;
    /// Ranges of the subsegments (e.g. fragments).
    /// The vector is empty if ranges are not specified. For example it
    /// may not be a single file.
    std::vector<Range> subsegment_ranges;
  };

  virtual ~MuxerListener() = default;

  /// Called when the media's encryption information is ready.
  /// OnEncryptionInfoReady with @a initial_encryption_info being true should be
  /// called before OnMediaStart(), if the media is encrypted. All the
  /// parameters may be empty just to notify that the media is encrypted. For
  /// ISO BMFF (MP4) media: If @a is_initial_encryption_info is true then @a
  /// key_id is the default_KID in 'tenc' box. If @a is_initial_encryption_info
  /// is false then @a key_id is the new key ID for the for the next crypto
  /// period.
  /// @param is_initial_encryption_info is true if this is the first encryption
  ///        info for the media. In general, this flag should always be true for
  ///        non-key-rotated media and should be called only once.
  /// @param protection_scheme specifies the protection scheme: 'cenc', 'cens',
  ///        'cbc1', 'cbcs'.
  /// @param key_id is the key ID for the media.  The format should be a vector
  ///        of uint8_t, i.e. not (necessarily) human readable hex string.
  /// @param iv is the initialization vector. For most cases this should be 16
  ///        bytes, but whether the input is accepted is up to the
  ///        implementation.
  virtual void OnEncryptionInfoReady(
      bool is_initial_encryption_info,
      FourCC protection_scheme,
      const std::vector<uint8_t>& key_id,
      const std::vector<uint8_t>& iv,
      const std::vector<ProtectionSystemSpecificInfo>& key_system_info) = 0;

  /// Called when the muxer starts encrypting the segments.
  /// Further segments notified via OnNewSegment() are encrypted.
  /// This may be called more than once e.g. per segment, but the semantics does
  /// not change.
  virtual void OnEncryptionStart() = 0;

  /// Called when muxing starts.
  /// For MPEG DASH Live profile, the initialization segment information is
  /// available from StreamInfo.
  /// @param muxer_options is the options for Muxer.
  /// @param stream_info is the information of this media.
  /// @param time_scale is a reference time scale that overrides the time scale
  ///         specified in @a stream_info.
  /// @param container_type is the container of this media.
  virtual void OnMediaStart(const MuxerOptions& muxer_options,
                            const StreamInfo& stream_info,
                            uint32_t time_scale,
                            ContainerType container_type) = 0;

  /// Called when the average sample duration of the media is determined.
  /// @param sample_duration in timescale of the media.
  virtual void OnSampleDurationReady(uint32_t sample_duration) = 0;

  /// Called when all files are written out and the muxer object does not output
  /// any more files.
  /// Note: This event might not be very interesting to MPEG DASH Live profile.
  /// @param media_ranges is the ranges of the media file. It should have ranges
  ///        for the entire file, using which the file size can be calculated.
  /// @param duration_seconds is the length of the media in seconds.
  virtual void OnMediaEnd(const MediaRanges& media_ranges,
                          float duration_seconds) = 0;

  /// Called when a segment has been muxed and the file has been written.
  /// Note: For some implementations, this is used to signal new subsegments.
  /// For example, for generating video on demand (VOD) MPD manifest, this is
  /// called to signal subsegments.
  /// @param segment_name is the name of the new segment. Note that some
  ///        implementations may not require this, e.g. if this is a subsegment.
  /// @param start_time is the start time of the segment, relative to the
  ///        timescale specified by MediaInfo passed to OnMediaStart().
  /// @param duration is the duration of the segment, relative to the timescale
  ///        specified by MediaInfo passed to OnMediaStart().
  /// @param segment_file_size is the segment size in bytes.
  /// @param segment_number is the segment number.
  virtual void OnNewSegment(const std::string& segment_name,
                            int64_t start_time,
                            int64_t duration,
                            uint64_t segment_file_size,
                            int64_t segment_number) = 0;

  /// Called when there is a new key frame. For Video only. Note that it should
  /// be called before OnNewSegment is called on the containing segment.
  /// @param timestamp is in terms of the timescale of the media.
  /// @param start_byte_offset is the offset of where the key frame starts.
  /// @param size is size in bytes.
  virtual void OnKeyFrame(int64_t timestamp,
                          uint64_t start_byte_offset,
                          uint64_t size) = 0;

  /// Called when there is a new Ad Cue, which should align with (sub)segments.
  /// @param timestamp indicate the cue timestamp.
  /// @param cue_data is the data of the cue.
  virtual void OnCueEvent(int64_t timestamp, const std::string& cue_data) = 0;

 protected:
  MuxerListener() = default;
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_EVENT_MUXER_LISTENER_H_
