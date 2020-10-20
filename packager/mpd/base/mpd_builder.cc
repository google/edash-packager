// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/mpd/base/mpd_builder.h"

#include <algorithm>

#include "packager/base/files/file_path.h"
#include "packager/base/logging.h"
#include "packager/base/optional.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/base/synchronization/lock.h"
#include "packager/base/time/default_clock.h"
#include "packager/base/time/time.h"
#include "packager/mpd/base/adaptation_set.h"
#include "packager/mpd/base/mpd_utils.h"
#include "packager/mpd/base/period.h"
#include "packager/mpd/base/representation.h"
#include "packager/mpd/base/xml/xml_node.h"
#include "packager/version/version.h"

namespace shaka {

using base::FilePath;
using xml::XmlNode;

namespace {

void AddMpdNameSpaceInfo(XmlNode* mpd) {
  DCHECK(mpd);

  const std::set<std::string> namespaces = mpd->ExtractReferencedNamespaces();

  static const char kXmlNamespace[] = "urn:mpeg:dash:schema:mpd:2011";
  static const char kXmlNamespaceXsi[] =
      "http://www.w3.org/2001/XMLSchema-instance";
  static const char kDashSchemaMpd2011[] =
      "urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd";

  mpd->SetStringAttribute("xmlns", kXmlNamespace);
  mpd->SetStringAttribute("xmlns:xsi", kXmlNamespaceXsi);
  mpd->SetStringAttribute("xsi:schemaLocation", kDashSchemaMpd2011);

  static const char kCencNamespace[] = "urn:mpeg:cenc:2013";
  static const char kMarlinNamespace[] =
      "urn:marlin:mas:1-0:services:schemas:mpd";
  static const char kXmlNamespaceXlink[] = "http://www.w3.org/1999/xlink";
  static const char kMsprNamespace[] = "urn:microsoft:playready";
  static const char kSbdNamespace[] = "urn:mpeg:dash:sbd:2020";

  const std::map<std::string, std::string> uris = {
      {"cenc", kCencNamespace},
      {"mas", kMarlinNamespace},
      {"xlink", kXmlNamespaceXlink},
      {"mspr", kMsprNamespace},
      {"sbd", kSbdNamespace},
  };

  for (const std::string& namespace_name : namespaces) {
    auto iter = uris.find(namespace_name);
    CHECK(iter != uris.end()) << " unexpected namespace " << namespace_name;

    mpd->SetStringAttribute(
        base::StringPrintf("xmlns:%s", namespace_name.c_str()).c_str(),
        iter->second);
  }
}

bool Positive(double d) {
  return d > 0.0;
}

// Return current time in XML DateTime format. The value is in UTC, so the
// string ends with a 'Z'.
std::string XmlDateTimeNowWithOffset(
    int32_t offset_seconds,
    base::Clock* clock) {
  base::Time time = clock->Now();
  time += base::TimeDelta::FromSeconds(offset_seconds);
  base::Time::Exploded time_exploded;
  time.UTCExplode(&time_exploded);

  return base::StringPrintf("%4d-%02d-%02dT%02d:%02d:%02dZ", time_exploded.year,
                            time_exploded.month, time_exploded.day_of_month,
                            time_exploded.hour, time_exploded.minute,
                            time_exploded.second);
}

void SetIfPositive(const char* attr_name, double value, XmlNode* mpd) {
  if (Positive(value)) {
    mpd->SetStringAttribute(attr_name, SecondsToXmlDuration(value));
  }
}

std::string MakePathRelative(const std::string& media_path,
                             const FilePath& parent_path) {
  FilePath relative_path;
  const FilePath child_path = FilePath::FromUTF8Unsafe(media_path);
  const bool is_child =
      parent_path.AppendRelativePath(child_path, &relative_path);
  if (!is_child)
    relative_path = child_path;
  return relative_path.NormalizePathSeparatorsTo('/').AsUTF8Unsafe();
}

// Spooky static initialization/cleanup of libxml.
class LibXmlInitializer {
 public:
  LibXmlInitializer() : initialized_(false) {
    base::AutoLock lock(lock_);
    if (!initialized_) {
      xmlInitParser();
      initialized_ = true;
    }
  }

  ~LibXmlInitializer() {
    base::AutoLock lock(lock_);
    if (initialized_) {
      xmlCleanupParser();
      initialized_ = false;
    }
  }

 private:
  base::Lock lock_;
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(LibXmlInitializer);
};

}  // namespace

MpdBuilder::MpdBuilder(const MpdOptions& mpd_options)
    : mpd_options_(mpd_options), clock_(new base::DefaultClock()) {}

MpdBuilder::~MpdBuilder() {}

void MpdBuilder::AddBaseUrl(const std::string& base_url) {
  base_urls_.push_back(base_url);
}

Period* MpdBuilder::GetOrCreatePeriod(double start_time_in_seconds) {
  for (auto& period : periods_) {
    const double kPeriodTimeDriftThresholdInSeconds = 1.0;
    const bool match =
        std::fabs(period->start_time_in_seconds() - start_time_in_seconds) <
        kPeriodTimeDriftThresholdInSeconds;
    if (match)
      return period.get();
  }
  periods_.emplace_back(new Period(period_counter_++, start_time_in_seconds,
                                   mpd_options_, &representation_counter_));
  return periods_.back().get();
}

bool MpdBuilder::ToString(std::string* output) {
  DCHECK(output);
  static LibXmlInitializer lib_xml_initializer;

  xml::scoped_xml_ptr<xmlDoc> doc(GenerateMpd());
  if (!doc)
    return false;

  static const int kNiceFormat = 1;
  int doc_str_size = 0;
  xmlChar* doc_str = nullptr;
  xmlDocDumpFormatMemoryEnc(doc.get(), &doc_str, &doc_str_size, "UTF-8",
                            kNiceFormat);
  output->assign(doc_str, doc_str + doc_str_size);
  xmlFree(doc_str);

  // Cleanup, free the doc.
  doc.reset();
  return true;
}

xmlDocPtr MpdBuilder::GenerateMpd() {
  // Setup nodes.
  static const char kXmlVersion[] = "1.0";
  xml::scoped_xml_ptr<xmlDoc> doc(xmlNewDoc(BAD_CAST kXmlVersion));
  XmlNode mpd("MPD");

  // Add baseurls to MPD.
  for (const std::string& base_url : base_urls_) {
    XmlNode xml_base_url("BaseURL");
    xml_base_url.SetContent(base_url);

    if (!mpd.AddChild(xml_base_url.PassScopedPtr()))
      return nullptr;
  }

  bool output_period_duration = false;
  if (mpd_options_.mpd_type == MpdType::kStatic) {
    UpdatePeriodDurationAndPresentationTimestamp();
    // Only output period duration if there are more than one period. In the
    // case of only one period, Period@duration is redundant as it is identical
    // to Mpd Duration so the convention is not to output Period@duration.
    output_period_duration = periods_.size() > 1;
  }

  for (const auto& period : periods_) {
    xml::scoped_xml_ptr<xmlNode> period_node(
        period->GetXml(output_period_duration));
    if (!period_node || !mpd.AddChild(std::move(period_node)))
      return nullptr;
  }

  AddMpdNameSpaceInfo(&mpd);

  static const char kOnDemandProfile[] =
      "urn:mpeg:dash:profile:isoff-on-demand:2011";
  static const char kLiveProfile[] =
      "urn:mpeg:dash:profile:isoff-live:2011";
  switch (mpd_options_.dash_profile) {
    case DashProfile::kOnDemand:
      mpd.SetStringAttribute("profiles", kOnDemandProfile);
      break;
    case DashProfile::kLive:
      mpd.SetStringAttribute("profiles", kLiveProfile);
      break;
    default:
      NOTREACHED() << "Unknown DASH profile: "
                   << static_cast<int>(mpd_options_.dash_profile);
      break;
  }

  AddCommonMpdInfo(&mpd);
  switch (mpd_options_.mpd_type) {
    case MpdType::kStatic:
      AddStaticMpdInfo(&mpd);
      break;
    case MpdType::kDynamic:
      AddDynamicMpdInfo(&mpd);
      // Must be after Period element.
      AddUtcTiming(&mpd);
      break;
    default:
      NOTREACHED() << "Unknown MPD type: "
                   << static_cast<int>(mpd_options_.mpd_type);
      break;
  }

  DCHECK(doc);
  const std::string version = GetPackagerVersion();
  if (!version.empty()) {
    std::string version_string =
        base::StringPrintf("Generated with %s version %s",
                           GetPackagerProjectUrl().c_str(), version.c_str());
    xml::scoped_xml_ptr<xmlNode> comment(
        xmlNewDocComment(doc.get(), BAD_CAST version_string.c_str()));
    xmlDocSetRootElement(doc.get(), comment.get());
    xmlAddSibling(comment.release(), mpd.Release());
  } else {
    xmlDocSetRootElement(doc.get(), mpd.Release());
  }
  return doc.release();
}

void MpdBuilder::AddCommonMpdInfo(XmlNode* mpd_node) {
  if (Positive(mpd_options_.mpd_params.min_buffer_time)) {
    mpd_node->SetStringAttribute(
        "minBufferTime",
        SecondsToXmlDuration(mpd_options_.mpd_params.min_buffer_time));
  } else {
    LOG(ERROR) << "minBufferTime value not specified.";
    // TODO(tinskip): Propagate error.
  }
}

void MpdBuilder::AddStaticMpdInfo(XmlNode* mpd_node) {
  DCHECK(mpd_node);
  DCHECK_EQ(MpdType::kStatic, mpd_options_.mpd_type);

  static const char kStaticMpdType[] = "static";
  mpd_node->SetStringAttribute("type", kStaticMpdType);
  mpd_node->SetStringAttribute("mediaPresentationDuration",
                               SecondsToXmlDuration(GetStaticMpdDuration()));
}

void MpdBuilder::AddDynamicMpdInfo(XmlNode* mpd_node) {
  DCHECK(mpd_node);
  DCHECK_EQ(MpdType::kDynamic, mpd_options_.mpd_type);

  static const char kDynamicMpdType[] = "dynamic";
  mpd_node->SetStringAttribute("type", kDynamicMpdType);

  // No offset from NOW.
  mpd_node->SetStringAttribute("publishTime",
                               XmlDateTimeNowWithOffset(0, clock_.get()));

  // 'availabilityStartTime' is required for dynamic profile. Calculate if
  // not already calculated.
  if (availability_start_time_.empty()) {
    double earliest_presentation_time;
    if (GetEarliestTimestamp(&earliest_presentation_time)) {
      availability_start_time_ = XmlDateTimeNowWithOffset(
          -std::ceil(earliest_presentation_time), clock_.get());
    } else {
      LOG(ERROR) << "Could not determine the earliest segment presentation "
                    "time for availabilityStartTime calculation.";
      // TODO(tinskip). Propagate an error.
    }
  }
  if (!availability_start_time_.empty())
    mpd_node->SetStringAttribute("availabilityStartTime",
                                 availability_start_time_);

  if (Positive(mpd_options_.mpd_params.minimum_update_period)) {
    mpd_node->SetStringAttribute(
        "minimumUpdatePeriod",
        SecondsToXmlDuration(mpd_options_.mpd_params.minimum_update_period));
  } else {
    LOG(WARNING) << "The profile is dynamic but no minimumUpdatePeriod "
                    "specified.";
  }

  SetIfPositive("timeShiftBufferDepth",
                mpd_options_.mpd_params.time_shift_buffer_depth, mpd_node);
  SetIfPositive("suggestedPresentationDelay",
                mpd_options_.mpd_params.suggested_presentation_delay, mpd_node);
}

void MpdBuilder::AddUtcTiming(XmlNode* mpd_node) {
  DCHECK(mpd_node);
  DCHECK_EQ(MpdType::kDynamic, mpd_options_.mpd_type);

  for (const MpdParams::UtcTiming& utc_timing :
       mpd_options_.mpd_params.utc_timings) {
    XmlNode utc_timing_node("UTCTiming");
    utc_timing_node.SetStringAttribute("schemeIdUri", utc_timing.scheme_id_uri);
    utc_timing_node.SetStringAttribute("value", utc_timing.value);
    mpd_node->AddChild(utc_timing_node.PassScopedPtr());
  }
}

float MpdBuilder::GetStaticMpdDuration() {
  DCHECK_EQ(MpdType::kStatic, mpd_options_.mpd_type);

  float total_duration = 0.0f;
  for (const auto& period : periods_) {
    total_duration += period->duration_seconds();
  }
  return total_duration;
}

bool MpdBuilder::GetEarliestTimestamp(double* timestamp_seconds) {
  DCHECK(timestamp_seconds);
  DCHECK(!periods_.empty());
  double timestamp = 0;
  double earliest_timestamp = -1;
  // TODO(kqyang): This is used to set availabilityStartTime. We may consider
  // set presentationTimeOffset in the Representations then we can set
  // availabilityStartTime to the time when MPD is first generated.
  // The first period should have the earliest timestamp.
  for (const auto* adaptation_set : periods_.front()->GetAdaptationSets()) {
    for (const auto* representation : adaptation_set->GetRepresentations()) {
      if (representation->GetStartAndEndTimestamps(&timestamp, nullptr) &&
          (earliest_timestamp < 0 || timestamp < earliest_timestamp)) {
        earliest_timestamp = timestamp;
      }
    }
  }
  if (earliest_timestamp < 0)
    return false;
  *timestamp_seconds = earliest_timestamp;
  return true;
}

void MpdBuilder::UpdatePeriodDurationAndPresentationTimestamp() {
  DCHECK_EQ(MpdType::kStatic, mpd_options_.mpd_type);

  for (const auto& period : periods_) {
    std::list<Representation*> video_representations;
    std::list<Representation*> non_video_representations;
    for (const auto& adaptation_set : period->GetAdaptationSets()) {
      const auto& representations = adaptation_set->GetRepresentations();
      if (adaptation_set->IsVideo()) {
        video_representations.insert(video_representations.end(),
                                     representations.begin(),
                                     representations.end());
      } else {
        non_video_representations.insert(non_video_representations.end(),
                                         representations.begin(),
                                         representations.end());
      }
    }

    base::Optional<double> earliest_start_time;
    base::Optional<double> latest_end_time;
    // The timestamps are based on Video Representations if exist.
    const auto& representations = video_representations.size() > 0
                                      ? video_representations
                                      : non_video_representations;
    for (const auto& representation : representations) {
      double start_time = 0;
      double end_time = 0;
      if (representation->GetStartAndEndTimestamps(&start_time, &end_time)) {
        earliest_start_time =
            std::min(earliest_start_time.value_or(start_time), start_time);
        latest_end_time =
            std::max(latest_end_time.value_or(end_time), end_time);
      }
    }

    if (!earliest_start_time)
      return;

    period->set_duration_seconds(*latest_end_time - *earliest_start_time);

    double presentation_time_offset = *earliest_start_time;
    for (const auto& adaptation_set : period->GetAdaptationSets()) {
      for (const auto& representation : adaptation_set->GetRepresentations()) {
        representation->SetPresentationTimeOffset(presentation_time_offset);
      }
    }
  }
}

void MpdBuilder::MakePathsRelativeToMpd(const std::string& mpd_path,
                                        MediaInfo* media_info) {
  DCHECK(media_info);
  const std::string kFileProtocol("file://");
  std::string mpd_file_path = (mpd_path.find(kFileProtocol) == 0)
                                  ? mpd_path.substr(kFileProtocol.size())
                                  : mpd_path;

  if (!mpd_file_path.empty()) {
    const FilePath mpd_dir(FilePath::FromUTF8Unsafe(mpd_file_path)
                               .DirName()
                               .AsEndingWithSeparator());
    if (!mpd_dir.empty()) {
      if (media_info->has_media_file_name()) {
        media_info->set_media_file_url(
            MakePathRelative(media_info->media_file_name(), mpd_dir));
      }
      if (media_info->has_init_segment_name()) {
        media_info->set_init_segment_url(
            MakePathRelative(media_info->init_segment_name(), mpd_dir));
      }
      if (media_info->has_segment_template()) {
        media_info->set_segment_template_url(
            MakePathRelative(media_info->segment_template(), mpd_dir));
      }
    }
  }
}

}  // namespace shaka
