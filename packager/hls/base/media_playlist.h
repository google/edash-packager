// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_HLS_BASE_MEDIA_PLAYLIST_H_
#define PACKAGER_HLS_BASE_MEDIA_PLAYLIST_H_

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "packager/base/macros.h"
#include "packager/base/time/time.h"
#include "packager/hls/public/hls_params.h"
#include "packager/mpd/base/bandwidth_estimator.h"
#include "packager/mpd/base/media_info.pb.h"

namespace shaka {

class File;

namespace hls {

class HlsEntry {
 public:
  enum class EntryType {
    kExtInf,
    kExtKey,
    kExtDiscontinuity,
    kExtPlacementOpportunity,
  };
  virtual ~HlsEntry();

  EntryType type() const { return type_; }
  virtual std::string ToString() = 0;

 protected:
  explicit HlsEntry(EntryType type);

 private:
  EntryType type_;
};

/// Methods are virtual for mocking.
class MediaPlaylist {
 public:
  enum class MediaPlaylistStreamType {
    kUnknown,
    kAudio,
    kVideo,
    kVideoIFramesOnly,
    kSubtitle,
  };
  enum class EncryptionMethod {
    kNone,           // No encryption, i.e. clear.
    kAes128,         // Completely encrypted using AES-CBC.
    kSampleAes,      // Encrypted using SAMPLE-AES method.
    kSampleAesCenc,  // 'cenc' encrypted content.
  };

  /// @param hls_params contains HLS parameters.
  /// @param file_name is the file name of this media playlist, relative to
  ///        master playlist output path.
  /// @param name is the name of this playlist. In other words this is the
  ///        value of the NAME attribute for EXT-X-MEDIA. This is not
  ///        necessarily the same as @a file_name.
  /// @param group_id is the group ID for this playlist. This is the value of
  ///        GROUP-ID attribute for EXT-X-MEDIA.
  MediaPlaylist(const HlsParams& hls_params,
                const std::string& file_name,
                const std::string& name,
                const std::string& group_id);
  virtual ~MediaPlaylist();

  const std::string& file_name() const { return file_name_; }
  const std::string& name() const { return name_; }
  const std::string& group_id() const { return group_id_; }
  MediaPlaylistStreamType stream_type() const { return stream_type_; }
  const std::string& codec() const { return codec_; }

  /// For testing only.
  void SetStreamTypeForTesting(MediaPlaylistStreamType stream_type);

  /// For testing only.
  void SetCodecForTesting(const std::string& codec);

  /// For testing only.
  void SetLanguageForTesting(const std::string& language);

  /// For testing only.
  void SetCharacteristicsForTesting(
      const std::vector<std::string>& characteristics);

  /// This must succeed before calling any other public methods.
  /// @param media_info is the info of the segments that are going to be added
  ///        to this playlist.
  /// @return true on success, false otherwise.
  virtual bool SetMediaInfo(const MediaInfo& media_info);

  /// Set the sample duration. Sample duration is used to generate frame rate.
  /// Sample duration is not available right away especially. This allows
  /// setting the sample duration after the Media Playlist has been initialized.
  /// @param sample_duration is the duration of a sample.
  virtual void SetSampleDuration(uint32_t sample_duration);

  /// Segments must be added in order.
  /// @param file_name is the file name of the segment.
  /// @param start_time is in terms of the timescale of the media.
  /// @param duration is in terms of the timescale of the media.
  /// @param start_byte_offset is the offset of where the subsegment starts.
  ///        This must be 0 if the whole segment is a subsegment.
  /// @param size is size in bytes.
  /// @param reference_time reference time for wall clock time associated
  ///        to segments generation start.
  virtual void AddSegment(const std::string& file_name,
                          int64_t start_time,
                          int64_t duration,
                          uint64_t start_byte_offset,
                          uint64_t size,
                          base::Time reference_time);

  /// Keyframes must be added in order. It is also called before the containing
  /// segment being called.
  /// @param timestamp is the timestamp of the key frame in timescale of the
  ///        media.
  /// @param start_byte_offset is the offset of where the key frame starts.
  /// @param size is size in bytes.
  virtual void AddKeyFrame(int64_t timestamp,
                           uint64_t start_byte_offset,
                           uint64_t size);

  /// All segments added after calling this method must be decryptable with
  /// the key that can be fetched from |url|, until calling this again.
  /// @param method is the encryption method.
  /// @param url specifies where the key is i.e. the value of the URI attribute.
  /// #param key_id is the default key ID for the encrypted segements.
  /// @param iv is the initialization vector in human readable format, i.e. the
  ///        value for IV attribute. This may be empty.
  /// @param key_format is the key format, i.e. the KEYFORMAT value. This may be
  ///        empty.
  /// @param key_format_versions is the KEYFORMATVERIONS value. This may be
  ///        empty.
  virtual void AddEncryptionInfo(EncryptionMethod method,
                                 const std::string& url,
                                 const std::string& key_id,
                                 const std::string& iv,
                                 const std::string& key_format,
                                 const std::string& key_format_versions);

  /// Add #EXT-X-PLACEMENT-OPPORTUNITY for mid-roll ads. See
  /// https://support.google.com/dfp_premium/answer/7295798?hl=en.
  virtual void AddPlacementOpportunity();

  /// Write the playlist to |file_path|.
  /// This does not close the file.
  /// If target duration is not set explicitly, this will try to find the target
  /// duration. Note that target duration cannot be changed. So calling this
  /// without explicitly setting the target duration and before adding any
  /// segments will end up setting the target duration to 0 and will always
  /// generate an invalid playlist.
  /// @param file_path is the output file path accepted by the File
  ///        implementation.
  /// @return true on success, false otherwise.
  virtual bool WriteToFile(const std::string& file_path);

  /// If bitrate is specified in MediaInfo then it will use that value.
  /// Otherwise, returns the max bitrate.
  /// @return the max bitrate (in bits per second) of this MediaPlaylist.
  virtual uint64_t MaxBitrate() const;

  /// Unlike @a MaxBitrate, AvgBitrate is always computed from the segment size
  /// and duration.
  /// @return The average bitrate (in bits per second) of this MediaPlaylist.
  virtual uint64_t AvgBitrate() const;

  /// @return the longest segment’s duration. This will return 0 if no
  ///         segments have been added.
  virtual double GetLongestSegmentDuration() const;

  /// @return the start time of the last added segment
  virtual uint64_t LastSegmentStartTime() const;

  /// Set the target duration of this MediaPlaylist.
  /// In other words this is the value for EXT-X-TARGETDURATION.
  /// If this is not called before calling Write(), it will estimate the best
  /// target duration.
  /// The spec does not allow changing EXT-X-TARGETDURATION. However, this class
  /// has no control over the input source.
  /// @param target_duration is the target duration for this playlist.
  virtual void SetTargetDuration(uint32_t target_duration);

  /// @return number of channels for audio. 0 is returned for video.
  virtual int GetNumChannels() const;

  /// @return Dolby Digital Plus JOC decoding complexity, ETSI TS 103 420 v1.2.1
  ///         Backwards-compatible object audio carriage using Enhanced AC-3
  ///         Standard C.3.2.3.
  virtual int GetEC3JocComplexity() const;

  /// @return true if it's an AC-4 IMS stream, based on Dolby AC-4 in MPEG-DASH
  ///         for Online Delivery Specification 2.5.3.
  ///         https://developer.dolby.com/tools-media/online-delivery-kits/dolby-ac-4/
  virtual bool GetAC4ImsFlag() const;

  /// @return true if it's an AC-4 CBI stream, based on ETSI TS 103 190-2
  ///         Digital Audio Compression (AC-4) Standard; Part 2: Immersive and
  ///         personalized audio 4.3.
  virtual bool GetAC4CbiFlag() const;

  /// @return true if |width| and |height| have been set with a valid
  ///         resolution values.
  virtual bool GetDisplayResolution(uint32_t* width, uint32_t* height) const;

  /// @return The video range of the stream.
  virtual std::string GetVideoRange() const;

  /// @return the frame rate.
  virtual double GetFrameRate() const;

  /// @return the language of the media, as an ISO language tag in its shortest
  ///         form.  May be an empty string for video.
  const std::string& language() const { return language_; }

  const std::vector<std::string>& characteristics() const {
    return characteristics_;
  }

  bool is_dvs() const {
    // HLS Authoring Specification for Apple Devices
    // https://developer.apple.com/documentation/http_live_streaming/hls_authoring_specification_for_apple_devices#overview
    // Section 2.12.
    const char DVS_CHARACTERISTICS[] = "public.accessibility.describes-video";
    return characteristics_.size() == 1 &&
           characteristics_[0] == DVS_CHARACTERISTICS;
  }

 private:
  // Add a SegmentInfoEntry (#EXTINF).
  void AddSegmentInfoEntry(const std::string& segment_file_name,
                           int64_t start_time,
                           int64_t duration,
                           uint64_t start_byte_offset,
                           uint64_t size,
                           const base::Time reference_time);
  // Adjust the duration of the last SegmentInfoEntry to end on
  // |next_timestamp|.
  void AdjustLastSegmentInfoEntryDuration(int64_t next_timestamp);
  // Remove elements from |entries_| for live profile. Increments
  // |sequence_number_| by the number of segments removed.
  void SlideWindow();
  // Remove the segment specified by |start_time|. The actual deletion can
  // happen at a later time depending on the value of
  // |preserved_segment_outside_live_window| in |hls_params_|.
  void RemoveOldSegment(int64_t start_time);

  const HlsParams& hls_params_;
  // Mainly for MasterPlaylist to use these values.
  const std::string file_name_;
  const std::string name_;
  const std::string group_id_;
  MediaInfo media_info_;
  MediaPlaylistStreamType stream_type_ = MediaPlaylistStreamType::kUnknown;
  // Whether to use byte range for SegmentInfoEntry.
  bool use_byte_range_ = false;
  std::string codec_;
  std::string language_;
  std::vector<std::string> characteristics_;
  uint32_t media_sequence_number_ = 0;
  bool inserted_discontinuity_tag_ = false;
  int discontinuity_sequence_number_ = 0;

  double longest_segment_duration_seconds_ = 0.0;
  uint32_t time_scale_ = 0;

  BandwidthEstimator bandwidth_estimator_;

  // Cache the previous calls AddSegment() end offset. This is used to construct
  // SegmentInfoEntry.
  uint64_t previous_segment_end_offset_ = 0;

  // See SetTargetDuration() comments.
  bool target_duration_set_ = false;
  uint32_t target_duration_ = 0;

  // TODO(kqyang): This could be managed better by a separate class, than having
  // all them managed in MediaPlaylist.
  std::list<std::unique_ptr<HlsEntry>> entries_;
  double current_buffer_depth_ = 0;
  // A list to hold the file names of the segments to be removed temporarily.
  // Once a file is actually removed, it is removed from the list.
  std::list<std::string> segments_to_be_removed_;

  // Store last segment start_time to be able to detect discontinuities
  int64_t last_segment_start_time_ = 0;

 // Used by kVideoIFrameOnly playlists to track the i-frames (key frames).
  struct KeyFrameInfo {
    int64_t timestamp;
    uint64_t start_byte_offset;
    uint64_t size;
    std::string segment_file_name;
  };
  std::list<KeyFrameInfo> key_frames_;

  DISALLOW_COPY_AND_ASSIGN(MediaPlaylist);
};

}  // namespace hls
}  // namespace shaka

#endif  // PACKAGER_HLS_BASE_MEDIA_PLAYLIST_H_
