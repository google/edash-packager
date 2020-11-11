// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/mpd/base/period.h"

#include "packager/base/stl_util.h"
#include "packager/mpd/base/adaptation_set.h"
#include "packager/mpd/base/mpd_options.h"
#include "packager/mpd/base/mpd_utils.h"
#include "packager/mpd/base/xml/xml_node.h"

namespace shaka {
namespace {

const std::string& GetDefaultAudioLanguage(const MpdOptions& mpd_options) {
  return mpd_options.mpd_params.default_language;
}

const std::string& GetDefaultTextLanguage(const MpdOptions& mpd_options) {
  return mpd_options.mpd_params.default_text_language.empty()
             ? mpd_options.mpd_params.default_language
             : mpd_options.mpd_params.default_text_language;
}

AdaptationSet::Role RoleFromString(const std::string& role_str) {
  if (role_str == "caption")
    return AdaptationSet::Role::kRoleCaption;
  if (role_str == "subtitle")
    return AdaptationSet::Role::kRoleSubtitle;
  if (role_str == "main")
    return AdaptationSet::Role::kRoleMain;
  if (role_str == "alternate")
    return AdaptationSet::Role::kRoleAlternate;
  if (role_str == "supplementary")
    return AdaptationSet::Role::kRoleSupplementary;
  if (role_str == "commentary")
    return AdaptationSet::Role::kRoleCommentary;
  if (role_str == "dub")
    return AdaptationSet::Role::kRoleDub;
  return AdaptationSet::Role::kRoleUnknown;
}

}  // namespace

Period::Period(uint32_t period_id,
               double start_time_in_seconds,
               const MpdOptions& mpd_options,
               uint32_t* representation_counter)
    : id_(period_id),
      start_time_in_seconds_(start_time_in_seconds),
      mpd_options_(mpd_options),
      representation_counter_(representation_counter) {}

AdaptationSet* Period::GetOrCreateAdaptationSet(
    const MediaInfo& media_info,
    bool content_protection_in_adaptation_set) {
  // Set duration if it is not set. It may be updated later from duration
  // calculated from segments.
  if (duration_seconds_ == 0)
    duration_seconds_ = media_info.media_duration_seconds();

  const std::string key = GetAdaptationSetKey(
      media_info, mpd_options_.mpd_params.allow_codec_switching);

  std::list<AdaptationSet*>& adaptation_sets = adaptation_set_list_map_[key];

  for (AdaptationSet* adaptation_set : adaptation_sets) {
    if (adaptation_set->MatchAdaptationSet(
            media_info, content_protection_in_adaptation_set))
      return adaptation_set;
  }

  // None of the adaptation sets match with the new content protection.
  // Need a new one.
  const std::string language = GetLanguage(media_info);
  std::unique_ptr<AdaptationSet> new_adaptation_set =
      NewAdaptationSet(language, mpd_options_, representation_counter_);
  if (!SetNewAdaptationSetAttributes(language, media_info, adaptation_sets,
                                     content_protection_in_adaptation_set,
                                     new_adaptation_set.get())) {
    return nullptr;
  }

  for (AdaptationSet* adaptation_set : adaptation_sets) {
    if (adaptation_set->SwitchableAdaptationSet(*new_adaptation_set)) {
      adaptation_set->AddAdaptationSetSwitching(new_adaptation_set.get());
      new_adaptation_set->AddAdaptationSetSwitching(adaptation_set);
    }
  }

  AdaptationSet* adaptation_set_ptr = new_adaptation_set.get();
  adaptation_sets.push_back(adaptation_set_ptr);
  adaptation_sets_.emplace_back(std::move(new_adaptation_set));
  return adaptation_set_ptr;
}

xml::scoped_xml_ptr<xmlNode> Period::GetXml(bool output_period_duration) {
  adaptation_sets_.sort(
      [](const std::unique_ptr<AdaptationSet>& adaptation_set_a,
         const std::unique_ptr<AdaptationSet>& adaptation_set_b) {
        if (!adaptation_set_a->has_id())
          return false;
        if (!adaptation_set_b->has_id())
          return true;
        return adaptation_set_a->id() < adaptation_set_b->id();
      });

  xml::XmlNode period("Period");

  // Required for 'dynamic' MPDs.
  period.SetId(id_);
  // Iterate thru AdaptationSets and add them to one big Period element.
  for (const auto& adaptation_set : adaptation_sets_) {
    xml::scoped_xml_ptr<xmlNode> child(adaptation_set->GetXml());
    if (!child || !period.AddChild(std::move(child)))
      return nullptr;
  }

  if (output_period_duration) {
    period.SetStringAttribute("duration",
                              SecondsToXmlDuration(duration_seconds_));
  } else if (mpd_options_.mpd_type == MpdType::kDynamic) {
    period.SetStringAttribute("start",
                              SecondsToXmlDuration(start_time_in_seconds_));
  }
  return period.PassScopedPtr();
}

const std::list<AdaptationSet*> Period::GetAdaptationSets() const {
  std::list<AdaptationSet*> adaptation_sets;
  for (const auto& adaptation_set : adaptation_sets_) {
    adaptation_sets.push_back(adaptation_set.get());
  }
  return adaptation_sets;
}

std::unique_ptr<AdaptationSet> Period::NewAdaptationSet(
    const std::string& language,
    const MpdOptions& options,
    uint32_t* representation_counter) {
  return std::unique_ptr<AdaptationSet>(
      new AdaptationSet(language, options, representation_counter));
}

bool Period::SetNewAdaptationSetAttributes(
    const std::string& language,
    const MediaInfo& media_info,
    const std::list<AdaptationSet*>& adaptation_sets,
    bool content_protection_in_adaptation_set,
    AdaptationSet* new_adaptation_set) {
  if (!media_info.dash_roles().empty()) {
    for (const std::string& role_str : media_info.dash_roles()) {
      AdaptationSet::Role role = RoleFromString(role_str);
      if (role == AdaptationSet::kRoleUnknown) {
        LOG(ERROR) << "Unrecognized role '" << role_str << "'.";
        return false;
      }
      new_adaptation_set->AddRole(role);
    }
  } else if (!language.empty()) {
    const bool is_main_role =
        language == (media_info.has_audio_info()
                         ? GetDefaultAudioLanguage(mpd_options_)
                         : GetDefaultTextLanguage(mpd_options_));
    if (is_main_role)
      new_adaptation_set->AddRole(AdaptationSet::kRoleMain);
  }
  for (const std::string& accessibility : media_info.dash_accessibilities()) {
    size_t pos = accessibility.find('=');
    if (pos == std::string::npos) {
      LOG(ERROR)
          << "Accessibility should be in scheme=value format, but seeing "
          << accessibility;
      return false;
    }
    new_adaptation_set->AddAccessibility(accessibility.substr(0, pos),
                                         accessibility.substr(pos + 1));
  }

  new_adaptation_set->set_codec(GetBaseCodec(media_info));

  if (media_info.has_video_info()) {
    // Because 'language' is ignored for videos, |adaptation_sets| must have
    // all the video AdaptationSets.
    if (adaptation_sets.size() > 1) {
      new_adaptation_set->AddRole(AdaptationSet::kRoleMain);
    } else if (adaptation_sets.size() == 1) {
      (*adaptation_sets.begin())->AddRole(AdaptationSet::kRoleMain);
      new_adaptation_set->AddRole(AdaptationSet::kRoleMain);
    }

    if (media_info.video_info().has_playback_rate()) {
      std::string trick_play_reference_adaptation_set_key;
      AdaptationSet* trick_play_reference_adaptation_set =
          FindMatchingAdaptationSetForTrickPlay(
              media_info, content_protection_in_adaptation_set,
              &trick_play_reference_adaptation_set_key);
      if (trick_play_reference_adaptation_set) {
        new_adaptation_set->AddTrickPlayReference(
            trick_play_reference_adaptation_set);
      } else {
        trickplay_cache_[trick_play_reference_adaptation_set_key].push_back(
            new_adaptation_set);
      }
    } else {
      std::string trick_play_adaptation_set_key;
      AdaptationSet* trickplay_adaptation_set =
          FindMatchingAdaptationSetForTrickPlay(
              media_info, content_protection_in_adaptation_set,
              &trick_play_adaptation_set_key);
      if (trickplay_adaptation_set) {
        trickplay_adaptation_set->AddTrickPlayReference(new_adaptation_set);
        trickplay_cache_.erase(trick_play_adaptation_set_key);
      }
    }

  } else if (media_info.has_text_info()) {
    // IOP requires all AdaptationSets to have (sub)segmentAlignment set to
    // true, so carelessly set it to true.
    // In practice it doesn't really make sense to adapt between text tracks.
    new_adaptation_set->ForceSetSegmentAlignment(true);
  }

  if (content_protection_in_adaptation_set &&
      media_info.has_protected_content()) {
    new_adaptation_set->set_protected_content(media_info);
    AddContentProtectionElements(media_info, new_adaptation_set);
  }

  return true;
}

AdaptationSet* Period::FindMatchingAdaptationSetForTrickPlay(
    const MediaInfo& media_info,
    bool content_protection_in_adaptation_set,
    std::string* adaptation_set_key) {
  std::list<AdaptationSet*>* adaptation_sets = nullptr;
  const bool is_trickplay_adaptation_set =
      media_info.video_info().has_playback_rate();
  if (is_trickplay_adaptation_set) {
    *adaptation_set_key = GetAdaptationSetKeyForTrickPlay(media_info);
    if (adaptation_set_list_map_.find(*adaptation_set_key) ==
        adaptation_set_list_map_.end())
      return nullptr;
    adaptation_sets = &adaptation_set_list_map_[*adaptation_set_key];
  } else {
    *adaptation_set_key = GetAdaptationSetKey(
        media_info, mpd_options_.mpd_params.allow_codec_switching);
    if (trickplay_cache_.find(*adaptation_set_key) == trickplay_cache_.end())
      return nullptr;
    adaptation_sets = &trickplay_cache_[*adaptation_set_key];
  }
  for (AdaptationSet* adaptation_set : *adaptation_sets) {
    if (adaptation_set->MatchAdaptationSet(
            media_info, content_protection_in_adaptation_set))
      return adaptation_set;
  }

  return nullptr;
}

std::string Period::GetAdaptationSetKeyForTrickPlay(
    const MediaInfo& media_info) {
  MediaInfo media_info_no_trickplay = media_info;
  media_info_no_trickplay.mutable_video_info()->clear_playback_rate();
  return GetAdaptationSetKey(media_info_no_trickplay,
                             mpd_options_.mpd_params.allow_codec_switching);
}

Period::~Period() {
  if (!trickplay_cache_.empty()) {
    LOG(WARNING) << "Trickplay adaptation set did not get a valid adaptation "
                    "set match. Please check the command line options.";
  }
}

}  // namespace shaka
