// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Classes to wrap XML operations. XmlNode is a generic wrapper class for
// XmlNode in libxml2. There are also MPD XML specific classes as well.

#ifndef MPD_BASE_XML_XML_NODE_H_
#define MPD_BASE_XML_XML_NODE_H_

#include <stdint.h>

#include <list>
#include <set>
#include <string>
#include <vector>

#include "packager/base/compiler_specific.h"
#include "packager/base/macros.h"
#include "packager/mpd/base/content_protection_element.h"
#include "packager/mpd/base/media_info.pb.h"

typedef struct _xmlNode xmlNode;

namespace shaka {

class MpdBuilder;
struct SegmentInfo;

namespace xml {
class XmlNode;
}  // namespace xml

// Defined in tests under mpd/test/xml_compare.h
bool XmlEqual(const std::string& xml1, const xml::XmlNode& xml2);

namespace xml {

/// These classes are wrapper classes for XML elements for generating MPD.
/// None of the pointer parameters should be NULL. None of the methods are meant
/// to be overridden.
class XmlNode {
 public:
  /// Make an XML element.
  /// @param name is the name of the element, which should not be NULL.
  explicit XmlNode(const std::string& name);
  XmlNode(XmlNode&&);
  virtual ~XmlNode();

  XmlNode& operator=(XmlNode&&);

  /// Add a child element to this element.
  /// @param child is an XmlNode to add as a child for this element.
  /// @return true on success, false otherwise.
  bool AddChild(XmlNode child) WARN_UNUSED_RESULT;

  /// Adds Elements to this node using the Element struct.
  bool AddElements(const std::vector<Element>& elements) WARN_UNUSED_RESULT;

  /// Set a string attribute.
  /// @param attribute_name The name (lhs) of the attribute.
  /// @param attribute The value (rhs) of the attribute.
  bool SetStringAttribute(const std::string& attribute_name,
                          const std::string& attribute) WARN_UNUSED_RESULT;

  /// Sets an integer attribute.
  /// @param attribute_name The name (lhs) of the attribute.
  /// @param number The value (rhs) of the attribute.
  bool SetIntegerAttribute(const std::string& attribute_name,
                           uint64_t number) WARN_UNUSED_RESULT;

  /// Set a floating point number attribute.
  /// @param attribute_name is the name of the attribute to set.
  /// @param number is the value (rhs) of the attribute.
  bool SetFloatingPointAttribute(const std::string& attribute_name,
                                 double number) WARN_UNUSED_RESULT;

  /// Sets 'id=@a id' attribute.
  /// @param id is the ID for this element.
  bool SetId(uint32_t id) WARN_UNUSED_RESULT;

  /// Similar to SetContent, but appends to the end of existing content.
  void AddContent(const std::string& content);

  /// Set the contents of an XML element using a string.
  /// This cannot set child elements because <> will become &lt; and &rt;
  /// This should be used to set the text for the element, e.g. setting
  /// a URL for <BaseURL> element.
  /// @param content is a string containing the text-encoded child elements to
  ///        be added to the element.
  void SetContent(const std::string& content);

  /// @return namespaces used in the node and its descendents.
  std::set<std::string> ExtractReferencedNamespaces() const;

  /// @param comment The body of a comment to add to the top of the XML.
  /// @return A string containing the XML.
  std::string ToString(const std::string& comment) const;

  /// Gets the attribute with the given name.
  /// @param name The name of the attribute to get.
  /// @param value [OUT] where to put the resulting value.
  /// @return True if the attribute exists, false if not.
  bool GetAttribute(const std::string& name, std::string* value) const;

 private:
  friend bool shaka::XmlEqual(const std::string& xml1,
                              const xml::XmlNode& xml2);
  xmlNode* GetRawPtr() const;

  // Don't use xmlNode directly so we don't have to forward-declare a bunch of
  // libxml types to define the scoped_xml_ptr type.  This allows us to only
  // include libxml headers in a few source files.
  class Impl;
  std::unique_ptr<Impl> impl_;

  DISALLOW_COPY_AND_ASSIGN(XmlNode);
};

/// This corresponds to RepresentationBaseType in MPD. RepresentationBaseType is
/// not a concrete element type so this should not get instantiated on its own.
/// AdaptationSet and Representation are subtypes of this.
class RepresentationBaseXmlNode : public XmlNode {
 public:
  ~RepresentationBaseXmlNode() override;
  bool AddContentProtectionElements(
      const std::list<ContentProtectionElement>& content_protection_elements)
      WARN_UNUSED_RESULT;

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  bool AddSupplementalProperty(const std::string& scheme_id_uri,
                               const std::string& value) WARN_UNUSED_RESULT;

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  bool AddEssentialProperty(const std::string& scheme_id_uri,
                            const std::string& value) WARN_UNUSED_RESULT;

 protected:
  explicit RepresentationBaseXmlNode(const std::string& name);

  /// Add a Descriptor.
  /// @param descriptor_name is the name of the descriptor.
  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  bool AddDescriptor(const std::string& descriptor_name,
                     const std::string& scheme_id_uri,
                     const std::string& value) WARN_UNUSED_RESULT;

 private:
  bool AddContentProtectionElement(
      const ContentProtectionElement& content_protection_element)
      WARN_UNUSED_RESULT;

  DISALLOW_COPY_AND_ASSIGN(RepresentationBaseXmlNode);
};

/// AdaptationSetType specified in MPD.
class AdaptationSetXmlNode : public RepresentationBaseXmlNode {
 public:
  AdaptationSetXmlNode();
  ~AdaptationSetXmlNode() override;

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  bool AddAccessibilityElement(const std::string& scheme_id_uri,
                               const std::string& value) WARN_UNUSED_RESULT;

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  bool AddRoleElement(const std::string& scheme_id_uri,
                      const std::string& value) WARN_UNUSED_RESULT;

  /// @param value is element's content.
  bool AddLabelElement(const std::string& value) WARN_UNUSED_RESULT;

 private:
  DISALLOW_COPY_AND_ASSIGN(AdaptationSetXmlNode);
};

/// RepresentationType in MPD.
class RepresentationXmlNode : public RepresentationBaseXmlNode {
 public:
  RepresentationXmlNode();
  ~RepresentationXmlNode() override;

  /// Adds video metadata to the MPD.
  /// @param video_info constains the VideoInfo for a Representation.
  /// @param set_width is a flag for setting the width attribute.
  /// @param set_height is a flag for setting the height attribute.
  /// @param set_frame_rate is a flag for setting the frameRate attribute.
  /// @return true if successfully set attributes and children elements (if
  ///         applicable), false otherwise.
  bool AddVideoInfo(const MediaInfo::VideoInfo& video_info,
                    bool set_width,
                    bool set_height,
                    bool set_frame_rate) WARN_UNUSED_RESULT;

  /// Adds audio metadata to the MPD.
  /// @param audio_info constains the AudioInfos for a Representation.
  /// @return true if successfully set attributes and children elements (if
  ///         applicable), false otherwise.
  bool AddAudioInfo(const MediaInfo::AudioInfo& audio_info) WARN_UNUSED_RESULT;

  /// Adds fields that are specific to VOD. This ignores @a media_info fields
  /// for Live.
  /// @param media_info is a MediaInfo with VOD information.
  /// @param use_segment_list is a param that instructs the xml writer to
  ///        use SegmentList instead of SegmentBase.
  /// @param target_segment_duration is a param that specifies the target
  //         duration of media segments. This is only used when use_segment_list
  //         is true.
  /// @return true on success, false otherwise.
  bool AddVODOnlyInfo(const MediaInfo& media_info,
                      bool use_segment_list,
                      double target_segment_duration) WARN_UNUSED_RESULT;

  /// @param segment_infos is a set of SegmentInfos. This method assumes that
  ///        SegmentInfos are sorted by its start time.
  bool AddLiveOnlyInfo(const MediaInfo& media_info,
                       const std::list<SegmentInfo>& segment_infos,
                       uint32_t start_number) WARN_UNUSED_RESULT;

 private:
  // Add AudioChannelConfiguration element. Note that it is a required element
  // for audio Representations.
  bool AddAudioChannelInfo(const MediaInfo::AudioInfo& audio_info)
      WARN_UNUSED_RESULT;

  // Add audioSamplingRate attribute to this element, if present.
  bool AddAudioSamplingRateInfo(const MediaInfo::AudioInfo& audio_info)
      WARN_UNUSED_RESULT;

  DISALLOW_COPY_AND_ASSIGN(RepresentationXmlNode);
};

}  // namespace xml
}  // namespace shaka
#endif  // MPD_BASE_XML_XML_NODE_H_
