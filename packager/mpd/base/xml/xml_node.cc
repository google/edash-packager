// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/mpd/base/xml/xml_node.h"

#include <gflags/gflags.h>
#include <libxml/tree.h>

#include <cmath>
#include <limits>
#include <set>

#include "packager/base/logging.h"
#include "packager/base/macros.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/sys_byteorder.h"
#include "packager/media/base/rcheck.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/mpd_utils.h"
#include "packager/mpd/base/segment_info.h"
#include "packager/mpd/base/xml/scoped_xml_ptr.h"

DEFINE_bool(segment_template_constant_duration,
            false,
            "Generates SegmentTemplate@duration if all segments except the "
            "last one has the same duration if this flag is set to true.");

DEFINE_bool(dash_add_last_segment_number_when_needed,
            false,
            "Adds a Supplemental Descriptor with @schemeIdUri "
            "set to http://dashif.org/guidelines/last-segment-number with "
            "the @value set to the last segment number.");

namespace shaka {

using xml::XmlNode;
typedef MediaInfo::AudioInfo AudioInfo;
typedef MediaInfo::VideoInfo VideoInfo;

namespace {
const char kEC3Codec[] = "ec-3";
const char kAC4Codec[] = "ac-4";

std::string RangeToString(const Range& range) {
  return base::Uint64ToString(range.begin()) + "-" +
         base::Uint64ToString(range.end());
}

// Check if segments are continuous and all segments except the last one are of
// the same duration.
bool IsTimelineConstantDuration(const std::list<SegmentInfo>& segment_infos,
                                uint32_t start_number) {
  if (!FLAGS_segment_template_constant_duration)
    return false;

  DCHECK(!segment_infos.empty());
  if (segment_infos.size() > 2)
    return false;

  const SegmentInfo& first_segment = segment_infos.front();
  if (first_segment.start_time != first_segment.duration * (start_number - 1))
    return false;

  if (segment_infos.size() == 1)
    return true;

  const SegmentInfo& last_segment = segment_infos.back();
  if (last_segment.repeat != 0)
    return false;

  const int64_t expected_last_segment_start_time =
      first_segment.start_time +
      first_segment.duration * (first_segment.repeat + 1);
  return expected_last_segment_start_time == last_segment.start_time;
}

bool PopulateSegmentTimeline(const std::list<SegmentInfo>& segment_infos,
                             XmlNode* segment_timeline) {
  for (const SegmentInfo& segment_info : segment_infos) {
    XmlNode s_element("S");
    RCHECK(s_element.SetIntegerAttribute("t", segment_info.start_time));
    RCHECK(s_element.SetIntegerAttribute("d", segment_info.duration));
    if (segment_info.repeat > 0)
      RCHECK(s_element.SetIntegerAttribute("r", segment_info.repeat));

    RCHECK(segment_timeline->AddChild(std::move(s_element)));
  }

  return true;
}

void CollectNamespaceFromName(const std::string& name,
                              std::set<std::string>* namespaces) {
  const size_t pos = name.find(':');
  if (pos != std::string::npos)
    namespaces->insert(name.substr(0, pos));
}

void TraverseAttrsAndCollectNamespaces(const xmlAttr* attr,
                                       std::set<std::string>* namespaces) {
  for (const xmlAttr* cur_attr = attr; cur_attr; cur_attr = cur_attr->next) {
    CollectNamespaceFromName(reinterpret_cast<const char*>(cur_attr->name),
                             namespaces);
  }
}

void TraverseNodesAndCollectNamespaces(const xmlNode* node,
                                       std::set<std::string>* namespaces) {
  for (const xmlNode* cur_node = node; cur_node; cur_node = cur_node->next) {
    CollectNamespaceFromName(reinterpret_cast<const char*>(cur_node->name),
                             namespaces);

    TraverseNodesAndCollectNamespaces(cur_node->children, namespaces);
    TraverseAttrsAndCollectNamespaces(cur_node->properties, namespaces);
  }
}

}  // namespace

namespace xml {

class XmlNode::Impl {
 public:
  scoped_xml_ptr<xmlNode> node;
};

XmlNode::XmlNode(const std::string& name) : impl_(new Impl) {
  impl_->node.reset(xmlNewNode(NULL, BAD_CAST name.c_str()));
  DCHECK(impl_->node);
}

XmlNode::XmlNode(XmlNode&&) = default;

XmlNode::~XmlNode() {}

XmlNode& XmlNode::operator=(XmlNode&&) = default;

bool XmlNode::AddChild(XmlNode child) {
  DCHECK(impl_->node);
  DCHECK(child.impl_->node);
  RCHECK(xmlAddChild(impl_->node.get(), child.impl_->node.get()));

  // Reaching here means the ownership of |child| transfered to |node|.
  // Release the pointer so that it doesn't get destructed in this scope.
  ignore_result(child.impl_->node.release());
  return true;
}

bool XmlNode::AddElements(const std::vector<Element>& elements) {
  for (size_t element_index = 0; element_index < elements.size();
       ++element_index) {
    const Element& child_element = elements[element_index];
    XmlNode child_node(child_element.name);
    for (std::map<std::string, std::string>::const_iterator attribute_it =
             child_element.attributes.begin();
         attribute_it != child_element.attributes.end(); ++attribute_it) {
      RCHECK(child_node.SetStringAttribute(attribute_it->first,
                                           attribute_it->second));
    }

    // Note that somehow |SetContent| needs to be called before |AddElements|
    // otherwise the added children will be overwritten by the content.
    child_node.SetContent(child_element.content);

    // Recursively set children for the child.
    RCHECK(child_node.AddElements(child_element.subelements));

    if (!xmlAddChild(impl_->node.get(), child_node.impl_->node.get())) {
      LOG(ERROR) << "Failed to set child " << child_element.name
                 << " to parent element "
                 << reinterpret_cast<const char*>(impl_->node->name);
      return false;
    }
    // Reaching here means the ownership of |child_node| transfered to |node|.
    // Release the pointer so that it doesn't get destructed in this scope.
    child_node.impl_->node.release();
  }
  return true;
}

bool XmlNode::SetStringAttribute(const std::string& attribute_name,
                                 const std::string& attribute) {
  DCHECK(impl_->node);
  return xmlSetProp(impl_->node.get(), BAD_CAST attribute_name.c_str(),
                    BAD_CAST attribute.c_str()) != nullptr;
}

bool XmlNode::SetIntegerAttribute(const std::string& attribute_name,
                                  uint64_t number) {
  DCHECK(impl_->node);
  return xmlSetProp(impl_->node.get(), BAD_CAST attribute_name.c_str(),
                    BAD_CAST(base::Uint64ToString(number).c_str())) != nullptr;
}

bool XmlNode::SetFloatingPointAttribute(const std::string& attribute_name,
                                        double number) {
  DCHECK(impl_->node);
  return xmlSetProp(impl_->node.get(), BAD_CAST attribute_name.c_str(),
                    BAD_CAST(base::DoubleToString(number).c_str())) != nullptr;
}

bool XmlNode::SetId(uint32_t id) {
  return SetIntegerAttribute("id", id);
}

void XmlNode::AddContent(const std::string& content) {
  DCHECK(impl_->node);
  xmlNodeAddContent(impl_->node.get(), BAD_CAST content.c_str());
}

void XmlNode::SetContent(const std::string& content) {
  DCHECK(impl_->node);
  xmlNodeSetContent(impl_->node.get(), BAD_CAST content.c_str());
}

std::set<std::string> XmlNode::ExtractReferencedNamespaces() const {
  std::set<std::string> namespaces;
  TraverseNodesAndCollectNamespaces(impl_->node.get(), &namespaces);
  return namespaces;
}

std::string XmlNode::ToString(const std::string& comment) const {
  // Create an xmlDoc from xmlNodePtr. The node is copied so ownership does not
  // transfer.
  xml::scoped_xml_ptr<xmlDoc> doc(xmlNewDoc(BAD_CAST "1.0"));
  if (comment.empty()) {
    xmlDocSetRootElement(doc.get(), xmlCopyNode(impl_->node.get(), true));
  } else {
    xml::scoped_xml_ptr<xmlNode> comment_xml(
        xmlNewDocComment(doc.get(), BAD_CAST comment.c_str()));
    xmlDocSetRootElement(doc.get(), comment_xml.get());
    xmlAddSibling(comment_xml.release(), xmlCopyNode(impl_->node.get(), true));
  }

  // Format the xmlDoc to string.
  static const int kNiceFormat = 1;
  int doc_str_size = 0;
  xmlChar* doc_str = nullptr;
  xmlDocDumpFormatMemoryEnc(doc.get(), &doc_str, &doc_str_size, "UTF-8",
                            kNiceFormat);
  std::string output(doc_str, doc_str + doc_str_size);
  xmlFree(doc_str);
  return output;
}

bool XmlNode::GetAttribute(const std::string& name, std::string* value) const {
  xml::scoped_xml_ptr<xmlChar> str(
      xmlGetProp(impl_->node.get(), BAD_CAST name.c_str()));
  if (!str)
    return false;
  *value = reinterpret_cast<const char*>(str.get());
  return true;
}

xmlNode* XmlNode::GetRawPtr() const {
  return impl_->node.get();
}

RepresentationBaseXmlNode::RepresentationBaseXmlNode(const std::string& name)
    : XmlNode(name) {}
RepresentationBaseXmlNode::~RepresentationBaseXmlNode() {}

bool RepresentationBaseXmlNode::AddContentProtectionElements(
    const std::list<ContentProtectionElement>& content_protection_elements) {
  for (const auto& elem : content_protection_elements) {
    RCHECK(AddContentProtectionElement(elem));
  }

  return true;
}

bool RepresentationBaseXmlNode::AddSupplementalProperty(
    const std::string& scheme_id_uri,
    const std::string& value) {
  return AddDescriptor("SupplementalProperty", scheme_id_uri, value);
}

bool RepresentationBaseXmlNode::AddEssentialProperty(
    const std::string& scheme_id_uri,
    const std::string& value) {
  return AddDescriptor("EssentialProperty", scheme_id_uri, value);
}

bool RepresentationBaseXmlNode::AddDescriptor(
    const std::string& descriptor_name,
    const std::string& scheme_id_uri,
    const std::string& value) {
  XmlNode descriptor(descriptor_name);
  RCHECK(descriptor.SetStringAttribute("schemeIdUri", scheme_id_uri));
  if (!value.empty())
    RCHECK(descriptor.SetStringAttribute("value", value));
  return AddChild(std::move(descriptor));
}

bool RepresentationBaseXmlNode::AddContentProtectionElement(
    const ContentProtectionElement& content_protection_element) {
  XmlNode content_protection_node("ContentProtection");

  // @value is an optional attribute.
  if (!content_protection_element.value.empty()) {
    RCHECK(content_protection_node.SetStringAttribute(
        "value", content_protection_element.value));
  }
  RCHECK(content_protection_node.SetStringAttribute(
      "schemeIdUri", content_protection_element.scheme_id_uri));

  for (const auto& pair : content_protection_element.additional_attributes) {
    RCHECK(content_protection_node.SetStringAttribute(pair.first, pair.second));
  }

  RCHECK(content_protection_node.AddElements(
      content_protection_element.subelements));
  return AddChild(std::move(content_protection_node));
}

AdaptationSetXmlNode::AdaptationSetXmlNode()
    : RepresentationBaseXmlNode("AdaptationSet") {}
AdaptationSetXmlNode::~AdaptationSetXmlNode() {}

bool AdaptationSetXmlNode::AddAccessibilityElement(
    const std::string& scheme_id_uri,
    const std::string& value) {
  return AddDescriptor("Accessibility", scheme_id_uri, value);
}

bool AdaptationSetXmlNode::AddRoleElement(const std::string& scheme_id_uri,
                                          const std::string& value) {
  return AddDescriptor("Role", scheme_id_uri, value);
}

bool AdaptationSetXmlNode::AddLabelElement(const std::string& value) {
  XmlNode descriptor("Label");
  descriptor.SetContent(value);
  return AddChild(std::move(descriptor));
}

RepresentationXmlNode::RepresentationXmlNode()
    : RepresentationBaseXmlNode("Representation") {}
RepresentationXmlNode::~RepresentationXmlNode() {}

bool RepresentationXmlNode::AddVideoInfo(const VideoInfo& video_info,
                                         bool set_width,
                                         bool set_height,
                                         bool set_frame_rate) {
  if (!video_info.has_width() || !video_info.has_height()) {
    LOG(ERROR) << "Missing width or height for adding a video info.";
    return false;
  }

  if (video_info.has_pixel_width() && video_info.has_pixel_height()) {
    RCHECK(SetStringAttribute(
        "sar", base::IntToString(video_info.pixel_width()) + ":" +
                   base::IntToString(video_info.pixel_height())));
  }

  if (set_width)
    RCHECK(SetIntegerAttribute("width", video_info.width()));
  if (set_height)
    RCHECK(SetIntegerAttribute("height", video_info.height()));
  if (set_frame_rate) {
    RCHECK(SetStringAttribute(
        "frameRate", base::IntToString(video_info.time_scale()) + "/" +
                         base::IntToString(video_info.frame_duration())));
  }

  if (video_info.has_playback_rate()) {
    RCHECK(SetStringAttribute("maxPlayoutRate",
                              base::IntToString(video_info.playback_rate())));
    // Since the trick play stream contains only key frames, there is no coding
    // dependency on the main stream. Simply set the codingDependency to false.
    // TODO(hmchen): propagate this attribute up to the AdaptationSet, since
    // all are set to false.
    RCHECK(SetStringAttribute("codingDependency", "false"));
  }
  return true;
}

bool RepresentationXmlNode::AddAudioInfo(const AudioInfo& audio_info) {
  return AddAudioChannelInfo(audio_info) &&
         AddAudioSamplingRateInfo(audio_info);
}

bool RepresentationXmlNode::AddVODOnlyInfo(const MediaInfo& media_info,
                                           bool use_segment_list,
                                           double target_segment_duration) {
  const bool use_single_segment_url_with_media =
      media_info.has_text_info() && media_info.has_presentation_time_offset();

  if (media_info.has_media_file_url() && !use_single_segment_url_with_media) {
    XmlNode base_url("BaseURL");
    base_url.SetContent(media_info.media_file_url());

    RCHECK(AddChild(std::move(base_url)));
  }

  const bool need_segment_base_or_list =
      use_segment_list || media_info.has_index_range() ||
      media_info.has_init_range() ||
      (media_info.has_reference_time_scale() && !media_info.has_text_info()) ||
      use_single_segment_url_with_media;

  if (!need_segment_base_or_list) {
    return true;
  }

  XmlNode child(use_segment_list || use_single_segment_url_with_media
                    ? "SegmentList"
                    : "SegmentBase");

  // Forcing SegmentList for longer audio causes sidx atom to not be
  // generated, therefore indexRange is not added to MPD if flag is set.
  if (media_info.has_index_range() && !use_segment_list) {
    RCHECK(child.SetStringAttribute("indexRange",
                                    RangeToString(media_info.index_range())));
  }

  if (media_info.has_reference_time_scale()) {
    RCHECK(child.SetIntegerAttribute("timescale",
                                     media_info.reference_time_scale()));

    if (use_segment_list && !use_single_segment_url_with_media) {
      const uint64_t duration_seconds = static_cast<uint64_t>(
          floor(target_segment_duration * media_info.reference_time_scale()));
      RCHECK(child.SetIntegerAttribute("duration", duration_seconds));
    }
  }

  if (media_info.has_presentation_time_offset()) {
    RCHECK(child.SetIntegerAttribute("presentationTimeOffset",
                                     media_info.presentation_time_offset()));
  }

  if (media_info.has_init_range()) {
    XmlNode initialization("Initialization");
    RCHECK(initialization.SetStringAttribute(
        "range", RangeToString(media_info.init_range())));

    RCHECK(child.AddChild(std::move(initialization)));
  }

  if (use_single_segment_url_with_media) {
    XmlNode media_url("SegmentURL");
    RCHECK(media_url.SetStringAttribute("media", media_info.media_file_url()));
    RCHECK(child.AddChild(std::move(media_url)));
  }

  // Since the SegmentURLs here do not have a @media element,
  // BaseURL element is mapped to the @media attribute.
  if (use_segment_list) {
    for (const Range& subsegment_range : media_info.subsegment_ranges()) {
      XmlNode subsegment("SegmentURL");
      RCHECK(subsegment.SetStringAttribute("mediaRange",
                                           RangeToString(subsegment_range)));

      RCHECK(child.AddChild(std::move(subsegment)));
    }
  }

  RCHECK(AddChild(std::move(child)));
  return true;
}

bool RepresentationXmlNode::AddLiveOnlyInfo(
    const MediaInfo& media_info,
    const std::list<SegmentInfo>& segment_infos,
    uint32_t start_number) {
  XmlNode segment_template("SegmentTemplate");
  if (media_info.has_reference_time_scale()) {
    RCHECK(segment_template.SetIntegerAttribute(
        "timescale", media_info.reference_time_scale()));
  }

  if (media_info.has_presentation_time_offset()) {
    RCHECK(segment_template.SetIntegerAttribute(
        "presentationTimeOffset", media_info.presentation_time_offset()));
  }

  if (media_info.has_init_segment_url()) {
    RCHECK(segment_template.SetStringAttribute("initialization",
                                               media_info.init_segment_url()));
  }

  if (media_info.has_segment_template_url()) {
    RCHECK(segment_template.SetStringAttribute(
        "media", media_info.segment_template_url()));
    RCHECK(segment_template.SetIntegerAttribute("startNumber", start_number));
  }

  if (!segment_infos.empty()) {
    // Don't use SegmentTimeline if all segments except the last one are of
    // the same duration.
    if (IsTimelineConstantDuration(segment_infos, start_number)) {
      RCHECK(segment_template.SetIntegerAttribute(
          "duration", segment_infos.front().duration));
      if (FLAGS_dash_add_last_segment_number_when_needed) {
        uint32_t last_segment_number = start_number - 1;
        for (const auto& segment_info_element : segment_infos)
          last_segment_number += segment_info_element.repeat + 1;

        RCHECK(AddSupplementalProperty(
            "http://dashif.org/guidelines/last-segment-number",
            std::to_string(last_segment_number)));
      }
    } else {
      XmlNode segment_timeline("SegmentTimeline");
      RCHECK(PopulateSegmentTimeline(segment_infos, &segment_timeline));
      RCHECK(segment_template.AddChild(std::move(segment_timeline)));
    }
  }
  return AddChild(std::move(segment_template));
}

bool RepresentationXmlNode::AddAudioChannelInfo(const AudioInfo& audio_info) {
  std::string audio_channel_config_scheme;
  std::string audio_channel_config_value;

  if (audio_info.codec() == kEC3Codec) {
    const auto& codec_data = audio_info.codec_specific_data();
    // Use MPEG scheme if the mpeg value is available and valid, fallback to
    // EC3 channel mapping otherwise.
    // See https://github.com/Dash-Industry-Forum/DASH-IF-IOP/issues/268
    const uint32_t ec3_channel_mpeg_value = codec_data.channel_mpeg_value();
    const uint32_t NO_MAPPING = 0xFFFFFFFF;
    if (ec3_channel_mpeg_value == NO_MAPPING) {
      // Convert EC3 channel map into string of hexadecimal digits. Spec: DASH-IF
      // Interoperability Points v3.0 9.2.1.2.
      const uint16_t ec3_channel_map =
        base::HostToNet16(codec_data.channel_mask());
      audio_channel_config_value =
        base::HexEncode(&ec3_channel_map, sizeof(ec3_channel_map));
      audio_channel_config_scheme =
        "tag:dolby.com,2014:dash:audio_channel_configuration:2011";
    } else {
      // Calculate EC3 channel configuration descriptor value with MPEG scheme.
      // Spec: ETSI TS 102 366 V1.4.1 Digital Audio Compression
      // (AC-3, Enhanced AC-3) I.1.2.
      audio_channel_config_value = base::UintToString(ec3_channel_mpeg_value);
      audio_channel_config_scheme = "urn:mpeg:mpegB:cicp:ChannelConfiguration";
    }
    bool ret = AddDescriptor("AudioChannelConfiguration",
                             audio_channel_config_scheme,
                             audio_channel_config_value);
    // Dolby Digital Plus JOC descriptor. Spec: ETSI TS 103 420 v1.2.1
    // Backwards-compatible object audio carriage using Enhanced AC-3 Standard
    // D.2.2.
    if (codec_data.ec3_joc_complexity() != 0) {
      std::string ec3_joc_complexity =
        base::UintToString(codec_data.ec3_joc_complexity());
      ret &= AddDescriptor("SupplementalProperty",
                           "tag:dolby.com,2018:dash:EC3_ExtensionType:2018",
                           "JOC");
      ret &= AddDescriptor("SupplementalProperty",
                           "tag:dolby.com,2018:dash:"
                           "EC3_ExtensionComplexityIndex:2018",
                           ec3_joc_complexity);
    }
    return ret;
  } else if (audio_info.codec().substr(0, 4) == kAC4Codec) {
    const auto& codec_data = audio_info.codec_specific_data();
    const bool ac4_ims_flag = codec_data.ac4_ims_flag();
    // Use MPEG scheme if the mpeg value is available and valid, fallback to
    // AC4 channel mask otherwise.
    // See https://github.com/Dash-Industry-Forum/DASH-IF-IOP/issues/268
    const uint32_t ac4_channel_mpeg_value = codec_data.channel_mpeg_value();
    const uint32_t NO_MAPPING = 0xFFFFFFFF;
    if (ac4_channel_mpeg_value == NO_MAPPING) {
      // Calculate AC-4 channel mask. Spec: ETSI TS 103 190-2 V1.2.1 Digital
      // Audio Compression (AC-4) Standard; Part 2: Immersive and personalized
      // audio G.3.1.
      const uint32_t ac4_channel_mask =
        base::HostToNet32(codec_data.channel_mask() << 8);
      audio_channel_config_value =
        base::HexEncode(&ac4_channel_mask, sizeof(ac4_channel_mask) - 1);
      // Note that the channel config schemes for EC-3 and AC-4 are different.
      // See https://github.com/Dash-Industry-Forum/DASH-IF-IOP/issues/268.
      audio_channel_config_scheme =
        "tag:dolby.com,2015:dash:audio_channel_configuration:2015";
    } else {
      // Calculate AC-4 channel configuration descriptor value with MPEG scheme.
      // Spec: ETSI TS 103 190-2 V1.2.1 Digital Audio Compression (AC-4) Standard;
      // Part 2: Immersive and personalized audio G.3.2.
      audio_channel_config_value = base::UintToString(ac4_channel_mpeg_value);
      audio_channel_config_scheme = "urn:mpeg:mpegB:cicp:ChannelConfiguration";
    }
    bool ret = AddDescriptor("AudioChannelConfiguration",
                             audio_channel_config_scheme,
                             audio_channel_config_value);
    if (ac4_ims_flag) {
      ret &= AddDescriptor("SupplementalProperty",
                           "tag:dolby.com,2016:dash:virtualized_content:2016",
                           "1");
    }
    return ret;
  } else {
    audio_channel_config_value = base::UintToString(audio_info.num_channels());
    audio_channel_config_scheme =
        "urn:mpeg:dash:23003:3:audio_channel_configuration:2011";
  }

  return AddDescriptor("AudioChannelConfiguration", audio_channel_config_scheme,
                       audio_channel_config_value);
}

// MPD expects one number for sampling frequency, or if it is a range it should
// be space separated.
bool RepresentationXmlNode::AddAudioSamplingRateInfo(
    const AudioInfo& audio_info) {
  return !audio_info.has_sampling_frequency() ||
         SetIntegerAttribute("audioSamplingRate",
                             audio_info.sampling_frequency());
}

}  // namespace xml
}  // namespace shaka
