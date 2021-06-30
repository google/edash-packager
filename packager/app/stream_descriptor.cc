// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/app/stream_descriptor.h"

#include "packager/base/logging.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/string_split.h"

namespace shaka {

namespace {

enum FieldType {
  kUnknownField = 0,
  kStreamSelectorField,
  kInputField,
  kOutputField,
  kSegmentTemplateField,
  kBandwidthField,
  kLanguageField,
  kCcIndexField,
  kOutputFormatField,
  kHlsNameField,
  kHlsGroupIdField,
  kHlsPlaylistNameField,
  kHlsIframePlaylistNameField,
  kTrickPlayFactorField,
  kSkipEncryptionField,
  kDrmStreamLabelField,
  kHlsCharacteristicsField,
  kDashAccessiblitiesField,
  kDashRolesField,
  kDashOnlyField,
  kHlsOnlyField,
  kDashLabelField,
};

struct FieldNameToTypeMapping {
  const char* field_name;
  FieldType field_type;
};

const FieldNameToTypeMapping kFieldNameTypeMappings[] = {
    {"stream_selector", kStreamSelectorField},
    {"stream", kStreamSelectorField},
    {"input", kInputField},
    {"in", kInputField},
    {"output", kOutputField},
    {"out", kOutputField},
    {"init_segment", kOutputField},
    {"segment_template", kSegmentTemplateField},
    {"template", kSegmentTemplateField},
    {"bandwidth", kBandwidthField},
    {"bw", kBandwidthField},
    {"bitrate", kBandwidthField},
    {"language", kLanguageField},
    {"lang", kLanguageField},
    {"cc_index", kCcIndexField},
    {"output_format", kOutputFormatField},
    {"format", kOutputFormatField},
    {"hls_name", kHlsNameField},
    {"hls_group_id", kHlsGroupIdField},
    {"playlist_name", kHlsPlaylistNameField},
    {"iframe_playlist_name", kHlsIframePlaylistNameField},
    {"trick_play_factor", kTrickPlayFactorField},
    {"tpf", kTrickPlayFactorField},
    {"skip_encryption", kSkipEncryptionField},
    {"drm_stream_label", kDrmStreamLabelField},
    {"drm_label", kDrmStreamLabelField},
    {"hls_characteristics", kHlsCharacteristicsField},
    {"characteristics", kHlsCharacteristicsField},
    {"charcs", kHlsCharacteristicsField},
    {"dash_accessibilities", kDashAccessiblitiesField},
    {"dash_accessibility", kDashAccessiblitiesField},
    {"accessibilities", kDashAccessiblitiesField},
    {"accessibility", kDashAccessiblitiesField},
    {"dash_roles", kDashRolesField},
    {"dash_role", kDashRolesField},
    {"roles", kDashRolesField},
    {"role", kDashRolesField},
    {"dash_only", kDashOnlyField},
    {"hls_only", kHlsOnlyField},
    {"dash_label", kDashLabelField},
};

FieldType GetFieldType(const std::string& field_name) {
  for (size_t idx = 0; idx < arraysize(kFieldNameTypeMappings); ++idx) {
    if (field_name == kFieldNameTypeMappings[idx].field_name)
      return kFieldNameTypeMappings[idx].field_type;
  }
  return kUnknownField;
}

}  // anonymous namespace

base::Optional<StreamDescriptor> ParseStreamDescriptor(
    const std::string& descriptor_string) {
  StreamDescriptor descriptor;

  // Split descriptor string into name/value pairs.
  base::StringPairs pairs;
  if (!base::SplitStringIntoKeyValuePairs(descriptor_string, '=', ',',
                                          &pairs)) {
    LOG(ERROR) << "Invalid stream descriptors name/value pairs: "
               << descriptor_string;
    return base::nullopt;
  }
  for (base::StringPairs::const_iterator iter = pairs.begin();
       iter != pairs.end(); ++iter) {
    switch (GetFieldType(iter->first)) {
      case kStreamSelectorField:
        descriptor.stream_selector = iter->second;
        break;
      case kInputField:
        descriptor.input = iter->second;
        break;
      case kOutputField:
        descriptor.output = iter->second;
        break;
      case kSegmentTemplateField:
        descriptor.segment_template = iter->second;
        break;
      case kBandwidthField: {
        unsigned bw;
        if (!base::StringToUint(iter->second, &bw)) {
          LOG(ERROR) << "Non-numeric bandwidth specified.";
          return base::nullopt;
        }
        descriptor.bandwidth = bw;
        break;
      }
      case kLanguageField: {
        descriptor.language = iter->second;
        break;
      }
      case kCcIndexField: {
        unsigned index;
        if (!base::StringToUint(iter->second, &index)) {
          LOG(ERROR) << "Non-numeric cc_index specified.";
          return base::nullopt;
        }
        descriptor.cc_index = index;
        break;
      }
      case kOutputFormatField: {
        descriptor.output_format = iter->second;
        break;
      }
      case kHlsNameField: {
        descriptor.hls_name = iter->second;
        break;
      }
      case kHlsGroupIdField: {
        descriptor.hls_group_id = iter->second;
        break;
      }
      case kHlsPlaylistNameField: {
        descriptor.hls_playlist_name = iter->second;
        break;
      }
      case kHlsIframePlaylistNameField: {
        descriptor.hls_iframe_playlist_name = iter->second;
        break;
      }
      case kTrickPlayFactorField: {
        unsigned factor;
        if (!base::StringToUint(iter->second, &factor)) {
          LOG(ERROR) << "Non-numeric trick play factor " << iter->second
                     << " specified.";
          return base::nullopt;
        }
        if (factor == 0) {
          LOG(ERROR) << "Stream trick_play_factor should be > 0.";
          return base::nullopt;
        }
        descriptor.trick_play_factor = factor;
        break;
      }
      case kSkipEncryptionField: {
        unsigned skip_encryption_value;
        if (!base::StringToUint(iter->second, &skip_encryption_value)) {
          LOG(ERROR) << "Non-numeric option for skip encryption field "
                        "specified (" << iter->second << ").";
          return base::nullopt;
        }
        if (skip_encryption_value > 1) {
          LOG(ERROR) << "skip_encryption should be either 0 or 1.";
          return base::nullopt;
        }

        descriptor.skip_encryption = skip_encryption_value > 0;
        break;
      }
      case kDrmStreamLabelField:
        descriptor.drm_label = iter->second;
        break;
      case kHlsCharacteristicsField:
        descriptor.hls_characteristics =
            base::SplitString(iter->second, ";:", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY);
        break;
      case kDashAccessiblitiesField:
        descriptor.dash_accessiblities =
            base::SplitString(iter->second, ";", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY);
        for (const std::string& accessibility :
             descriptor.dash_accessiblities) {
          size_t pos = accessibility.find('=');
          if (pos == std::string::npos) {
            LOG(ERROR)
                << "Accessibility should be in scheme=value format, but seeing "
                << accessibility;
            return base::nullopt;
          }
        }
        break;
      case kDashRolesField:
        descriptor.dash_roles =
            base::SplitString(iter->second, ";", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY);
        break;
      case kDashOnlyField:
        unsigned dash_only_value;
        if (!base::StringToUint(iter->second, &dash_only_value)) {
          LOG(ERROR) << "Non-numeric option for dash_only field "
                        "specified (" << iter->second << ").";
          return base::nullopt;
        }
        if (dash_only_value > 1) {
          LOG(ERROR) << "dash_only should be either 0 or 1.";
          return base::nullopt;
        }
        descriptor.dash_only = dash_only_value > 0;
        break;
      case kHlsOnlyField:
        unsigned hls_only_value;
        if (!base::StringToUint(iter->second, &hls_only_value)) {
          LOG(ERROR) << "Non-numeric option for hls_only field "
                        "specified (" << iter->second << ").";
          return base::nullopt;
        }
        if (hls_only_value > 1) {
          LOG(ERROR) << "hls_only should be either 0 or 1.";
          return base::nullopt;
        }
        descriptor.hls_only = hls_only_value > 0;
        break;
      case kDashLabelField:
        descriptor.dash_label = iter->second;
        break;
      default:
        LOG(ERROR) << "Unknown field in stream descriptor (\"" << iter->first
                   << "\").";
        return base::nullopt;
    }
  }
  return descriptor;
}

}  // namespace shaka
