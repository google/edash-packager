// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_EVENT_MUXER_LISTENER_FACTORY_H_
#define PACKAGER_MEDIA_EVENT_MUXER_LISTENER_FACTORY_H_

#include <memory>
#include <string>
#include <vector>

namespace shaka {
class MpdNotifier;

namespace hls {
class HlsNotifier;
}

namespace media {
class MuxerListener;

/// Factory class for creating MuxerListeners. Will produce a single muxer
/// listener that will wrap the various muxer listeners that the factory
/// supports. Currently the factory supports:
///    - Media Info Dump
///    - HLS
///    - MPD
///
/// The listeners that will be combined will be based on the parameters given
/// when constructing the factory.
class MuxerListenerFactory {
 public:
  /// The subset of data from a stream descriptor that the muxer listener
  /// factory needs in order to create listeners for the stream.
  struct StreamData {
    // The stream's output destination. Will only be used if the factory is
    // told to output media info.
    std::string media_info_output;

    // HLS specific values needed to write to HLS manifests. Will only be used
    // if an HlsNotifier is given to the factory.
    std::string hls_group_id;
    std::string hls_name;
    std::string hls_playlist_name;
    std::string hls_iframe_playlist_name;
    std::vector<std::string> hls_characteristics;
    bool hls_only = false;

    // DASH specific values needed to write DASH mpd. Will only be used if an
    // MpdNotifier is given to the factory.
    std::vector<std::string> dash_accessiblities;
    std::vector<std::string> dash_roles;
    bool dash_only = false;
    std::string dash_label;
  };

  /// Create a new muxer listener.
  /// @param output_media_info must be true for the combined listener to include
  ///        a media info dump listener.
  /// @param use_segment_list is set when mpd_notifier_ is null and
  ///        --output_media_info is set. If mpd_notifer is non-null, this value
  ///        is the same as mpd_notifier->use_segment_list().
  /// @param mpd_notifer must be non-null for the combined listener to include a
  ///        mpd listener.
  /// @param hls_notifier must be non-null for the combined listener to include
  ///        an HLS listener.
  MuxerListenerFactory(bool output_media_info,
                       bool use_segment_list,
                       MpdNotifier* mpd_notifier,
                       hls::HlsNotifier* hls_notifier);

  /// Create a listener for a stream.
  std::unique_ptr<MuxerListener> CreateListener(const StreamData& stream);

  /// Create an HLS listener if possible. If it is not possible to
  /// create an HLS listener, this method will return null.
  std::unique_ptr<MuxerListener> CreateHlsListener(const StreamData& stream);

 private:
  MuxerListenerFactory(const MuxerListenerFactory&) = delete;
  MuxerListenerFactory operator=(const MuxerListenerFactory&) = delete;

  bool output_media_info_;
  MpdNotifier* mpd_notifier_;
  hls::HlsNotifier* hls_notifier_;

  /// This is set when mpd_notifier_ is NULL and --output_media_info is set.
  bool use_segment_list_;

  // A counter to track which stream we are on.
  int stream_index_ = 0;
};
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_EVENT_MUXER_LISTENER_FACTORY_H_
