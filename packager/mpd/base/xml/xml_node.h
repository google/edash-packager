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

#include <libxml/tree.h>
#include <stdint.h>

#include <list>
#include <set>

#include "packager/base/macros.h"
#include "packager/mpd/base/content_protection_element.h"
#include "packager/mpd/base/media_info.pb.h"
#include "packager/mpd/base/xml/scoped_xml_ptr.h"
#include "packager/mpd/base/mpd_options.h"

namespace shaka {

struct SegmentInfo;

namespace xml {

/// These classes are wrapper classes for XML elements for generating MPD.
/// None of the pointer parameters should be NULL. None of the methods are meant
/// to be overridden.
class XmlNode {
 public:
  /// Make an XML element.
  /// @param name is the name of the element, which should not be NULL.
  explicit XmlNode(const char* name);
  virtual ~XmlNode();

  /// Add a child element to this element.
  /// @param child is a xmlNode to add as a child for this element. Ownership
  ///        of the child node is transferred.
  /// @return true on success, false otherwise.
  bool AddChild(scoped_xml_ptr<xmlNode> child);

  /// Adds Elements to this node using the Element struct.
  bool AddElements(const std::vector<Element>& elements);

  /// Set a string attribute.
  /// @param attribute_name The name (lhs) of the attribute.
  /// @param attribute The value (rhs) of the attribute.
  void SetStringAttribute(const char* attribute_name,
                          const std::string& attribute);

  /// Sets an integer attribute.
  /// @param attribute_name The name (lhs) of the attribute.
  /// @param number The value (rhs) of the attribute.
  void SetIntegerAttribute(const char* attribute_name, uint64_t number);

  /// Set a floating point number attribute.
  /// @param attribute_name is the name of the attribute to set.
  /// @param number is the value (rhs) of the attribute.
  void SetFloatingPointAttribute(const char* attribute_name, double number);

  /// Sets 'id=@a id' attribute.
  /// @param id is the ID for this element.
  void SetId(uint32_t id);

  /// Set the contents of an XML element using a string.
  /// This cannot set child elements because <> will become &lt; and &rt;
  /// This should be used to set the text for the element, e.g. setting
  /// a URL for <BaseURL> element.
  /// @param content is a string containing the text-encoded child elements to
  ///        be added to the element.
  void SetContent(const std::string& content);

  /// @return namespaces used in the node and its descendents.
  std::set<std::string> ExtractReferencedNamespaces();

  /// Transfer the ownership of the xmlNodePtr. After calling this method, the
  /// behavior of any methods, except the destructor, is undefined.
  /// @return The resource of this object.
  scoped_xml_ptr<xmlNode> PassScopedPtr();

  /// Release the xmlNodePtr of this object. After calling this method, the
  /// behavior of any methods, except the destructor, is undefined.
  xmlNodePtr Release();

  /// @return Raw pointer to the element.
  xmlNodePtr GetRawPtr();

 private:
  scoped_xml_ptr<xmlNode> node_;

  DISALLOW_COPY_AND_ASSIGN(XmlNode);
};

/// This corresponds to RepresentationBaseType in MPD. RepresentationBaseType is
/// not a concrete element type so this should not get instantiated on its own.
/// AdaptationSet and Representation are subtypes of this.
class RepresentationBaseXmlNode : public XmlNode {
 public:
  ~RepresentationBaseXmlNode() override;
  bool AddContentProtectionElements(
      const std::list<ContentProtectionElement>& content_protection_elements);

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  void AddSupplementalProperty(const std::string& scheme_id_uri,
                               const std::string& value);

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  void AddEssentialProperty(const std::string& scheme_id_uri,
                            const std::string& value,
			    const MpdOptions& mpd_options);

 protected:
  explicit RepresentationBaseXmlNode(const char* name);

  /// Add a Descriptor.
  /// @param descriptor_name is the name of the descriptor.
  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  bool AddDescriptor(const std::string& descriptor_name,
                     const std::string& scheme_id_uri,
                     const std::string& value);

  bool AddDescriptorSBD(const std::string& descriptor_name,
                        const std::string& scheme_id_uri,
                        const std::string& value,
		        const MpdOptions& mpd_options);

  bool AddSBDInfo(const std::vector<MpdParams::SBD>& sbd_adaptation_set, 
		  const std::string& descriptor_name, 
		  const std::string& scheme_id_uri,
                  const std::string& value);
 private:
  bool AddContentProtectionElement(
      const ContentProtectionElement& content_protection_element);

  DISALLOW_COPY_AND_ASSIGN(RepresentationBaseXmlNode);
};

/// AdaptationSetType specified in MPD.
class AdaptationSetXmlNode : public RepresentationBaseXmlNode {
 public:
  AdaptationSetXmlNode();
  ~AdaptationSetXmlNode() override;

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  void AddAccessibilityElement(const std::string& scheme_id_uri,
                               const std::string& value);

  /// @param scheme_id_uri is content of the schemeIdUri attribute.
  /// @param value is the content of value attribute.
  void AddRoleElement(const std::string& scheme_id_uri,
                      const std::string& value);

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
                    bool set_frame_rate);

  /// Adds audio metadata to the MPD.
  /// @param audio_info constains the AudioInfos for a Representation.
  /// @return true if successfully set attributes and children elements (if
  ///         applicable), false otherwise.
  bool AddAudioInfo(const MediaInfo::AudioInfo& audio_info);

  /// Adds fields that are specific to VOD. This ignores @a media_info fields
  /// for Live.
  /// @param media_info is a MediaInfo with VOD information.
  /// @return true on success, false otherwise.
  bool AddVODOnlyInfo(const MediaInfo& media_info);

  /// @param segment_infos is a set of SegmentInfos. This method assumes that
  ///        SegmentInfos are sorted by its start time.
  bool AddLiveOnlyInfo(const MediaInfo& media_info,
                       const std::list<SegmentInfo>& segment_infos,
                       uint32_t start_number);

 private:
  // Add AudioChannelConfiguration element. Note that it is a required element
  // for audio Representations.
  bool AddAudioChannelInfo(const MediaInfo::AudioInfo& audio_info);

  // Add audioSamplingRate attribute to this element, if present.
  void AddAudioSamplingRateInfo(const MediaInfo::AudioInfo& audio_info);

  DISALLOW_COPY_AND_ASSIGN(RepresentationXmlNode);
};

}  // namespace xml
}  // namespace shaka
#endif  // MPD_BASE_XML_XML_NODE_H_
