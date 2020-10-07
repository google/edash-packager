// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
/// All the methods that are virtual are virtual for mocking.

#ifndef PACKAGER_MPD_BASE_REPRESENTATION_H_
#define PACKAGER_MPD_BASE_REPRESENTATION_H_

#include "packager/mpd/base/bandwidth_estimator.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/segment_info.h"
#include "packager/mpd/base/xml/scoped_xml_ptr.h"

#include <stdint.h>

#include <list>
#include <memory>

namespace shaka {

struct ContentProtectionElement;
struct MpdOptions;

namespace xml {
class XmlNode;
class RepresentationXmlNode;
}  // namespace xml

class RepresentationStateChangeListener {
 public:
  RepresentationStateChangeListener() {}
  virtual ~RepresentationStateChangeListener() {}

  /// Notifies the instance that a new (sub)segment was added to
  /// the Representation.
  /// @param start_time is the start time of the new segment.
  /// @param duration is the duration of the new segment.
  virtual void OnNewSegmentForRepresentation(int64_t start_time,
                                             int64_t duration) = 0;

  /// Notifies the instance that the frame rate was set for the
  /// Representation.
  /// @param frame_duration is the duration of a frame.
  /// @param timescale is the timescale of the Representation.
  virtual void OnSetFrameRateForRepresentation(uint32_t frame_duration,
                                               uint32_t timescale) = 0;
};

/// Representation class contains references to a single media stream, as
/// well as optional ContentProtection elements for that stream.
class Representation {
 public:
  enum SuppressFlag {
    kSuppressWidth = 1,
    kSuppressHeight = 2,
    kSuppressFrameRate = 4,
  };

  virtual ~Representation();

  /// Tries to initialize the instance. If this does not succeed, the instance
  /// should not be used.
  /// @return true on success, false otherwise.
  bool Init();

  /// Add a ContenProtection element to the representation.
  /// Representation does not add <ContentProtection> elements
  /// automatically to itself even if @a media_info passed to
  /// AdaptationSet::AddRepresentation() has @a media_info.protected_content
  /// populated. This is because some MPDs should have the elements at
  /// AdaptationSet level and some at Representation level.
  /// @param element contains the ContentProtection element contents.
  ///        If @a element has {value, schemeIdUri} set and has
  ///        {“value”, “schemeIdUri”} as key for @a additional_attributes,
  ///        then the former is used.
  virtual void AddContentProtectionElement(
      const ContentProtectionElement& element);

  /// Update the 'cenc:pssh' element for @a drm_uuid ContentProtection element.
  /// If the element does not exist, this will add one.
  /// @param drm_uuid is the UUID of the DRM for encryption.
  /// @param pssh is the content of <cenc:pssh> element.
  ///        Note that DASH IF IOP mentions that this should be base64 encoded
  ///        string of the whole pssh box.
  /// @attention This might get removed once DASH IF IOP specification makes a
  ///            a clear guideline on how to handle key rotation. Also to get
  ///            this working with shaka-player, this method *DOES NOT* update
  ///            the PSSH element. Instead, it removes the element regardless of
  ///            the content of @a pssh.
  virtual void UpdateContentProtectionPssh(const std::string& drm_uuid,
                                           const std::string& pssh);

  /// Add a media (sub)segment to the representation.
  /// AdaptationSet@{subsegmentAlignment,segmentAlignment} cannot be set
  /// if this is not called for all Representations.
  /// @param start_time is the start time for the (sub)segment, in units of the
  ///        stream's time scale.
  /// @param duration is the duration of the segment, in units of the stream's
  ///        time scale.
  /// @param size of the segment in bytes.
  /// @param segment_index is the current segment index.
  virtual void AddNewSegment(int64_t start_time,
                             int64_t duration,
                             uint64_t size,
                             int64_t segment_index);

  /// Set the sample duration of this Representation.
  /// Sample duration is not available right away especially for live. This
  /// allows setting the sample duration after the Representation has been
  /// initialized.
  /// @param sample_duration is the duration of a sample.
  virtual void SetSampleDuration(uint32_t sample_duration);

  /// @return MediaInfo for the Representation.
  virtual const MediaInfo& GetMediaInfo() const;

  /// @return Copy of <Representation>.
  xml::scoped_xml_ptr<xmlNode> GetXml();

  /// By calling this methods, the next time GetXml() is
  /// called, the corresponding attributes will not be set.
  /// For example, if SuppressOnce(kSuppressWidth) is called, then GetXml() will
  /// return a <Representation> element without a @width attribute.
  /// Note that it only applies to the next call to GetXml(), calling GetXml()
  /// again without calling this methods will return a <Representation> element
  /// with the attribute.
  /// This may be called multiple times to set different (or the same) flags.
  void SuppressOnce(SuppressFlag flag);

  /// Set @presentationTimeOffset in SegmentBase / SegmentTemplate.
  void SetPresentationTimeOffset(double presentation_time_offset);

  /// Gets the start and end timestamps in seconds.
  /// @param start_timestamp_seconds contains the returned start timestamp in
  ///        seconds on success. It can be nullptr, which means that start
  ///        timestamp does not need to be returned.
  /// @param end_timestamp_seconds contains the returned end timestamp in
  ///        seconds on success. It can be nullptr, which means that end
  ///        timestamp does not need to be returned.
  /// @return true if successful, false otherwise.
  bool GetStartAndEndTimestamps(double* start_timestamp_seconds,
                                double* end_timestamp_seconds) const;

  /// @return ID number for <Representation>.
  uint32_t id() const { return id_; }

  void set_media_info(const MediaInfo& media_info) { media_info_ = media_info; }

 protected:
  /// @param media_info is a MediaInfo containing information on the media.
  ///        @a media_info.bandwidth is required for 'static' profile. If @a
  ///        media_info.bandwidth is not present in 'dynamic' profile, this
  ///        tries to estimate it using the info passed to AddNewSegment().
  /// @param mpd_options is options for the entire MPD.
  /// @param representation_id is the numeric ID for the <Representation>.
  /// @param state_change_listener is an event handler for state changes to
  ///        the representation. If null, no event handler registered.
  Representation(
      const MediaInfo& media_info,
      const MpdOptions& mpd_options,
      uint32_t representation_id,
      std::unique_ptr<RepresentationStateChangeListener> state_change_listener);

  /// @param representation points to the original Representation to be cloned.
  /// @param state_change_listener is an event handler for state changes to
  ///        the representation. If null, no event handler registered.
  Representation(
      const Representation& representation,
      std::unique_ptr<RepresentationStateChangeListener> state_change_listener);

 private:
  Representation(const Representation&) = delete;
  Representation& operator=(const Representation&) = delete;

  friend class AdaptationSet;
  friend class RepresentationTest;

  // Returns true if |media_info_| has required fields to generate a valid
  // Representation. Otherwise returns false.
  bool HasRequiredMediaInfoFields() const;

  // Add a SegmentInfo. This function may insert an adjusted SegmentInfo if
  // |allow_approximate_segment_timeline_| is set.
  void AddSegmentInfo(int64_t start_time,
                      int64_t duration,
                      int64_t segment_index);

  // Check if two timestamps are approximately equal if
  // |allow_approximate_segment_timeline_| is set; Otherwise check whether the
  // two times match.
  bool ApproximiatelyEqual(int64_t time1, int64_t time2) const;

  // Return adjusted duration if |allow_aproximate_segment_timeline_or_duration|
  // is set; otherwise duration is returned without adjustment.
  int64_t AdjustDuration(int64_t duration) const;

  // Remove elements from |segment_infos_| for dynamic live profile. Increments
  // |start_number_| by the number of segments removed.
  void SlideWindow();

  // Remove the first segment in |segment_info|.
  void RemoveOldSegment(SegmentInfo* segment_info);

  // Note: Because 'mimeType' is a required field for a valid MPD, these return
  // strings.
  std::string GetVideoMimeType() const;
  std::string GetAudioMimeType() const;
  std::string GetTextMimeType() const;

  // Get Representation as string. For debugging.
  std::string RepresentationAsString() const;

  // Init() checks that only one of VideoInfo, AudioInfo, or TextInfo is set. So
  // any logic using this can assume only one set.
  MediaInfo media_info_;
  std::list<ContentProtectionElement> content_protection_elements_;

  int64_t current_buffer_depth_ = 0;
  // TODO(kqyang): Address sliding window issue with multiple periods.
  std::list<SegmentInfo> segment_infos_;
  // A list to hold the file names of the segments to be removed temporarily.
  // Once a file is actually removed, it is removed from the list.
  std::list<std::string> segments_to_be_removed_;

  const uint32_t id_;
  std::string mime_type_;
  std::string codecs_;
  BandwidthEstimator bandwidth_estimator_;
  const MpdOptions& mpd_options_;

  bool stream_just_started_ = false;

  // If this is not null, then Representation is responsible for calling the
  // right methods at right timings.
  std::unique_ptr<RepresentationStateChangeListener> state_change_listener_;

  // Bit vector for tracking witch attributes should not be output.
  int output_suppression_flags_ = 0;

  // When set to true, allows segments to have slightly different durations (up
  // to one sample).
  const bool allow_approximate_segment_timeline_ = false;
  // Segments with duration difference less than one frame duration are
  // considered to have the same duration.
  uint32_t frame_duration_ = 0;
};

}  // namespace shaka

#endif  // PACKAGER_MPD_BASE_REPRESENTATION_H_
