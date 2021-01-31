// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_APP_HLS_FLAGS_H_
#define PACKAGER_APP_HLS_FLAGS_H_

#include <gflags/gflags.h>

DECLARE_string(hls_master_playlist_output);
DECLARE_string(hls_base_url);
DECLARE_string(hls_key_uri);
DECLARE_string(hls_playlist_type);
DECLARE_int32(hls_media_sequence_number);
DECLARE_int32(hls_ext_x_program_date_time);

#endif  // PACKAGER_APP_HLS_FLAGS_H_
