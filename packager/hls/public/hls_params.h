// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_HLS_PUBLIC_HLS_PARAMS_H_
#define PACKAGER_HLS_PUBLIC_HLS_PARAMS_H_

#include <string>

namespace shaka {

/// Defines the EXT-X-PLAYLIST-TYPE in the HLS specification. For
/// HlsPlaylistType of kLive, EXT-X-PLAYLIST-TYPE tag is omitted.
enum class HlsPlaylistType {
  kVod,
  kEvent,
  kLive,
};

/// HLS related parameters.
struct HlsParams {
  /// HLS playlist type. See HLS specification for details.
  HlsPlaylistType playlist_type = HlsPlaylistType::kVod;
  /// HLS master playlist output path.
  std::string master_playlist_output;
  /// The base URL for the Media Playlists and media files listed in the
  /// playlists. This is the prefix for the files.
  std::string base_url;
  /// Defines the live window, or the guaranteed duration of the time shifting
  /// buffer for 'live' playlists.
  double time_shift_buffer_depth = 0;
  /// Segments outside the live window (defined by 'time_shift_buffer_depth'
  /// above) are automatically removed except for the most recent X segments
  /// defined by this parameter. This is needed to accommodate latencies in
  /// various stages of content serving pipeline, so that the segments stay
  /// accessible as they may still be accessed by the player. The segments are
  /// not removed if the value is zero.
  size_t preserved_segments_outside_live_window = 0;
  /// Defines the key uri for "identity" and "com.apple.streamingkeydelivery"
  /// key formats. Ignored if the playlist is not encrypted or not using the
  /// above key formats.
  std::string key_uri;
  /// The renditions tagged with this language will have 'DEFAULT' set to 'YES'
  /// in 'EXT-X-MEDIA' tag. This allows the player to choose the correct default
  /// language for the content.
  /// This applies to both audio and text tracks. The default language for text
  /// tracks can be overriden by 'default_text_language'.
  std::string default_language;
  /// Same as above, but this overrides the default language for text tracks,
  /// i.e. subtitles or close-captions.
  std::string default_text_language;
  // Indicates that all media samples in the media segments can be decoded
  // without information from other segments.
  bool is_independent_segments;
  /// This is the target segment duration requested by the user. The actual
  /// segment duration may be different to the target segment duration. It will
  /// be populated from segment duration specified in ChunkingParams if not
  /// specified.
  double target_segment_duration = 0;
  /// Custom EXT-X-MEDIA-SEQUENCE value to allow continuous media playback
  /// across packager restarts. See #691 for details.
  uint32_t media_sequence_number = 0;
  /// Enable generation of EXT-X-PROGRAM-DATE-TIME tag.
  bool add_ext_x_program_date_time = false;
  uint32_t packaging_time_offset_ms = 0;
};

}  // namespace shaka

#endif  // PACKAGER_HLS_PUBLIC_HLS_PARAMS_H_
