// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MPD_PUBLIC_MPD_PARAMS_H_
#define PACKAGER_MPD_PUBLIC_MPD_PARAMS_H_

#include <string>
#include <vector>

namespace shaka {

/// DASH MPD related parameters.
struct MpdParams {
  /// MPD output file path.
  std::string mpd_output;
  /// BaseURLs for the MPD. The values will be added as <BaseURL> element(s)
  /// under the <MPD> element.
  std::vector<std::string> base_urls;
  /// Set MPD@minBufferTime attribute, which specifies, in seconds, a common
  /// duration used in the definition of the MPD representation data rate. A
  /// client can be assured of having enough data for continous playout
  /// providing playout begins at min_buffer_time after the first bit is
  /// received.
  double min_buffer_time = 2.0;
  /// Set MPD@minimumUpdatePeriod attribute, which indicates to the player how
  /// often to refresh the MPD in seconds. For dynamic MPD only.
  double minimum_update_period = 0;
  /// Set MPD@suggestedPresentationDelay attribute. For 'dynamic' media
  /// presentations, it specifies a delay, in seconds, to be added to the media
  /// presentation time. The attribute is not set if the value is 0; the client
  /// is expected to choose a suitable value in this case.
  static constexpr double kSuggestedPresentationDelayNotSet = 0;
  double suggested_presentation_delay = kSuggestedPresentationDelayNotSet;
  /// Set MPD@timeShiftBufferDepth attribute, which is the guaranteed duration
  /// of the time shifting buffer for 'dynamic' media presentations, in seconds.
  double time_shift_buffer_depth = 0;
  /// Segments outside the live window (defined by 'time_shift_buffer_depth'
  /// above) are automatically removed except for the most recent X segments
  /// defined by this parameter. This is needed to accommodate latencies in
  /// various stages of content serving pipeline, so that the segments stay
  /// accessible as they may still be accessed by the player. The segments are
  /// not removed if the value is zero.
  size_t preserved_segments_outside_live_window = 0;
  /// UTCTimings. For dynamic MPD only.
  struct UtcTiming {
    std::string scheme_id_uri;
    std::string value;
  };
  std::vector<UtcTiming> utc_timings;
  /// The tracks tagged with this language will have <Role ... value=\"main\" />
  /// in the manifest. This allows the player to choose the correct default
  /// language for the content.
  /// This applies to both audio and text tracks. The default language for text
  /// tracks can be overriden by 'default_text_language'.
  std::string default_language;
  /// Same as above, but this overrides the default language for text tracks,
  /// i.e. subtitles or close-captions.
  std::string default_text_language;
  /// Generate static MPD for live profile. Note that this flag has no effect
  /// for on-demand profile, in which case static MPD is always used.
  bool generate_static_live_mpd = false;
  /// Try to generate DASH-IF IOP compliant MPD.
  bool generate_dash_if_iop_compliant_mpd = true;
  /// For live profile only.
  /// If enabled, segments with close duration (i.e. with difference less than
  /// one sample) are considered to have the same duration. This enables
  /// MPD generator to generate less SegmentTimeline entries. If all segments
  /// are of the same duration except the last one, we will do further
  /// optimization to use SegmentTemplate@duration instead and omit
  /// SegmentTimeline completely.
  /// Ignored if $Time$ is used in segment template, since $Time$ requires
  /// accurate Segment Timeline.
  bool allow_approximate_segment_timeline = false;
  /// This is the target segment duration requested by the user. The actual
  /// segment duration may be different to the target segment duration.
  /// This parameter is included here to calculate the approximate
  /// SegmentTimeline if it is enabled. It will be populated from segment
  /// duration specified in ChunkingParams if not specified.
  double target_segment_duration = 0;
  /// If enabled, allow switching between different codecs, if they have the
  /// same language, media type (audio, video etc) and container type.
  bool allow_codec_switching = false;
  /// If enabled, PlayReady Object <mspr:pro> will be inserted into
  /// <ContentProtection ...> element alongside with <cenc:pssh>
  /// when using PlayReady protection system.
  bool include_mspr_pro = true;
  /// Session Based Description Document flags.
  
  struct SBD {
    std::string url_;
    std::string template_;
    std::vector<std::pair<std::string,std::string>> sbd_keys_;
  };
  std::vector<SBD> sbd_adaptation_set_video;
  std::vector<SBD> sbd_adaptation_set_audio;
  std::vector<SBD> sbd_adaptation_set_text;
  std::vector<SBD> sbd_adaptation_set_all;

};

}  // namespace shaka

#endif  // PACKAGER_MPD_PUBLIC_MPD_PARAMS_H_
