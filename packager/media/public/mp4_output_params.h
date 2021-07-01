// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_PUBLIC_MP4_OUTPUT_PARAMS_H_
#define PACKAGER_MEDIA_PUBLIC_MP4_OUTPUT_PARAMS_H_

namespace shaka {

/// MP4 (ISO-BMFF) output related parameters.
struct Mp4OutputParams {
  /// Include pssh in the encrypted stream. CMAF and DASH-IF recommends carrying
  /// license acquisition information in the manifest and not duplicate the
  /// information in the stream. (This is not a hard requirement so we are still
  /// CMAF compatible even if pssh is included in the stream.)
  bool include_pssh_in_stream = true;
  /// Indicates whether a 'sidx' box should be generated in the media segments.
  /// Note that it is required by spec if segment_template contains $Times$
  /// specifier.
  bool generate_sidx_in_media_segments = true;
  int initial_sequence_number = 1;
};

}  // namespace shaka

#endif  // PACKAGER_MEDIA_PUBLIC_MP4_OUTPUT_PARAMS_H_
