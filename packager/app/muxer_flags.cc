// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Defines Muxer flags.

#include "packager/app/muxer_flags.h"

DEFINE_double(clear_lead,
              5.0f,
              "Clear lead in seconds if encryption is enabled. Note that we do "
              "not support partial segment encryption, so it is rounded up to "
              "full segments. Set it to a value smaller than segment_duration "
              "so only the first segment is in clear since the first segment "
              "could be smaller than segment_duration if there is small "
              "non-zero starting timestamp.");
DEFINE_double(segment_duration,
              6.0f,
              "Segment duration in seconds. If single_segment is specified, "
              "this parameter sets the duration of a subsegment; otherwise, "
              "this parameter sets the duration of a segment. Actual segment "
              "durations may not be exactly as requested.");
DEFINE_bool(segment_sap_aligned,
            true,
            "Force segments to begin with stream access points.");
DEFINE_double(fragment_duration,
              0,
              "Fragment duration in seconds. Should not be larger than "
              "the segment duration. Actual fragment durations may not be "
              "exactly as requested.");
DEFINE_bool(fragment_sap_aligned,
            true,
            "Force fragments to begin with stream access points. This flag "
            "implies segment_sap_aligned.");
DEFINE_bool(generate_sidx_in_media_segments,
            true,
            "Indicates whether to generate 'sidx' box in media segments. Note "
            "that it is required for DASH on-demand profile (not using segment "
            "template).");
DEFINE_string(temp_dir,
              "",
              "Specify a directory in which to store temporary (intermediate) "
              " files. Used only if single_segment=true.");
DEFINE_bool(mp4_include_pssh_in_stream,
            true,
            "MP4 only: include pssh in the encrypted stream.");
DEFINE_int32(transport_stream_timestamp_offset_ms,
             100,
             "A positive value, in milliseconds, by which output timestamps "
             "are offset to compensate for possible negative timestamps in the "
             "input. For example, timestamps from ISO-BMFF after adjusted by "
             "EditList could be negative. In transport streams, timestamps are "
             "not allowed to be less than zero.");
DEFINE_int32(mp4_initial_sequence_number,
             1,
             "Sets the initialize sequence number in the mp4 moof->mfhd. "
             "Otherwise defaults to 1.");