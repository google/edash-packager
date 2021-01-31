// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/hls/base/simple_hls_notifier.h"

#include <gflags/gflags.h>
#include <cmath>

#include "packager/base/base64.h"
#include "packager/base/files/file_path.h"
#include "packager/base/logging.h"
#include "packager/base/optional.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/hls/base/media_playlist.h"
#include "packager/media/base/protection_system_ids.h"
#include "packager/media/base/protection_system_specific_info.h"
#include "packager/media/base/proto_json_util.h"
#include "packager/media/base/widevine_pssh_data.pb.h"

DEFINE_bool(enable_legacy_widevine_hls_signaling,
            false,
            "Specifies whether Legacy Widevine HLS, i.e. v1 is signalled in "
            "the media playlist. Applies to Widevine protection system in HLS "
            "with SAMPLE-AES only.");

namespace shaka {

using base::FilePath;

namespace hls {

namespace {

const char kUriBase64Prefix[] = "data:text/plain;base64,";
const char kUriFairPlayPrefix[] = "skd://";
const char kWidevineDashIfIopUUID[] =
    "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed";

bool IsWidevineSystemId(const std::vector<uint8_t>& system_id) {
  return system_id.size() == arraysize(media::kWidevineSystemId) &&
         std::equal(system_id.begin(), system_id.end(),
                    media::kWidevineSystemId);
}

bool IsCommonSystemId(const std::vector<uint8_t>& system_id) {
  return system_id.size() == arraysize(media::kCommonSystemId) &&
         std::equal(system_id.begin(), system_id.end(), media::kCommonSystemId);
}

bool IsFairPlaySystemId(const std::vector<uint8_t>& system_id) {
  return system_id.size() == arraysize(media::kFairPlaySystemId) &&
         std::equal(system_id.begin(), system_id.end(),
                    media::kFairPlaySystemId);
}

std::string Base64EncodeData(const std::string& prefix,
                             const std::string& data) {
    std::string data_base64;
    base::Base64Encode(data, &data_base64);
    return prefix + data_base64;
}

std::string VectorToString(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

// TODO(rkuroiwa): Dedup these with the functions in MpdBuilder.
// If |media_path| is contained in |parent_path|, then
//   Strips the common path and keep only the relative part of |media_path|.
//   e.g. if |parent_path| is /some/parent/ and
//           |media_path| is /some/parent/abc/child/item.ext,
//        abc/child/item.ext is returned.
// else
//   Returns |media_path|.
// The path separator of the output is also changed to "/" if it is not.
std::string MakePathRelative(const std::string& media_path,
                             const FilePath& parent_path) {
  FilePath relative_path;
  const FilePath child_path = FilePath::FromUTF8Unsafe(media_path);
  const bool is_child =
      parent_path.AppendRelativePath(child_path, &relative_path);
  if (!is_child)
    relative_path = child_path;
  return relative_path.NormalizePathSeparatorsTo('/').AsUTF8Unsafe();
}

// Segment URL is relative to either output directory or the directory
// containing the media playlist depends on whether base_url is set.
std::string GenerateSegmentUrl(const std::string& segment_name,
                               const std::string& base_url,
                               const std::string& output_dir,
                               const std::string& playlist_file_name) {
  FilePath output_path = FilePath::FromUTF8Unsafe(output_dir);
  if (!base_url.empty()) {
    // Media segment URL is base_url + segment path relative to output
    // directory.
    return base_url + MakePathRelative(segment_name, output_path);
  }
  // Media segment URL is segment path relative to the directory containing the
  // playlist.
  const FilePath playlist_dir =
      output_path.Append(FilePath::FromUTF8Unsafe(playlist_file_name))
          .DirName()
          .AsEndingWithSeparator();
  return MakePathRelative(segment_name, playlist_dir);
}

MediaInfo MakeMediaInfoPathsRelativeToPlaylist(
    const MediaInfo& media_info,
    const std::string& base_url,
    const std::string& output_dir,
    const std::string& playlist_name) {
  MediaInfo media_info_copy = media_info;
  if (media_info_copy.has_init_segment_name()) {
    media_info_copy.set_init_segment_url(
        GenerateSegmentUrl(media_info_copy.init_segment_name(), base_url,
                           output_dir, playlist_name));
  }
  if (media_info_copy.has_media_file_name()) {
    media_info_copy.set_media_file_url(
        GenerateSegmentUrl(media_info_copy.media_file_name(), base_url,
                           output_dir, playlist_name));
  }
  if (media_info_copy.has_segment_template()) {
    media_info_copy.set_segment_template_url(
        GenerateSegmentUrl(media_info_copy.segment_template(), base_url,
                           output_dir, playlist_name));
  }
  return media_info_copy;
}

bool WidevinePsshToJson(const std::vector<uint8_t>& pssh_box,
                        const std::vector<uint8_t>& key_id,
                        std::string* pssh_json) {
  std::unique_ptr<media::PsshBoxBuilder> pssh_builder =
      media::PsshBoxBuilder::ParseFromBox(pssh_box.data(), pssh_box.size());
  if (!pssh_builder) {
    LOG(ERROR) << "Failed to parse PSSH box.";
    return false;
  }

  media::WidevinePsshData pssh_proto;
  if (!pssh_proto.ParseFromArray(pssh_builder->pssh_data().data(),
                                 pssh_builder->pssh_data().size())) {
    LOG(ERROR) << "Failed to parse protection_system_specific_data.";
    return false;
  }

  media::WidevineHeader widevine_header;

  if (pssh_proto.has_provider()) {
    widevine_header.set_provider(pssh_proto.provider());
  } else {
    LOG(WARNING) << "Missing provider in Widevine PSSH. The content may not "
                    "play in some devices.";
  }

  if (pssh_proto.has_content_id()) {
    widevine_header.set_content_id(pssh_proto.content_id());
  } else {
    LOG(WARNING) << "Missing content_id in Widevine PSSH. The content may not "
                    "play in some devices.";
  }

  // Place the current |key_id| to the front and converts all key_id to hex
  // format.
  widevine_header.add_key_ids(base::HexEncode(key_id.data(), key_id.size()));
  for (const std::string& key_id_in_pssh : pssh_proto.key_id()) {
    const std::string key_id_hex =
        base::HexEncode(key_id_in_pssh.data(), key_id_in_pssh.size());
    if (widevine_header.key_ids(0) != key_id_hex)
      widevine_header.add_key_ids(key_id_hex);
  }

  *pssh_json = media::MessageToJsonString(widevine_header);
  return true;
}

base::Optional<MediaPlaylist::EncryptionMethod> StringToEncryptionMethod(
    const std::string& method) {
  if (method == "cenc") {
    return MediaPlaylist::EncryptionMethod::kSampleAesCenc;
  }
  if (method == "cbcs") {
    return MediaPlaylist::EncryptionMethod::kSampleAes;
  }
  if (method == "cbca") {
    // cbca is a place holder for sample aes.
    return MediaPlaylist::EncryptionMethod::kSampleAes;
  }
  return base::nullopt;
}

void NotifyEncryptionToMediaPlaylist(
    MediaPlaylist::EncryptionMethod encryption_method,
    const std::string& uri,
    const std::vector<uint8_t>& key_id,
    const std::vector<uint8_t>& iv,
    const std::string& key_format,
    const std::string& key_format_version,
    MediaPlaylist* media_playlist) {
  std::string iv_string;
  if (!iv.empty()) {
    iv_string = "0x" + base::HexEncode(iv.data(), iv.size());
  }
  std::string key_id_string;
  if (!key_id.empty()) {
    key_id_string = "0x" + base::HexEncode(key_id.data(), key_id.size());
  }

  media_playlist->AddEncryptionInfo(
      encryption_method,
      uri, key_id_string, iv_string,
      key_format, key_format_version);
}

// Creates JSON format and the format similar to MPD.
bool HandleWidevineKeyFormats(
    MediaPlaylist::EncryptionMethod encryption_method,
    const std::vector<uint8_t>& key_id,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& protection_system_specific_data,
    MediaPlaylist* media_playlist) {
  if (FLAGS_enable_legacy_widevine_hls_signaling &&
      encryption_method == MediaPlaylist::EncryptionMethod::kSampleAes) {
    // This format allows SAMPLE-AES only.
    std::string key_uri_data;
    if (!WidevinePsshToJson(protection_system_specific_data, key_id,
                            &key_uri_data)) {
      return false;
    }
    std::string key_uri_data_base64 =
        Base64EncodeData(kUriBase64Prefix, key_uri_data);
    NotifyEncryptionToMediaPlaylist(encryption_method, key_uri_data_base64,
                                    std::vector<uint8_t>(), iv, "com.widevine",
                                    "1", media_playlist);
  }

  std::string pssh_as_string(
      reinterpret_cast<const char*>(protection_system_specific_data.data()),
      protection_system_specific_data.size());
  std::string key_uri_data_base64 =
      Base64EncodeData(kUriBase64Prefix, pssh_as_string);
  NotifyEncryptionToMediaPlaylist(encryption_method, key_uri_data_base64,
                                  key_id, iv, kWidevineDashIfIopUUID, "1",
                                  media_playlist);
  return true;
}

bool WriteMediaPlaylist(const std::string& output_dir,
                        MediaPlaylist* playlist) {
  std::string file_path =
      FilePath::FromUTF8Unsafe(output_dir)
          .Append(FilePath::FromUTF8Unsafe(playlist->file_name()))
          .AsUTF8Unsafe();
  if (!playlist->WriteToFile(file_path)) {
    LOG(ERROR) << "Failed to write playlist " << file_path;
    return false;
  }
  return true;
}

}  // namespace

MediaPlaylistFactory::~MediaPlaylistFactory() {}

std::unique_ptr<MediaPlaylist> MediaPlaylistFactory::Create(
    const HlsParams& hls_params,
    const std::string& file_name,
    const std::string& name,
    const std::string& group_id) {
  return std::unique_ptr<MediaPlaylist>(
      new MediaPlaylist(hls_params, file_name, name, group_id));
}

SimpleHlsNotifier::SimpleHlsNotifier(const HlsParams& hls_params)
    : HlsNotifier(hls_params),
      media_playlist_factory_(new MediaPlaylistFactory()) {
  const base::FilePath master_playlist_path(
      base::FilePath::FromUTF8Unsafe(hls_params.master_playlist_output));
  master_playlist_dir_ = master_playlist_path.DirName().AsUTF8Unsafe();
  const std::string& default_audio_langauge = hls_params.default_language;
  const std::string& default_text_language =
      hls_params.default_text_language.empty()
          ? hls_params.default_language
          : hls_params.default_text_language;
  master_playlist_.reset(
      new MasterPlaylist(master_playlist_path.BaseName().AsUTF8Unsafe(),
                         default_audio_langauge, default_text_language, 
                         hls_params.is_independent_segments));
}

SimpleHlsNotifier::~SimpleHlsNotifier() {}

bool SimpleHlsNotifier::Init() {
  return true;
}

bool SimpleHlsNotifier::NotifyNewStream(const MediaInfo& media_info,
                                        const std::string& playlist_name,
                                        const std::string& name,
                                        const std::string& group_id,
                                        uint32_t* stream_id) {
  DCHECK(stream_id);

  const std::string relative_playlist_path = MakePathRelative(
      playlist_name, FilePath::FromUTF8Unsafe(master_playlist_dir_));

  std::unique_ptr<MediaPlaylist> media_playlist =
      media_playlist_factory_->Create(hls_params(), relative_playlist_path,
                                      name, group_id);
  MediaInfo adjusted_media_info = MakeMediaInfoPathsRelativeToPlaylist(
      media_info, hls_params().base_url, master_playlist_dir_,
      media_playlist->file_name());
  if (!media_playlist->SetMediaInfo(adjusted_media_info)) {
    LOG(ERROR) << "Failed to set media info for playlist " << playlist_name;
    return false;
  }

  MediaPlaylist::EncryptionMethod encryption_method =
      MediaPlaylist::EncryptionMethod::kNone;
  if (media_info.protected_content().has_protection_scheme()) {
    const std::string& protection_scheme =
        media_info.protected_content().protection_scheme();
    base::Optional<MediaPlaylist::EncryptionMethod> enc_method =
        StringToEncryptionMethod(protection_scheme);
    if (!enc_method) {
      LOG(ERROR) << "Failed to recognize protection scheme "
                 << protection_scheme;
      return false;
    }
    encryption_method = enc_method.value();
  }

  base::AutoLock auto_lock(lock_);
  *stream_id = sequence_number_++;
  media_playlists_.push_back(media_playlist.get());
  stream_map_[*stream_id].reset(
      new StreamEntry{std::move(media_playlist), encryption_method});
  return true;
}

bool SimpleHlsNotifier::NotifySampleDuration(uint32_t stream_id,
                                             uint32_t sample_duration) {
  base::AutoLock auto_lock(lock_);
  auto stream_iterator = stream_map_.find(stream_id);
  if (stream_iterator == stream_map_.end()) {
    LOG(ERROR) << "Cannot find stream with ID: " << stream_id;
    return false;
  }
  auto& media_playlist = stream_iterator->second->media_playlist;
  media_playlist->SetSampleDuration(sample_duration);
  return true;
}

bool SimpleHlsNotifier::NotifyNewSegment(uint32_t stream_id,
                                         const std::string& segment_name,
                                         uint64_t start_time,
                                         uint64_t duration,
                                         uint64_t start_byte_offset,
                                         uint64_t size) {
  base::AutoLock auto_lock(lock_);
  auto stream_iterator = stream_map_.find(stream_id);
  if (stream_iterator == stream_map_.end()) {
    LOG(ERROR) << "Cannot find stream with ID: " << stream_id;
    return false;
  }
  auto& media_playlist = stream_iterator->second->media_playlist;
  const std::string& segment_url =
      GenerateSegmentUrl(segment_name, hls_params().base_url,
                         master_playlist_dir_, media_playlist->file_name());

  // Check if we need to set reference_time_
  // This need to be adjusted on discontinuity
  if (hls_params().add_ext_x_program_date_time) {
    if (reference_time_.is_null() || start_time < media_playlist->LastSegmentStartTime()) {
      base::Time adjusted_time = base::Time::Now()
          + base::TimeDelta::FromMilliseconds(hls_params().packaging_time_offset_ms);
      if (reference_time_.is_null()) {
        LOG(INFO)
            << "Init reference time: " << adjusted_time;
      } else {
        LOG(WARNING)
            << "Reset reference time on discontinuity: " << adjusted_time
            << " prev: " << media_playlist->LastSegmentStartTime() << " cur: " << start_time;
      }
      reference_time_ = adjusted_time;
    }
  }

  media_playlist->AddSegment(segment_url, start_time, duration,
                             start_byte_offset, size, reference_time_);

  // Update target duration.
  uint32_t longest_segment_duration =
      static_cast<uint32_t>(ceil(media_playlist->GetLongestSegmentDuration()));
  bool target_duration_updated = false;
  if (longest_segment_duration > target_duration_) {
    target_duration_ = longest_segment_duration;
    target_duration_updated = true;
  }

  // Update the playlists when there is new segments in live mode.
  if (hls_params().playlist_type == HlsPlaylistType::kLive ||
      hls_params().playlist_type == HlsPlaylistType::kEvent) {
    // Update all playlists if target duration is updated.
    if (target_duration_updated) {
      for (MediaPlaylist* playlist : media_playlists_) {
        playlist->SetTargetDuration(target_duration_);
        if (!WriteMediaPlaylist(master_playlist_dir_, playlist))
          return false;
      }
    } else {
      if (!WriteMediaPlaylist(master_playlist_dir_, media_playlist.get()))
        return false;
    }
    if (!master_playlist_->WriteMasterPlaylist(
            hls_params().base_url, master_playlist_dir_, media_playlists_)) {
      LOG(ERROR) << "Failed to write master playlist.";
      return false;
    }
  }
  return true;
}

bool SimpleHlsNotifier::NotifyKeyFrame(uint32_t stream_id,
                                       uint64_t timestamp,
                                       uint64_t start_byte_offset,
                                       uint64_t size) {
  base::AutoLock auto_lock(lock_);
  auto stream_iterator = stream_map_.find(stream_id);
  if (stream_iterator == stream_map_.end()) {
    LOG(ERROR) << "Cannot find stream with ID: " << stream_id;
    return false;
  }
  auto& media_playlist = stream_iterator->second->media_playlist;
  media_playlist->AddKeyFrame(timestamp, start_byte_offset, size);
  return true;
}

bool SimpleHlsNotifier::NotifyCueEvent(uint32_t stream_id, uint64_t timestamp) {
  base::AutoLock auto_lock(lock_);
  auto stream_iterator = stream_map_.find(stream_id);
  if (stream_iterator == stream_map_.end()) {
    LOG(ERROR) << "Cannot find stream with ID: " << stream_id;
    return false;
  }
  auto& media_playlist = stream_iterator->second->media_playlist;
  media_playlist->AddPlacementOpportunity();
  return true;
}

bool SimpleHlsNotifier::NotifyEncryptionUpdate(
    uint32_t stream_id,
    const std::vector<uint8_t>& key_id,
    const std::vector<uint8_t>& system_id,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& protection_system_specific_data) {
  base::AutoLock auto_lock(lock_);
  auto stream_iterator = stream_map_.find(stream_id);
  if (stream_iterator == stream_map_.end()) {
    LOG(ERROR) << "Cannot find stream with ID: " << stream_id;
    return false;
  }

  std::unique_ptr<MediaPlaylist>& media_playlist =
      stream_iterator->second->media_playlist;
  const MediaPlaylist::EncryptionMethod encryption_method =
      stream_iterator->second->encryption_method;
  LOG_IF(WARNING, encryption_method == MediaPlaylist::EncryptionMethod::kNone)
      << "Got encryption notification but the encryption method is NONE";
  if (IsWidevineSystemId(system_id)) {
    return HandleWidevineKeyFormats(encryption_method,
                                    key_id, iv, protection_system_specific_data,
                                    media_playlist.get());
  }

  // Key Id does not need to be specified with "identity" and "sdk".
  const std::vector<uint8_t> empty_key_id;

  if (IsCommonSystemId(system_id)) {
    std::string key_uri = hls_params().key_uri;
    if (key_uri.empty()) {
      // Use key_id as the key_uri. The player needs to have custom logic to
      // convert it to the actual key uri.
      std::string key_uri_data = VectorToString(key_id);
      key_uri = Base64EncodeData(kUriBase64Prefix, key_uri_data);
    }
    NotifyEncryptionToMediaPlaylist(encryption_method, key_uri, empty_key_id,
                                    iv, "identity", "", media_playlist.get());
    return true;
  }
  if (IsFairPlaySystemId(system_id)) {
    std::string key_uri = hls_params().key_uri;
    if (key_uri.empty()) {
      // Use key_id as the key_uri. The player needs to have custom logic to
      // convert it to the actual key uri.
      std::string key_uri_data = VectorToString(key_id);
      key_uri = Base64EncodeData(kUriFairPlayPrefix, key_uri_data);
    }

    // FairPlay defines IV to be carried with the key, not the playlist.
    const std::vector<uint8_t> empty_iv;
    NotifyEncryptionToMediaPlaylist(encryption_method, key_uri, empty_key_id,
                                    empty_iv, "com.apple.streamingkeydelivery",
                                    "1", media_playlist.get());
    return true;
  }

  LOG(WARNING) << "HLS: Ignore unknown or unsupported system ID: "
               << base::HexEncode(system_id.data(), system_id.size());
  return true;
}

bool SimpleHlsNotifier::Flush() {
  base::AutoLock auto_lock(lock_);
  for (MediaPlaylist* playlist : media_playlists_) {
    playlist->SetTargetDuration(target_duration_);
    if (!WriteMediaPlaylist(master_playlist_dir_, playlist))
      return false;
  }
  if (!master_playlist_->WriteMasterPlaylist(
          hls_params().base_url, master_playlist_dir_, media_playlists_)) {
    LOG(ERROR) << "Failed to write master playlist.";
    return false;
  }
  return true;
}

}  // namespace hls
}  // namespace shaka
