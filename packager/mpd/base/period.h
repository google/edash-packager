// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
/// All the methods that are virtual are virtual for mocking.

#ifndef PACKAGER_MPD_BASE_PERIOD_H_
#define PACKAGER_MPD_BASE_PERIOD_H_

#include <list>
#include <map>

#include "packager/base/optional.h"
#include "packager/mpd/base/adaptation_set.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/xml/scoped_xml_ptr.h"

namespace shaka {

struct MpdOptions;

namespace xml {
class XmlNode;
}  // namespace xml

/// Period class maps to <Period> element and provides methods to add
/// AdaptationSets.
class Period {
 public:
  virtual ~Period();

  /// Check the existing AdaptationSets, if there is one matching the provided
  /// @a media_info, return it; otherwise a new AdaptationSet is created and
  /// returned.
  /// @param media_info contains media information, which is used to match
  ///        AdaptationSets.
  /// @param content_protection_in_adaptation_set determines if the
  ///        ContentProtection is placed in AdaptationSet or Representation
  ///        element. This affects how MediaInfo in AdaptationSets are matched.
  /// @return the AdaptationSet matching @a media_info if found; otherwise
  ///         return a new AdaptationSet.
  // TODO(kqyang): Move |content_protection_in_adaptation_set| to Period
  // constructor.
  virtual AdaptationSet* GetOrCreateAdaptationSet(
      const MediaInfo& media_info,
      bool content_protection_in_adaptation_set);

  /// Generates <Period> xml element with its child AdaptationSet elements.
  /// @return On success returns a non-NULL scoped_xml_ptr. Otherwise returns a
  ///         NULL scoped_xml_ptr.
  xml::scoped_xml_ptr<xmlNode> GetXml(bool output_period_duration);

  /// @return The list of AdaptationSets in this Period.
  const std::list<AdaptationSet*> GetAdaptationSets() const;

  /// @return The start time of this Period.
  double start_time_in_seconds() const { return start_time_in_seconds_; }

  /// @return period duration in seconds.
  double duration_seconds() const { return duration_seconds_; }

  /// Set period duration.
  void set_duration_seconds(double duration_seconds) {
    duration_seconds_ = duration_seconds;
  }

  /// @return trickplay_cache.
  const std::map<std::string, std::list<AdaptationSet*>>& trickplay_cache()
      const {
    return trickplay_cache_;
  }

 protected:
  /// @param period_id is an ID number for this Period.
  /// @param start_time_in_seconds is the start time for this Period.
  /// @param mpd_options is the options for this MPD.
  /// @param representation_counter is a counter for assigning ID numbers to
  ///        Representation. It can not be NULL.
  Period(uint32_t period_id,
         double start_time_in_seconds,
         const MpdOptions& mpd_options,
         uint32_t* representation_counter);

 private:
  Period(const Period&) = delete;
  Period& operator=(const Period&) = delete;

  friend class MpdBuilder;
  friend class PeriodTest;

  // Calls AdaptationSet constructor. For mock injection.
  virtual std::unique_ptr<AdaptationSet> NewAdaptationSet(
      const std::string& lang,
      const MpdOptions& options,
      uint32_t* representation_counter);

  // Helper function to set new AdaptationSet attributes.
  bool SetNewAdaptationSetAttributes(
      const std::string& language,
      const MediaInfo& media_info,
      const std::list<AdaptationSet*>& adaptation_sets,
      bool content_protection_in_adaptation_set,
      AdaptationSet* new_adaptation_set);

  // If processing a trick play AdaptationSet, gets the original AdaptationSet
  // which the trick play video belongs to.It is assumed that the corresponding
  // AdaptationSet has been created before the trick play AdaptationSet.
  // Returns the matching AdaptationSet if found, otherwise returns nullptr;
  // If processing non-trick play AdaptationSet, gets the trick play
  // AdaptationSet that belongs to current AdaptationSet from trick play cache.
  // Returns nullptr if matching trick play AdaptationSet is not found.
  AdaptationSet* FindMatchingAdaptationSetForTrickPlay(
      const MediaInfo& media_info,
      bool content_protection_in_adaptation_set,
      std::string* adaptation_set_key);

  // Returns AdaptationSet key without ':trickplay' in it for trickplay
  // AdaptationSet.
  std::string GetAdaptationSetKeyForTrickPlay(const MediaInfo& media_info);

  // FindMatchingAdaptationSetForTrickPlay
  const uint32_t id_;
  const double start_time_in_seconds_;
  double duration_seconds_ = 0;
  const MpdOptions& mpd_options_;
  uint32_t* const representation_counter_;
  std::list<std::unique_ptr<AdaptationSet>> adaptation_sets_;
  // AdaptationSets grouped by a specific adaptation set grouping key.
  // AdaptationSets with the same key contain identical parameters except
  // ContentProtection parameters. A single AdaptationSet would be created
  // if they contain identical ContentProtection elements. This map is only
  // useful when ContentProtection element is placed in AdaptationSet.
  std::map<std::string, std::list<AdaptationSet*>> adaptation_set_list_map_;
  // Contains Trickplay AdaptationSets grouped by specific adaptation set
  // grouping key. These AdaptationSets still have not found reference
  // AdaptationSet.
  std::map<std::string, std::list<AdaptationSet*>> trickplay_cache_;
};

}  // namespace shaka

#endif  // PACKAGER_MPD_BASE_PERIOD_H_
