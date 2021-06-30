// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/mpd/base/adaptation_set.h"

#include <cmath>

#include "packager/base/logging.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/mpd_options.h"
#include "packager/mpd/base/mpd_utils.h"
#include "packager/mpd/base/representation.h"
#include "packager/mpd/base/xml/xml_node.h"

namespace shaka {
namespace {

AdaptationSet::Role MediaInfoTextTypeToRole(
    MediaInfo::TextInfo::TextType type) {
  switch (type) {
    case MediaInfo::TextInfo::UNKNOWN:
      LOG(WARNING) << "Unknown text type, assuming subtitle.";
      return AdaptationSet::kRoleSubtitle;
    case MediaInfo::TextInfo::CAPTION:
      return AdaptationSet::kRoleCaption;
    case MediaInfo::TextInfo::SUBTITLE:
      return AdaptationSet::kRoleSubtitle;
    default:
      NOTREACHED() << "Unknown MediaInfo TextType: " << type
                   << " assuming subtitle.";
      return AdaptationSet::kRoleSubtitle;
  }
}

std::string RoleToText(AdaptationSet::Role role) {
  // Using switch so that the compiler can detect whether there is a case that's
  // not being handled.
  switch (role) {
    case AdaptationSet::kRoleCaption:
      return "caption";
    case AdaptationSet::kRoleSubtitle:
      return "subtitle";
    case AdaptationSet::kRoleMain:
      return "main";
    case AdaptationSet::kRoleAlternate:
      return "alternate";
    case AdaptationSet::kRoleSupplementary:
      return "supplementary";
    case AdaptationSet::kRoleCommentary:
      return "commentary";
    case AdaptationSet::kRoleDub:
      return "dub";
    default:
      return "unknown";
  }
}

// Returns the picture aspect ratio string e.g. "16:9", "4:3".
// "Reducing the quotient to minimal form" does not work well in practice as
// there may be some rounding performed in the input, e.g. the resolution of
// 480p is 854:480 for 16:9 aspect ratio, can only be reduced to 427:240.
// The algorithm finds out the pair of integers, num and den, where num / den is
// the closest ratio to scaled_width / scaled_height, by looping den through
// common values.
std::string GetPictureAspectRatio(uint32_t width,
                                  uint32_t height,
                                  uint32_t pixel_width,
                                  uint32_t pixel_height) {
  const uint32_t scaled_width = pixel_width * width;
  const uint32_t scaled_height = pixel_height * height;
  const double par = static_cast<double>(scaled_width) / scaled_height;

  // Typical aspect ratios have par_y less than or equal to 19:
  // https://en.wikipedia.org/wiki/List_of_common_resolutions
  const uint32_t kLargestPossibleParY = 19;

  uint32_t par_num = 0;
  uint32_t par_den = 0;
  double min_error = 1.0;
  for (uint32_t den = 1; den <= kLargestPossibleParY; ++den) {
    uint32_t num = par * den + 0.5;
    double error = fabs(par - static_cast<double>(num) / den);
    if (error < min_error) {
      min_error = error;
      par_num = num;
      par_den = den;
      if (error == 0)
        break;
    }
  }
  VLOG(2) << "width*pix_width : height*pixel_height (" << scaled_width << ":"
          << scaled_height << ") reduced to " << par_num << ":" << par_den
          << " with error " << min_error << ".";

  return base::IntToString(par_num) + ":" + base::IntToString(par_den);
}

// Adds an entry to picture_aspect_ratio if the size of picture_aspect_ratio is
// less than 2 and video_info has both pixel width and pixel height.
void AddPictureAspectRatio(const MediaInfo::VideoInfo& video_info,
                           std::set<std::string>* picture_aspect_ratio) {
  // If there are more than one entries in picture_aspect_ratio, the @par
  // attribute cannot be set, so skip.
  if (picture_aspect_ratio->size() > 1)
    return;

  if (video_info.width() == 0 || video_info.height() == 0 ||
      video_info.pixel_width() == 0 || video_info.pixel_height() == 0) {
    // If there is even one Representation without a @sar attribute, @par cannot
    // be calculated.
    // Just populate the set with at least 2 bogus strings so that further call
    // to this function will bail out immediately.
    picture_aspect_ratio->insert("bogus");
    picture_aspect_ratio->insert("entries");
    return;
  }

  const std::string par = GetPictureAspectRatio(
      video_info.width(), video_info.height(), video_info.pixel_width(),
      video_info.pixel_height());
  DVLOG(1) << "Setting par as: " << par
           << " for video with width: " << video_info.width()
           << " height: " << video_info.height()
           << " pixel_width: " << video_info.pixel_width() << " pixel_height; "
           << video_info.pixel_height();
  picture_aspect_ratio->insert(par);
}

class RepresentationStateChangeListenerImpl
    : public RepresentationStateChangeListener {
 public:
  // |adaptation_set| is not owned by this class.
  RepresentationStateChangeListenerImpl(uint32_t representation_id,
                                        AdaptationSet* adaptation_set)
      : representation_id_(representation_id), adaptation_set_(adaptation_set) {
    DCHECK(adaptation_set_);
  }
  ~RepresentationStateChangeListenerImpl() override {}

  // RepresentationStateChangeListener implementation.
  void OnNewSegmentForRepresentation(int64_t start_time,
                                     int64_t duration) override {
    adaptation_set_->OnNewSegmentForRepresentation(representation_id_,
                                                   start_time, duration);
  }

  void OnSetFrameRateForRepresentation(uint32_t frame_duration,
                                       uint32_t timescale) override {
    adaptation_set_->OnSetFrameRateForRepresentation(representation_id_,
                                                     frame_duration, timescale);
  }

 private:
  const uint32_t representation_id_;
  AdaptationSet* const adaptation_set_;

  DISALLOW_COPY_AND_ASSIGN(RepresentationStateChangeListenerImpl);
};

}  // namespace

AdaptationSet::AdaptationSet(const std::string& language,
                             const MpdOptions& mpd_options,
                             uint32_t* counter)
    : representation_counter_(counter),
      language_(language),
      mpd_options_(mpd_options),
      segments_aligned_(kSegmentAlignmentUnknown),
      force_set_segment_alignment_(false) {
  DCHECK(counter);
}

AdaptationSet::~AdaptationSet() {}

Representation* AdaptationSet::AddRepresentation(const MediaInfo& media_info) {
  const uint32_t representation_id = (*representation_counter_)++;
  // Note that AdaptationSet outlive Representation, so this object
  // will die before AdaptationSet.
  std::unique_ptr<RepresentationStateChangeListener> listener(
      new RepresentationStateChangeListenerImpl(representation_id, this));
  std::unique_ptr<Representation> new_representation(new Representation(
      media_info, mpd_options_, representation_id, std::move(listener)));

  if (!new_representation->Init()) {
    LOG(ERROR) << "Failed to initialize Representation.";
    return NULL;
  }
  UpdateFromMediaInfo(media_info);
  Representation* representation_ptr = new_representation.get();
  representation_map_[representation_ptr->id()] = std::move(new_representation);
  return representation_ptr;
}

Representation* AdaptationSet::CopyRepresentation(
    const Representation& representation) {
  // Note that AdaptationSet outlive Representation, so this object
  // will die before AdaptationSet.
  std::unique_ptr<RepresentationStateChangeListener> listener(
      new RepresentationStateChangeListenerImpl(representation.id(), this));
  std::unique_ptr<Representation> new_representation(
      new Representation(representation, std::move(listener)));

  UpdateFromMediaInfo(new_representation->GetMediaInfo());
  Representation* representation_ptr = new_representation.get();
  representation_map_[representation_ptr->id()] = std::move(new_representation);
  return representation_ptr;
}

void AdaptationSet::AddContentProtectionElement(
    const ContentProtectionElement& content_protection_element) {
  content_protection_elements_.push_back(content_protection_element);
  RemoveDuplicateAttributes(&content_protection_elements_.back());
}

void AdaptationSet::UpdateContentProtectionPssh(const std::string& drm_uuid,
                                                const std::string& pssh) {
  UpdateContentProtectionPsshHelper(drm_uuid, pssh,
                                    &content_protection_elements_);
}

void AdaptationSet::AddAccessibility(const std::string& scheme,
                                     const std::string& value) {
  accessibilities_.push_back(Accessibility{scheme, value});
}

void AdaptationSet::AddRole(Role role) {
  roles_.insert(role);
}

// Creates a copy of <AdaptationSet> xml element, iterate thru all the
// <Representation> (child) elements and add them to the copy.
// Set all the attributes first and then add the children elements so that flags
// can be passed to Representation to avoid setting redundant attributes. For
// example, if AdaptationSet@width is set, then Representation@width is
// redundant and should not be set.
base::Optional<xml::XmlNode> AdaptationSet::GetXml() {
  xml::AdaptationSetXmlNode adaptation_set;

  bool suppress_representation_width = false;
  bool suppress_representation_height = false;
  bool suppress_representation_frame_rate = false;

  if (id_ && !adaptation_set.SetId(id_.value()))
    return base::nullopt;
  if (!adaptation_set.SetStringAttribute("contentType", content_type_))
    return base::nullopt;
  if (!language_.empty() && language_ != "und" &&
      !adaptation_set.SetStringAttribute("lang", language_)) {
    return base::nullopt;
  }

  // Note that std::{set,map} are ordered, so the last element is the max value.
  if (video_widths_.size() == 1) {
    suppress_representation_width = true;
    if (!adaptation_set.SetIntegerAttribute("width", *video_widths_.begin()))
      return base::nullopt;
  } else if (video_widths_.size() > 1) {
    if (!adaptation_set.SetIntegerAttribute("maxWidth",
                                            *video_widths_.rbegin())) {
      return base::nullopt;
    }
  }
  if (video_heights_.size() == 1) {
    suppress_representation_height = true;
    if (!adaptation_set.SetIntegerAttribute("height", *video_heights_.begin()))
      return base::nullopt;
  } else if (video_heights_.size() > 1) {
    if (!adaptation_set.SetIntegerAttribute("maxHeight",
                                            *video_heights_.rbegin())) {
      return base::nullopt;
    }
  }

  if (video_frame_rates_.size() == 1) {
    suppress_representation_frame_rate = true;
    if (!adaptation_set.SetStringAttribute(
            "frameRate", video_frame_rates_.begin()->second)) {
      return base::nullopt;
    }
  } else if (video_frame_rates_.size() > 1) {
    if (!adaptation_set.SetStringAttribute(
            "maxFrameRate", video_frame_rates_.rbegin()->second)) {
      return base::nullopt;
    }
  }

  // Note: must be checked before checking segments_aligned_ (below). So that
  // segments_aligned_ is set before checking below.
  if (mpd_options_.mpd_type == MpdType::kStatic) {
    CheckStaticSegmentAlignment();
  }

  if (segments_aligned_ == kSegmentAlignmentTrue) {
    if (!adaptation_set.SetStringAttribute(
            mpd_options_.dash_profile == DashProfile::kOnDemand
                ? "subsegmentAlignment"
                : "segmentAlignment",
            "true")) {
      return base::nullopt;
    }
  }

  if (picture_aspect_ratio_.size() == 1 &&
      !adaptation_set.SetStringAttribute("par",
                                         *picture_aspect_ratio_.begin())) {
    return base::nullopt;
  }

  if (!adaptation_set.AddContentProtectionElements(
          content_protection_elements_)) {
    return base::nullopt;
  }

  std::string trick_play_reference_ids;
  for (const AdaptationSet* adaptation_set : trick_play_references_) {
    // Should be a whitespace-separated list, see DASH-IOP 3.2.9.
    if (!trick_play_reference_ids.empty())
      trick_play_reference_ids += ' ';
    CHECK(adaptation_set->has_id());
    trick_play_reference_ids += std::to_string(adaptation_set->id());
  }
  if (!trick_play_reference_ids.empty() &&
      !adaptation_set.AddEssentialProperty(
          "http://dashif.org/guidelines/trickmode", trick_play_reference_ids)) {
    return base::nullopt;
  }

  std::string switching_ids;
  for (const AdaptationSet* adaptation_set : switchable_adaptation_sets_) {
    // Should be a comma-separated list, see DASH-IOP 3.8.
    if (!switching_ids.empty())
      switching_ids += ',';
    CHECK(adaptation_set->has_id());
    switching_ids += std::to_string(adaptation_set->id());
  }
  if (!switching_ids.empty() &&
      !adaptation_set.AddSupplementalProperty(
          "urn:mpeg:dash:adaptation-set-switching:2016", switching_ids)) {
    return base::nullopt;
  }

  for (const AdaptationSet::Accessibility& accessibility : accessibilities_) {
    if (!adaptation_set.AddAccessibilityElement(accessibility.scheme,
                                                accessibility.value)) {
      return base::nullopt;
    }
  }

  for (AdaptationSet::Role role : roles_) {
    if (!adaptation_set.AddRoleElement("urn:mpeg:dash:role:2011",
                                       RoleToText(role))) {
      return base::nullopt;
    }
  }

  if (!label_.empty() && !adaptation_set.AddLabelElement(label_))
    return base::nullopt;

  for (const auto& representation_pair : representation_map_) {
    const auto& representation = representation_pair.second;
    if (suppress_representation_width)
      representation->SuppressOnce(Representation::kSuppressWidth);
    if (suppress_representation_height)
      representation->SuppressOnce(Representation::kSuppressHeight);
    if (suppress_representation_frame_rate)
      representation->SuppressOnce(Representation::kSuppressFrameRate);
    auto child = representation->GetXml();
    if (!child || !adaptation_set.AddChild(std::move(*child)))
      return base::nullopt;
  }

  return std::move(adaptation_set);
}

void AdaptationSet::ForceSetSegmentAlignment(bool segment_alignment) {
  segments_aligned_ =
      segment_alignment ? kSegmentAlignmentTrue : kSegmentAlignmentFalse;
  force_set_segment_alignment_ = true;
}

void AdaptationSet::AddAdaptationSetSwitching(
    const AdaptationSet* adaptation_set) {
  switchable_adaptation_sets_.push_back(adaptation_set);
}

// For dynamic MPD, storing all start_time and duration will out-of-memory
// because there's no way of knowing when it will end. Static MPD
// subsegmentAlignment check is *not* done here because it is possible that some
// Representations might not have been added yet (e.g. a thread is assigned per
// muxer so one might run faster than others). To be clear, for dynamic MPD, all
// Representations should be added before a segment is added.
void AdaptationSet::OnNewSegmentForRepresentation(uint32_t representation_id,
                                                  uint64_t start_time,
                                                  uint64_t duration) {
  if (mpd_options_.mpd_type == MpdType::kDynamic) {
    CheckDynamicSegmentAlignment(representation_id, start_time, duration);
  } else {
    representation_segment_start_times_[representation_id].push_back(
        start_time);
  }
}

void AdaptationSet::OnSetFrameRateForRepresentation(uint32_t representation_id,
                                                    uint32_t frame_duration,
                                                    uint32_t timescale) {
  RecordFrameRate(frame_duration, timescale);
}

void AdaptationSet::AddTrickPlayReference(const AdaptationSet* adaptation_set) {
  trick_play_references_.push_back(adaptation_set);
}

const std::list<Representation*> AdaptationSet::GetRepresentations() const {
  std::list<Representation*> representations;
  for (const auto& representation_pair : representation_map_) {
    representations.push_back(representation_pair.second.get());
  }
  return representations;
}

bool AdaptationSet::IsVideo() const {
  return content_type_ == "video";
}

void AdaptationSet::UpdateFromMediaInfo(const MediaInfo& media_info) {
  // For videos, record the width, height, and the frame rate to calculate the
  // max {width,height,framerate} required for DASH IOP.
  if (media_info.has_video_info()) {
    const MediaInfo::VideoInfo& video_info = media_info.video_info();
    DCHECK(video_info.has_width());
    DCHECK(video_info.has_height());
    video_widths_.insert(video_info.width());
    video_heights_.insert(video_info.height());

    if (video_info.has_time_scale() && video_info.has_frame_duration())
      RecordFrameRate(video_info.frame_duration(), video_info.time_scale());

    AddPictureAspectRatio(video_info, &picture_aspect_ratio_);
  }

  if (media_info.has_dash_label())
    label_ = media_info.dash_label();

  if (media_info.has_video_info()) {
    content_type_ = "video";
  } else if (media_info.has_audio_info()) {
    content_type_ = "audio";
  } else if (media_info.has_text_info()) {
    content_type_ = "text";

    if (media_info.text_info().has_type() &&
        (media_info.text_info().type() != MediaInfo::TextInfo::UNKNOWN)) {
      roles_.insert(MediaInfoTextTypeToRole(media_info.text_info().type()));
    }
  }
}

// This implementation assumes that each representations' segments' are
// contiguous.
// Also assumes that all Representations are added before this is called.
// This checks whether the first elements of the lists in
// representation_segment_start_times_ are aligned.
// For example, suppose this method was just called with args rep_id=2
// start_time=1.
// 1 -> [1, 100, 200]
// 2 -> [1]
// The timestamps of the first elements match, so this flags
// segments_aligned_=true.
// Also since the first segment start times match, the first element of all the
// lists are removed, so the map of lists becomes:
// 1 -> [100, 200]
// 2 -> []
// Note that there could be false positives.
// e.g. just got rep_id=3 start_time=1 duration=300, and the duration of the
// whole AdaptationSet is 300.
// 1 -> [1, 100, 200]
// 2 -> [1, 90, 100]
// 3 -> [1]
// They are not aligned but this will be marked as aligned.
// But since this is unlikely to happen in the packager (and to save
// computation), this isn't handled at the moment.
void AdaptationSet::CheckDynamicSegmentAlignment(uint32_t representation_id,
                                                 uint64_t start_time,
                                                 uint64_t /* duration */) {
  if (segments_aligned_ == kSegmentAlignmentFalse ||
      force_set_segment_alignment_) {
    return;
  }

  std::list<uint64_t>& current_representation_start_times =
      representation_segment_start_times_[representation_id];
  current_representation_start_times.push_back(start_time);
  // There's no way to detemine whether the segments are aligned if some
  // representations do not have any segments.
  if (representation_segment_start_times_.size() != representation_map_.size())
    return;

  DCHECK(!current_representation_start_times.empty());
  const uint64_t expected_start_time =
      current_representation_start_times.front();
  for (const auto& key_value : representation_segment_start_times_) {
    const std::list<uint64_t>& representation_start_time = key_value.second;
    // If there are no entries in a list, then there is no way for the
    // segment alignment status to change.
    // Note that it can be empty because entries get deleted below.
    if (representation_start_time.empty())
      return;

    if (expected_start_time != representation_start_time.front()) {
      VLOG(1) << "Seeing Misaligned segments with different start_times: "
              << expected_start_time << " vs "
              << representation_start_time.front();
      // Flag as false and clear the start times data, no need to keep it
      // around.
      segments_aligned_ = kSegmentAlignmentFalse;
      representation_segment_start_times_.clear();
      return;
    }
  }
  segments_aligned_ = kSegmentAlignmentTrue;

  for (auto& key_value : representation_segment_start_times_) {
    std::list<uint64_t>& representation_start_time = key_value.second;
    representation_start_time.pop_front();
  }
}

// Make sure all segements start times match for all Representations.
// This assumes that the segments are contiguous.
void AdaptationSet::CheckStaticSegmentAlignment() {
  if (segments_aligned_ == kSegmentAlignmentFalse ||
      force_set_segment_alignment_) {
    return;
  }
  if (representation_segment_start_times_.empty())
    return;
  if (representation_segment_start_times_.size() == 1) {
    segments_aligned_ = kSegmentAlignmentTrue;
    return;
  }

  // This is not the most efficient implementation to compare the values
  // because expected_time_line is compared against all other time lines, but
  // probably the most readable.
  const std::list<uint64_t>& expected_time_line =
      representation_segment_start_times_.begin()->second;

  bool all_segment_time_line_same_length = true;
  // Note that the first entry is skipped because it is expected_time_line.
  RepresentationTimeline::const_iterator it =
      representation_segment_start_times_.begin();
  for (++it; it != representation_segment_start_times_.end(); ++it) {
    const std::list<uint64_t>& other_time_line = it->second;
    if (expected_time_line.size() != other_time_line.size()) {
      all_segment_time_line_same_length = false;
    }

    const std::list<uint64_t>* longer_list = &other_time_line;
    const std::list<uint64_t>* shorter_list = &expected_time_line;
    if (expected_time_line.size() > other_time_line.size()) {
      shorter_list = &other_time_line;
      longer_list = &expected_time_line;
    }

    if (!std::equal(shorter_list->begin(), shorter_list->end(),
                    longer_list->begin())) {
      // Some segments are definitely unaligned.
      segments_aligned_ = kSegmentAlignmentFalse;
      representation_segment_start_times_.clear();
      return;
    }
  }

  // TODO(rkuroiwa): The right way to do this is to also check the durations.
  // For example:
  // (a)  3 4 5
  // (b)  3 4 5 6
  // could be true or false depending on the length of the third segment of (a).
  // i.e. if length of the third segment is 2, then this is not aligned.
  if (!all_segment_time_line_same_length) {
    segments_aligned_ = kSegmentAlignmentUnknown;
    return;
  }

  segments_aligned_ = kSegmentAlignmentTrue;
}

// Since all AdaptationSet cares about is the maxFrameRate, representation_id
// is not passed to this method.
void AdaptationSet::RecordFrameRate(uint32_t frame_duration,
                                    uint32_t timescale) {
  if (frame_duration == 0) {
    LOG(ERROR) << "Frame duration is 0 and cannot be set.";
    return;
  }
  video_frame_rates_[static_cast<double>(timescale) / frame_duration] =
      base::IntToString(timescale) + "/" + base::IntToString(frame_duration);
}

}  // namespace shaka
