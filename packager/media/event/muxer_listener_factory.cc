// Copyright 2017 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/event/muxer_listener_factory.h"

#include <list>

#include "packager/base/memory/ptr_util.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/hls/base/hls_notifier.h"
#include "packager/media/event/combined_muxer_listener.h"
#include "packager/media/event/hls_notify_muxer_listener.h"
#include "packager/media/event/mpd_notify_muxer_listener.h"
#include "packager/media/event/multi_codec_muxer_listener.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/event/vod_media_info_dump_muxer_listener.h"
#include "packager/mpd/base/mpd_notifier.h"

namespace shaka {
namespace media {
namespace {
const char kMediaInfoSuffix[] = ".media_info";

std::unique_ptr<MuxerListener> CreateMediaInfoDumpListenerInternal(
    const std::string& output,
    bool use_segment_list) {
  DCHECK(!output.empty());

  std::unique_ptr<MuxerListener> listener(
      new VodMediaInfoDumpMuxerListener(output + kMediaInfoSuffix, use_segment_list));
  return listener;
}

std::unique_ptr<MuxerListener> CreateMpdListenerInternal(
    const MuxerListenerFactory::StreamData& stream,
    MpdNotifier* notifier) {
  DCHECK(notifier);

  auto listener = base::MakeUnique<MpdNotifyMuxerListener>(notifier);
  listener->set_accessibilities(stream.dash_accessiblities);
  listener->set_roles(stream.dash_roles);
  listener->set_dash_label(stream.dash_label);
  return listener;
}

std::list<std::unique_ptr<MuxerListener>> CreateHlsListenersInternal(
    const MuxerListenerFactory::StreamData& stream,
    int stream_index,
    hls::HlsNotifier* notifier) {
  DCHECK(notifier);
  DCHECK_GE(stream_index, 0);

  std::string name = stream.hls_name;
  std::string playlist_name = stream.hls_playlist_name;

  const std::string& group_id = stream.hls_group_id;
  const std::string& iframe_playlist_name = stream.hls_iframe_playlist_name;
  const std::vector<std::string>& characteristics = stream.hls_characteristics;

  if (name.empty()) {
    name = base::StringPrintf("stream_%d", stream_index);
  }

  if (playlist_name.empty()) {
    playlist_name = base::StringPrintf("stream_%d.m3u8", stream_index);
  }

  const bool kIFramesOnly = true;
  std::list<std::unique_ptr<MuxerListener>> listeners;
  listeners.emplace_back(new HlsNotifyMuxerListener(
      playlist_name, !kIFramesOnly, name, group_id, characteristics, notifier));
  if (!iframe_playlist_name.empty()) {
    listeners.emplace_back(new HlsNotifyMuxerListener(
        iframe_playlist_name, kIFramesOnly, name, group_id,
        std::vector<std::string>(), notifier));
  }
  return listeners;
}
}  // namespace

MuxerListenerFactory::MuxerListenerFactory(bool output_media_info,
                                           bool use_segment_list,
                                           MpdNotifier* mpd_notifier,
                                           hls::HlsNotifier* hls_notifier)
    : output_media_info_(output_media_info),
      mpd_notifier_(mpd_notifier),
      hls_notifier_(hls_notifier),
      use_segment_list_(use_segment_list) {}

std::unique_ptr<MuxerListener> MuxerListenerFactory::CreateListener(
    const StreamData& stream) {
  const int stream_index = stream_index_++;

  // Use a MultiCodecMuxerListener to handle possible DolbyVision profile 8
  // stream which can be signalled as two different codecs.
  std::unique_ptr<MultiCodecMuxerListener> multi_codec_listener(
      new MultiCodecMuxerListener);
  // Creates two child MuxerListeners. Both are used if the stream is a
  // multi-codec stream (e.g. DolbyVision proifile 8); otherwise the second
  // child is ignored. Right now the only use case is DolbyVision profile 8
  // which contains two codecs.
  for (int i = 0; i < 2; i++) {
    std::unique_ptr<CombinedMuxerListener> combined_listener(
        new CombinedMuxerListener);
    if (output_media_info_) {
      combined_listener->AddListener(
          CreateMediaInfoDumpListenerInternal(stream.media_info_output,
                                              use_segment_list_));
    }

    if (mpd_notifier_ && !stream.hls_only) {
      combined_listener->AddListener(
          CreateMpdListenerInternal(stream, mpd_notifier_));
    }

    if (hls_notifier_ && !stream.dash_only) {
      for (auto& listener :
           CreateHlsListenersInternal(stream, stream_index, hls_notifier_)) {
        combined_listener->AddListener(std::move(listener));
      }
    }

    multi_codec_listener->AddListener(std::move(combined_listener));
  }

  return std::move(multi_codec_listener);
}

std::unique_ptr<MuxerListener> MuxerListenerFactory::CreateHlsListener(
    const StreamData& stream) {
  if (!hls_notifier_) {
    return nullptr;
  }

  const int stream_index = stream_index_++;
  return std::move(
      CreateHlsListenersInternal(stream, stream_index, hls_notifier_).front());
}

}  // namespace media
}  // namespace shaka
