// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Defines Mpd flags.

#ifndef APP_MPD_FLAGS_H_
#define APP_MPD_FLAGS_H_

#include <gflags/gflags.h>

DECLARE_bool(generate_static_live_mpd);
DECLARE_bool(output_media_info);
DECLARE_string(mpd_output);
DECLARE_string(base_urls);
DECLARE_double(minimum_update_period);
DECLARE_double(min_buffer_time);
DECLARE_double(suggested_presentation_delay);
DECLARE_string(utc_timings);
DECLARE_bool(generate_dash_if_iop_compliant_mpd);
DECLARE_bool(allow_approximate_segment_timeline);
DECLARE_bool(allow_codec_switching);
DECLARE_bool(include_mspr_pro_for_playready);
//Session based urls
DECLARE_string(sbd_url_all);
DECLARE_string(sbd_template_all);
DECLARE_string(sbd_key_all);

DECLARE_string(sbd_url_video);
DECLARE_string(sbd_template_video);
DECLARE_string(sbd_key_video);

DECLARE_string(sbd_url_audio);
DECLARE_string(sbd_template_audio);
DECLARE_string(sbd_key_audio);

DECLARE_string(sbd_url_text);
DECLARE_string(sbd_template_text);
DECLARE_string(sbd_key_text);

#endif  // APP_MPD_FLAGS_H_
