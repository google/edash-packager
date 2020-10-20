// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Defines Mpd flags.

#include "packager/app/mpd_flags.h"

DEFINE_bool(generate_static_live_mpd,
            false,
            "Set to true to generate static mpd. If segment_template is "
            "specified in stream descriptors, shaka-packager generates dynamic "
            "mpd by default; if this flag is enabled, shaka-packager generates "
            "static mpd instead. Note that if segment_template is not "
            "specified, shaka-packager always generates static mpd regardless "
            "of the value of this flag.");
DEFINE_bool(output_media_info,
            false,
            "Create a human readable format of MediaInfo. The output file name "
            "will be the name specified by output flag, suffixed with "
            "'.media_info'.");
DEFINE_string(mpd_output, "", "MPD output file name.");
DEFINE_string(base_urls,
              "",
              "Comma separated BaseURLs for the MPD. The values will be added "
              "as <BaseURL> element(s) immediately under the <MPD> element.");
DEFINE_double(min_buffer_time,
              2.0,
              "Specifies, in seconds, a common duration used in the definition "
              "of the MPD Representation data rate.");
DEFINE_double(minimum_update_period,
              5.0,
              "Indicates to the player how often to refresh the media "
              "presentation description in seconds. This value is used for "
              "dynamic MPD only.");
DEFINE_double(suggested_presentation_delay,
              0.0,
              "Specifies a delay, in seconds, to be added to the media "
              "presentation time. This value is used for dynamic MPD only.");
DEFINE_string(utc_timings,
              "",
              "Comma separated UTCTiming schemeIdUri and value pairs for the "
              "MPD. This value is used for dynamic MPD only.");
DEFINE_bool(generate_dash_if_iop_compliant_mpd,
            true,
            "Try to generate DASH-IF IOP compliant MPD. This is best effort "
            "and does not guarantee compliance.");
DEFINE_bool(
    allow_approximate_segment_timeline,
    false,
    "For live profile only. "
    "If enabled, segments with close duration (i.e. with difference less than "
    "one sample) are considered to have the same duration. This enables MPD "
    "generator to generate less SegmentTimeline entries. If all segments are "
    "of the same duration except the last one, we will do further optimization "
    "to use SegmentTemplate@duration instead and omit SegmentTimeline "
    "completely."
    "Ignored if $Time$ is used in segment template, since $Time$ requires "
    "accurate Segment Timeline.");
DEFINE_bool(allow_codec_switching,
            false,
            "If enabled, allow adaptive switching between different codecs, "
            "if they have the same language, media type (audio, video etc) and "
            "container type.");
DEFINE_bool(include_mspr_pro_for_playready,
            true,
            "If enabled, PlayReady Object <mspr:pro> will be inserted into "
            "<ContentProtection ...> element alongside with <cenc:pssh> "
            "when using PlayReady protection system.");


DEFINE_string(sbd_url_all, "", "Comma separated URLs of sbd document.Add to all adaptation sets.");
DEFINE_string(sbd_template_all, "", "Comma separated template used to create URL query.");
DEFINE_string(sbd_key_all, "", "Comma separated SBD keys to fetch from sbd client.");


DEFINE_string(sbd_url_video, "", "Comma separated URL of sbd document. Add to all video adaptation sets");
DEFINE_string(sbd_template_video, "", "Comma separated template used to create URL query.");
DEFINE_string(sbd_key_video, "", "SBD keys to fetch from sbd client.");


DEFINE_string(sbd_url_audio, "", "Comma separated URL of sbd document.Add to all adaptation sets.");
DEFINE_string(sbd_template_audio, "", "Comma separated Template used to create URL query.");
DEFINE_string(sbd_key_audio, "", "Comma separated SBD keys to fetch from sbd client.");


DEFINE_string(sbd_url_text, "", "Comma separated URL of sbd document. Add to all text adapatation sets.");
DEFINE_string(sbd_template_text, "", "Comma separated Template used to create URL query.");
DEFINE_string(sbd_key_text, "", "Comma separated SBD key-value pairs.");
