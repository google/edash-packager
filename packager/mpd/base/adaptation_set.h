// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
/// All the methods that are virtual are virtual for mocking.

#ifndef PACKAGER_MPD_BASE_ADAPTATION_SET_H_
#define PACKAGER_MPD_BASE_ADAPTATION_SET_H_

#include <stdint.h>

#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "packager/base/optional.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/xml/scoped_xml_ptr.h"

namespace shaka {

class MediaInfo;
class Representation;

struct ContentProtectionElement;
struct MpdOptions;

namespace xml {
class XmlNode;
}  // namespace xml

/// AdaptationSet class provides methods to add Representations and
/// <ContentProtection> elements to the AdaptationSet element.
class AdaptationSet {
 public:
  // The role for this AdaptationSet. These values are used to add a Role
  // element to the AdaptationSet with schemeIdUri=urn:mpeg:dash:role:2011.
  // See ISO/IEC 23009-1:2012 section 5.8.5.5.
  enum Role {
    kRoleUnknown,
    kRoleCaption,
    kRoleSubtitle,
    kRoleMain,
    kRoleAlternate,
    kRoleSupplementary,
    kRoleCommentary,
    kRoleDub
  };

  virtual ~AdaptationSet();

  /// Create a Representation instance using @a media_info.
  /// @param media_info is a MediaInfo object used to initialize the returned
  ///        Representation instance. It may contain only one of VideoInfo,
  ///        AudioInfo, or TextInfo, i.e. VideoInfo XOR AudioInfo XOR TextInfo.
  /// @return On success, returns a pointer to Representation. Otherwise returns
  ///         NULL. The returned pointer is owned by the AdaptationSet instance.
  virtual Representation* AddRepresentation(const MediaInfo& media_info);

  /// Copy a Representation instance from @a representation in another
  /// AdaptationSet. One use case is to duplicate Representation in different
  /// periods.
  /// @param representation is an existing Representation to be cloned from.
  /// @return On success, returns a pointer to Representation. Otherwise returns
  ///         NULL. The returned pointer is owned by the AdaptationSet instance.
  virtual Representation* CopyRepresentation(
      const Representation& representation);

  /// Add a ContenProtection element to the adaptation set.
  /// AdaptationSet does not add <ContentProtection> elements
  /// automatically to itself even if @a media_info.protected_content is
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

  /// Set the Accessibility element for this AdaptationSet.
  /// See ISO/IEC 23009-1:2012 section 5.8.4.3.
  /// @param scheme is the schemeIdUri of the accessibility element.
  /// @param value is the value of the accessibility element.
  virtual void AddAccessibility(const std::string& scheme,
                                const std::string& value);

  /// Set the Role element for this AdaptationSet.
  /// The Role element's is schemeIdUri='urn:mpeg:dash:role:2011'.
  /// See ISO/IEC 23009-1:2012 section 5.8.5.5.
  /// @param role of this AdaptationSet.
  virtual void AddRole(Role role);

  /// Makes a copy of AdaptationSet xml element with its child Representation
  /// and ContentProtection elements.
  /// @return On success returns a non-NULL scoped_xml_ptr. Otherwise returns a
  ///         NULL scoped_xml_ptr.
  xml::scoped_xml_ptr<xmlNode> GetXml();

  /// Forces the (sub)segmentAlignment field to be set to @a segment_alignment.
  /// Use this if you are certain that the (sub)segments are alinged/unaligned
  /// for the AdaptationSet.
  /// @param segment_alignment is the value used for (sub)segmentAlignment
  ///        attribute.
  virtual void ForceSetSegmentAlignment(bool segment_alignment);

  /// Adds the adaptation set this adaptation set can switch to.
  /// @param adaptation_set points to the switchable adaptation set.
  virtual void AddAdaptationSetSwitching(const AdaptationSet* adaptation_set);

  /// @return true if id is set, false otherwise.
  bool has_id() const { return static_cast<bool>(id_); }

  // Must be unique in the Period.
  uint32_t id() const { return id_.value(); }

  /// Set AdaptationSet@id.
  /// @param id is the new ID to be set.
  void set_id(uint32_t id) { id_ = id; }

  /// Notifies the AdaptationSet instance that a new (sub)segment was added to
  /// the Representation with @a representation_id.
  /// This must be called every time a (sub)segment is added to a
  /// Representation in this AdaptationSet.
  /// If a Representation is constructed using AddRepresentation() this
  /// is called automatically whenever Representation::AddNewSegment() is
  /// is called.
  /// @param representation_id is the id of the Representation with a new
  ///        segment.
  /// @param start_time is the start time of the new segment.
  /// @param duration is the duration of the new segment.
  void OnNewSegmentForRepresentation(uint32_t representation_id,
                                     uint64_t start_time,
                                     uint64_t duration);

  /// Notifies the AdaptationSet instance that the sample duration for the
  /// Representation was set.
  /// The frame duration for a video Representation might not be specified when
  /// a Representation is created (by calling AddRepresentation()).
  /// This should be used to notify this instance that the frame rate for a
  /// Represenatation has been set.
  /// This method is called automatically when
  /// Represenatation::SetSampleDuration() is called if the Represenatation
  /// instance was created using AddRepresentation().
  /// @param representation_id is the id of the Representation.
  /// @frame_duration is the duration of a frame in the Representation.
  /// @param timescale is the timescale of the Representation.
  void OnSetFrameRateForRepresentation(uint32_t representation_id,
                                       uint32_t frame_duration,
                                       uint32_t timescale);

  /// Add the adaptation set this trick play adaptation set belongs to.
  /// @param adaptation_set points to the reference (or main) adapation set.
  virtual void AddTrickPlayReference(const AdaptationSet* adaptation_set);

  // Return the list of Representations in this AdaptationSet.
  const std::list<Representation*> GetRepresentations() const;

  /// @return true if it is a video AdaptationSet.
  bool IsVideo() const;

  /// @return codec.
  const std::string& codec() const { return codec_; }

  /// Set AdaptationSet@codec.
  /// @param codec is the new codec to be set.
  void set_codec(const std::string& codec) { codec_ = codec; };

  /// Return ProtectedContent.
  const MediaInfo::ProtectedContent* protected_content() const {
    return protected_content_;
  };

  /// Set AdaptationSet@protected_content.
  /// @param media_info to extract the ProtectedContent from.
  void set_protected_content(const MediaInfo& media_info);

  /// Check if the protected content associated with this AdaptationSet  matches
  /// with the one in |media_info|.
  /// @param media_info to extract ProtectedContent from.
  /// @param content_protection_in_adaptation_set to indicate if there is
  ///        protected content in AdaptationSet.
  /// @return true if there is a match.
  bool MatchAdaptationSet(const MediaInfo& media_info,
                          bool content_protection_in_adaptation_set);

  /// Check if the adaptation sets are switchable.
  /// @param adaptation_set to compare this AdaptationSet with.
  /// @return true if AdaptationSets are switchable.
  bool SwitchableAdaptationSet(const AdaptationSet& adaptation_set);

 protected:
  /// @param language is the language of this AdaptationSet. Mainly relevant for
  ///        audio.
  /// @param mpd_options is the options for this MPD.
  /// @param mpd_type is the type of this MPD.
  /// @param representation_counter is a Counter for assigning ID numbers to
  ///        Representation. It can not be NULL.
  AdaptationSet(const std::string& language,
                const MpdOptions& mpd_options,
                uint32_t* representation_counter);

 private:
  AdaptationSet(const AdaptationSet&) = delete;
  AdaptationSet& operator=(const AdaptationSet&) = delete;

  friend class Period;
  friend class AdaptationSetTest;

  // kSegmentAlignmentUnknown means that it is uncertain if the
  // (sub)segments are aligned or not.
  // kSegmentAlignmentTrue means that it is certain that the all the (current)
  // segments added to the adaptation set are aligned.
  // kSegmentAlignmentFalse means that it is it is certain that some segments
  // are not aligned. This is useful to disable the computation for
  // segment alignment, once it is certain that some segments are not aligned.
  enum SegmentAligmentStatus {
    kSegmentAlignmentUnknown,
    kSegmentAlignmentTrue,
    kSegmentAlignmentFalse
  };

  // This maps Representations (IDs) to a list of start times of the segments.
  // e.g.
  // If Representation 1 has start time 0, 100, 200 and Representation 2 has
  // start times 0, 200, 400, then the map contains:
  // 1 -> [0, 100, 200]
  // 2 -> [0, 200, 400]
  typedef std::map<uint32_t, std::list<uint64_t>> RepresentationTimeline;

  // Update AdaptationSet attributes for new MediaInfo.
  void UpdateFromMediaInfo(const MediaInfo& media_info);

  /// Called from OnNewSegmentForRepresentation(). Checks whether the segments
  /// are aligned. Sets segments_aligned_.
  /// This is only for dynamic MPD. For static MPD,
  /// CheckStaticSegmentAlignment() should be used.
  /// @param representation_id is the id of the Representation with a new
  ///        segment.
  /// @param start_time is the start time of the new segment.
  /// @param duration is the duration of the new segment.
  void CheckDynamicSegmentAlignment(uint32_t representation_id,
                                    uint64_t start_time,
                                    uint64_t duration);

  // Checks representation_segment_start_times_ and sets segments_aligned_.
  // Use this for static MPD, do not use for dynamic MPD.
  void CheckStaticSegmentAlignment();

  // Records the framerate of a Representation.
  void RecordFrameRate(uint32_t frame_duration, uint32_t timescale);

  std::list<ContentProtectionElement> content_protection_elements_;
  // representation_id => Representation map. It also keeps the representations_
  // sorted by default.
  std::map<uint32_t, std::unique_ptr<Representation>> representation_map_;

  uint32_t* const representation_counter_;

  base::Optional<uint32_t> id_;
  const std::string language_;
  const MpdOptions& mpd_options_;

  // An array of adaptation sets this adaptation set can switch to.
  std::vector<const AdaptationSet*> switchable_adaptation_sets_;

  // Video widths and heights of Representations. Note that this is a set; if
  // there is only 1 resolution, then @width & @height should be set, otherwise
  // @maxWidth & @maxHeight should be set for DASH IOP.
  std::set<uint32_t> video_widths_;
  std::set<uint32_t> video_heights_;

  // Video representations' frame rates.
  // The frame rate notation for MPD is <integer>/<integer> (where the
  // denominator is optional). This means the frame rate could be non-whole
  // rational value, therefore the key is of type double.
  // Value is <integer>/<integer> in string form.
  // So, key == CalculatedValue(value)
  std::map<double, std::string> video_frame_rates_;

  // contentType attribute of AdaptationSet.
  // Determined by examining the MediaInfo passed to AddRepresentation().
  std::string content_type_;

  // Codec of AdaptationSet.
  std::string codec_;

  // This does not have to be a set, it could be a list or vector because all we
  // really care is whether there is more than one entry.
  // Contains one entry if all the Representations have the same picture aspect
  // ratio (@par attribute for AdaptationSet).
  // There will be more than one entry if there are multiple picture aspect
  // ratios.
  // The @par attribute should only be set if there is exactly one entry
  // in this set.
  std::set<std::string> picture_aspect_ratio_;

  // accessibilities of this AdaptationSet.
  struct Accessibility {
    std::string scheme;
    std::string value;
  };
  std::vector<Accessibility> accessibilities_;

  // The roles of this AdaptationSet.
  std::set<Role> roles_;

  // True iff all the segments are aligned.
  SegmentAligmentStatus segments_aligned_;
  bool force_set_segment_alignment_;

  // Keeps track of segment start times of Representations.
  // For static MPD, this will not be cleared, all the segment start times are
  // stored in this. This should not out-of-memory for a reasonable length
  // video and reasonable subsegment length.
  // For dynamic MPD, the entries are deleted (see
  // CheckDynamicSegmentAlignment() implementation comment) because storing the
  // entire timeline is not reasonable and may cause an out-of-memory problem.
  RepresentationTimeline representation_segment_start_times_;

  // Record the original AdaptationSets the trick play stream belongs to. There
  // can be more than one reference AdaptationSets as multiple streams e.g. SD
  // and HD videos in different AdaptationSets can share the same trick play
  // stream.
  std::vector<const AdaptationSet*> trick_play_references_;

  // ProtectedContent of this AdaptationSet.
  MediaInfo::ProtectedContent* protected_content_;
};

}  // namespace shaka

#endif  // PACKAGER_MPD_BASE_ADAPTATION_SET_H_
