// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Muxer utility functions.

#ifndef PACKAGER_MEDIA_BASE_MUXER_UTIL_H_
#define PACKAGER_MEDIA_BASE_MUXER_UTIL_H_

#include <stdint.h>

#include "packager/status.h"

namespace shaka {
namespace media {

class StreamInfo;

/// Validates the segment template against segment URL construction rule
/// specified in ISO/IEC 23009-1:2012 5.3.9.4.4.
/// @param segment_template is the template to be validated.
/// @return OK if the segment template complies with
//          ISO/IEC 23009-1:2012 5.3.9.4.4.
Status ValidateSegmentTemplate(const std::string& segment_template);

/// Build the segment name from provided input.
/// @param segment_template is the segment template pattern, which should
///        comply with ISO/IEC 23009-1:2012 5.3.9.4.4.
/// @param segment_start_time specifies the segment start time.
/// @param segment_number specifies the segment number.
/// @param bandwidth represents the bit rate, in bits/sec, of the stream.
/// @return The segment name with identifier substituted.
std::string GetSegmentName(const std::string& segment_template,
                           uint64_t segment_start_time,
                           uint32_t segment_number,
                           uint32_t bandwidth);

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_BASE_MUXER_UTIL_H_
