// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/hls/base/media_playlist.h"

#include <inttypes.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "packager/app/hls_flags.h"
#include "packager/base/logging.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/strings/stringprintf.h"
#include "packager/base/time/time.h"
#include "packager/file/file.h"
#include "packager/hls/base/tag.h"
#include "packager/media/base/language_utils.h"
#include "packager/media/base/muxer_util.h"
#include "packager/version/version.h"

namespace shaka {
namespace hls {

namespace {
uint32_t GetTimeScale(const MediaInfo& media_info) {
  if (media_info.has_reference_time_scale())
    return media_info.reference_time_scale();

  if (media_info.has_video_info())
    return media_info.video_info().time_scale();

  if (media_info.has_audio_info())
    return media_info.audio_info().time_scale();
  return 0u;
}

std::string AdjustVideoCodec(const std::string& codec) {
  // Apple does not like video formats with the parameter sets stored in the
  // samples. It also fails mediastreamvalidator checks and some Apple devices /
  // platforms refused to play.
  // See https://apple.co/30n90DC 1.10 and
  // https://github.com/google/shaka-packager/issues/587#issuecomment-489182182.
  // Replaced with the corresponding formats with the parameter sets stored in
  // the sample descriptions instead.
  std::string adjusted_codec = codec;
  std::string fourcc = codec.substr(0, 4);
  if (fourcc == "avc3")
    adjusted_codec = "avc1" + codec.substr(4);
  else if (fourcc == "hev1")
    adjusted_codec = "hvc1" + codec.substr(4);
  else if (fourcc == "dvhe")
    adjusted_codec = "dvh1" + codec.substr(4);
  if (adjusted_codec != codec) {
    VLOG(1) << "Adusting video codec string from " << codec << " to "
            << adjusted_codec;
  }
  return adjusted_codec;
}

// Duplicated from MpdUtils because:
// 1. MpdUtils header depends on libxml header, which is not in the deps here
// 2. GetLanguage depends on MediaInfo from packager/mpd/
// 3. Moving GetLanguage to LanguageUtils would create a a media => mpd dep.
// TODO(https://github.com/google/shaka-packager/issues/373): Fix this
// dependency situation and factor this out to a common location.
std::string GetLanguage(const MediaInfo& media_info) {
  std::string lang;
  if (media_info.has_audio_info()) {
    lang = media_info.audio_info().language();
  } else if (media_info.has_text_info()) {
    lang = media_info.text_info().language();
  }
  return LanguageToShortestForm(lang);
}

void AppendExtXMap(const MediaInfo& media_info, std::string* out) {
  if (media_info.has_init_segment_url()) {
    Tag tag("#EXT-X-MAP", out);
    tag.AddQuotedString("URI", media_info.init_segment_url().data());
    out->append("\n");
  } else if (media_info.has_media_file_url() && media_info.has_init_range()) {
    // It only makes sense for single segment media to have EXT-X-MAP if
    // there is init_range.
    Tag tag("#EXT-X-MAP", out);
    tag.AddQuotedString("URI", media_info.media_file_url().data());

    if (media_info.has_init_range()) {
      const uint64_t begin = media_info.init_range().begin();
      const uint64_t end = media_info.init_range().end();
      const uint64_t length = end - begin + 1;

      tag.AddQuotedNumberPair("BYTERANGE", length, '@', begin);
    }

    out->append("\n");
  } else {
    // This media info does not need an ext-x-map tag.
  }
}

std::string CreatePlaylistHeader(
    const MediaInfo& media_info,
    uint32_t target_duration,
    HlsPlaylistType type,
    MediaPlaylist::MediaPlaylistStreamType stream_type,
    uint32_t media_sequence_number,
    int discontinuity_sequence_number) {
  const std::string version = GetPackagerVersion();
  std::string version_line;
  if (!version.empty()) {
    version_line =
        base::StringPrintf("## Generated with %s version %s\n",
                           GetPackagerProjectUrl().c_str(), version.c_str());
  }

  // 6 is required for EXT-X-MAP without EXT-X-I-FRAMES-ONLY.
  std::string header = base::StringPrintf(
      "#EXTM3U\n"
      "#EXT-X-VERSION:6\n"
      "%s"
      "#EXT-X-TARGETDURATION:%d\n",
      version_line.c_str(), target_duration);

  switch (type) {
    case HlsPlaylistType::kVod:
      header += "#EXT-X-PLAYLIST-TYPE:VOD\n";
      break;
    case HlsPlaylistType::kEvent:
      header += "#EXT-X-PLAYLIST-TYPE:EVENT\n";
      break;
    case HlsPlaylistType::kLive:
      if (media_sequence_number > 0) {
        base::StringAppendF(&header, "#EXT-X-MEDIA-SEQUENCE:%d\n",
                            media_sequence_number);
      }
      if (discontinuity_sequence_number > 0) {
        base::StringAppendF(&header, "#EXT-X-DISCONTINUITY-SEQUENCE:%d\n",
                            discontinuity_sequence_number);
      }
      break;
    default:
      NOTREACHED() << "Unexpected MediaPlaylistType " << static_cast<int>(type);
  }
  if (stream_type ==
      MediaPlaylist::MediaPlaylistStreamType::kVideoIFramesOnly) {
    base::StringAppendF(&header, "#EXT-X-I-FRAMES-ONLY\n");
  }

  // Put EXT-X-MAP at the end since the rest of the playlist is about the
  // segment and key info.
  AppendExtXMap(media_info, &header);

  return header;
}

class SegmentInfoEntry : public HlsEntry {
 public:
  // If |use_byte_range| true then this will append EXT-X-BYTERANGE
  // after EXTINF.
  // It uses |previous_segment_end_offset| to determine if it has to also
  // specify the start byte offset in the tag.
  // |start_time| is in timescale.
  // |duration_seconds| is duration in seconds.
  SegmentInfoEntry(const std::string& file_name,
                   int64_t start_time,
                   double duration_seconds,
                   bool use_byte_range,
                   uint64_t start_byte_offset,
                   uint64_t segment_file_size,
                   uint64_t previous_segment_end_offset,
                   const base::Time wall_time);

  std::string ToString() override;
  int64_t start_time() const { return start_time_; }
  double duration_seconds() const { return duration_seconds_; }
  void set_duration_seconds(double duration_seconds) {
    duration_seconds_ = duration_seconds;
  }

  const base::Time wall_time() {
    return wall_time_;
  }

 private:
  SegmentInfoEntry(const SegmentInfoEntry&) = delete;
  SegmentInfoEntry& operator=(const SegmentInfoEntry&) = delete;

  const std::string file_name_;
  const int64_t start_time_;
  double duration_seconds_;
  const bool use_byte_range_;
  const uint64_t start_byte_offset_;
  const uint64_t segment_file_size_;
  const uint64_t previous_segment_end_offset_;
  const base::Time wall_time_;
};

SegmentInfoEntry::SegmentInfoEntry(const std::string& file_name,
                                   int64_t start_time,
                                   double duration_seconds,
                                   bool use_byte_range,
                                   uint64_t start_byte_offset,
                                   uint64_t segment_file_size,
                                   uint64_t previous_segment_end_offset,
                                   const base::Time wall_time)
    : HlsEntry(HlsEntry::EntryType::kExtInf),
      file_name_(file_name),
      start_time_(start_time),
      duration_seconds_(duration_seconds),
      use_byte_range_(use_byte_range),
      start_byte_offset_(start_byte_offset),
      segment_file_size_(segment_file_size),
      previous_segment_end_offset_(previous_segment_end_offset),
      wall_time_(wall_time) {}

std::string SegmentInfoEntry::ToString() {
  std::string result = base::StringPrintf("#EXTINF:%.3f,", duration_seconds_);

  if (use_byte_range_) {
    base::StringAppendF(&result, "\n#EXT-X-BYTERANGE:%" PRIu64,
                        segment_file_size_);
    if (previous_segment_end_offset_ + 1 != start_byte_offset_) {
      base::StringAppendF(&result, "@%" PRIu64, start_byte_offset_);
    }
  }

  base::StringAppendF(&result, "\n%s", file_name_.c_str());

  return result;
}

class EncryptionInfoEntry : public HlsEntry {
 public:
  EncryptionInfoEntry(MediaPlaylist::EncryptionMethod method,
                      const std::string& url,
                      const std::string& key_id,
                      const std::string& iv,
                      const std::string& key_format,
                      const std::string& key_format_versions);

  std::string ToString() override;

 private:
  EncryptionInfoEntry(const EncryptionInfoEntry&) = delete;
  EncryptionInfoEntry& operator=(const EncryptionInfoEntry&) = delete;

  const MediaPlaylist::EncryptionMethod method_;
  const std::string url_;
  const std::string key_id_;
  const std::string iv_;
  const std::string key_format_;
  const std::string key_format_versions_;
};

EncryptionInfoEntry::EncryptionInfoEntry(MediaPlaylist::EncryptionMethod method,
                                         const std::string& url,
                                         const std::string& key_id,
                                         const std::string& iv,
                                         const std::string& key_format,
                                         const std::string& key_format_versions)
    : HlsEntry(HlsEntry::EntryType::kExtKey),
      method_(method),
      url_(url),
      key_id_(key_id),
      iv_(iv),
      key_format_(key_format),
      key_format_versions_(key_format_versions) {}

std::string EncryptionInfoEntry::ToString() {
  std::string tag_string;
  Tag tag("#EXT-X-KEY", &tag_string);

  if (method_ == MediaPlaylist::EncryptionMethod::kSampleAes) {
    tag.AddString("METHOD", "SAMPLE-AES");
  } else if (method_ == MediaPlaylist::EncryptionMethod::kAes128) {
    tag.AddString("METHOD", "AES-128");
  } else if (method_ == MediaPlaylist::EncryptionMethod::kSampleAesCenc) {
    tag.AddString("METHOD", "SAMPLE-AES-CTR");
  } else {
    DCHECK(method_ == MediaPlaylist::EncryptionMethod::kNone);
    tag.AddString("METHOD", "NONE");
  }

  tag.AddQuotedString("URI", url_);

  if (!key_id_.empty()) {
    tag.AddString("KEYID", key_id_);
  }
  if (!iv_.empty()) {
    tag.AddString("IV", iv_);
  }
  if (!key_format_versions_.empty()) {
    tag.AddQuotedString("KEYFORMATVERSIONS", key_format_versions_);
  }
  if (!key_format_.empty()) {
    tag.AddQuotedString("KEYFORMAT", key_format_);
  }

  return tag_string;
}

class DiscontinuityEntry : public HlsEntry {
 public:
  DiscontinuityEntry();

  std::string ToString() override;

 private:
  DiscontinuityEntry(const DiscontinuityEntry&) = delete;
  DiscontinuityEntry& operator=(const DiscontinuityEntry&) = delete;
};

DiscontinuityEntry::DiscontinuityEntry()
    : HlsEntry(HlsEntry::EntryType::kExtDiscontinuity) {}

std::string DiscontinuityEntry::ToString() {
  return "#EXT-X-DISCONTINUITY";
}

class PlacementOpportunityEntry : public HlsEntry {
 public:
  PlacementOpportunityEntry();

  std::string ToString() override;

 private:
  PlacementOpportunityEntry(const PlacementOpportunityEntry&) = delete;
  PlacementOpportunityEntry& operator=(const PlacementOpportunityEntry&) =
      delete;
};

PlacementOpportunityEntry::PlacementOpportunityEntry()
    : HlsEntry(HlsEntry::EntryType::kExtPlacementOpportunity) {}

std::string PlacementOpportunityEntry::ToString() {
  return "#EXT-X-PLACEMENT-OPPORTUNITY";
}

}  // namespace

HlsEntry::HlsEntry(HlsEntry::EntryType type) : type_(type) {}
HlsEntry::~HlsEntry() {}

MediaPlaylist::MediaPlaylist(const HlsParams& hls_params,
                             const std::string& file_name,
                             const std::string& name,
                             const std::string& group_id)
    : hls_params_(hls_params),
      file_name_(file_name),
      name_(name),
      group_id_(group_id),
      media_sequence_number_(hls_params_.media_sequence_number) {
        // When there's a forced media_sequence_number, start with discontinuity
        if (media_sequence_number_ > 0)
          entries_.emplace_back(new DiscontinuityEntry());
      }

MediaPlaylist::~MediaPlaylist() {}

void MediaPlaylist::SetStreamTypeForTesting(
    MediaPlaylistStreamType stream_type) {
  stream_type_ = stream_type;
}

void MediaPlaylist::SetCodecForTesting(const std::string& codec) {
  codec_ = codec;
}

void MediaPlaylist::SetLanguageForTesting(const std::string& language) {
  language_ = language;
}

void MediaPlaylist::SetCharacteristicsForTesting(
    const std::vector<std::string>& characteristics) {
  characteristics_ = characteristics;
}

bool MediaPlaylist::SetMediaInfo(const MediaInfo& media_info) {
  const uint32_t time_scale = GetTimeScale(media_info);
  if (time_scale == 0) {
    LOG(ERROR) << "MediaInfo does not contain a valid timescale.";
    return false;
  }

  if (media_info.has_video_info()) {
    stream_type_ = MediaPlaylistStreamType::kVideo;
    codec_ = AdjustVideoCodec(media_info.video_info().codec());
  } else if (media_info.has_audio_info()) {
    stream_type_ = MediaPlaylistStreamType::kAudio;
    codec_ = media_info.audio_info().codec();
  } else {
    stream_type_ = MediaPlaylistStreamType::kSubtitle;
    codec_ = media_info.text_info().codec();
  }

  time_scale_ = time_scale;
  media_info_ = media_info;
  language_ = GetLanguage(media_info);
  use_byte_range_ = !media_info_.has_segment_template_url() &&
                    media_info_.container_type() != MediaInfo::CONTAINER_TEXT;
  characteristics_ =
      std::vector<std::string>(media_info_.hls_characteristics().begin(),
                               media_info_.hls_characteristics().end());

  return true;
}

void MediaPlaylist::SetSampleDuration(uint32_t sample_duration) {
  if (media_info_.has_video_info())
    media_info_.mutable_video_info()->set_frame_duration(sample_duration);
}

void MediaPlaylist::AddSegment(const std::string& file_name,
                               int64_t start_time,
                               int64_t duration,
                               uint64_t start_byte_offset,
                               uint64_t size,
                               const base::Time reference_time) {
  if (stream_type_ == MediaPlaylistStreamType::kVideoIFramesOnly) {
    if (key_frames_.empty())
      return;

    AdjustLastSegmentInfoEntryDuration(key_frames_.front().timestamp);

    for (auto iter = key_frames_.begin(); iter != key_frames_.end(); ++iter) {
      // Last entry duration may be adjusted later when the next iframe becomes
      // available.
      const int64_t next_timestamp = std::next(iter) == key_frames_.end()
                                         ? (start_time + duration)
                                         : std::next(iter)->timestamp;
      AddSegmentInfoEntry(file_name, iter->timestamp,
                          next_timestamp - iter->timestamp,
                          iter->start_byte_offset, iter->size, reference_time);
    }
    key_frames_.clear();
    return;
  }
  return AddSegmentInfoEntry(file_name, start_time, duration, start_byte_offset,
                             size, reference_time);
}

void MediaPlaylist::AddKeyFrame(int64_t timestamp,
                                uint64_t start_byte_offset,
                                uint64_t size) {
  if (stream_type_ != MediaPlaylistStreamType::kVideoIFramesOnly) {
    if (stream_type_ != MediaPlaylistStreamType::kVideo) {
      LOG(WARNING)
          << "I-Frames Only playlist applies to video renditions only.";
      return;
    }
    stream_type_ = MediaPlaylistStreamType::kVideoIFramesOnly;
    use_byte_range_ = true;
  }
  key_frames_.push_back({timestamp, start_byte_offset, size});
}

void MediaPlaylist::AddEncryptionInfo(MediaPlaylist::EncryptionMethod method,
                                      const std::string& url,
                                      const std::string& key_id,
                                      const std::string& iv,
                                      const std::string& key_format,
                                      const std::string& key_format_versions) {
  if (!inserted_discontinuity_tag_) {
    // Insert discontinuity tag only for the first EXT-X-KEY, only if there
    // are non-encrypted media segments.
    if (!entries_.empty())
      entries_.emplace_back(new DiscontinuityEntry());
    inserted_discontinuity_tag_ = true;
  }
  entries_.emplace_back(new EncryptionInfoEntry(
      method, url, key_id, iv, key_format, key_format_versions));
}

void MediaPlaylist::AddPlacementOpportunity() {
  entries_.emplace_back(new PlacementOpportunityEntry());
}

bool MediaPlaylist::WriteToFile(const std::string& file_path) {
  if (!target_duration_set_) {
    SetTargetDuration(ceil(GetLongestSegmentDuration()));
  }

  std::string content = CreatePlaylistHeader(
      media_info_, target_duration_, hls_params_.playlist_type, stream_type_,
      media_sequence_number_, discontinuity_sequence_number_);

  bool need_ext_date_time = hls_params_.add_ext_x_program_date_time;

  for (const auto& entry : entries_) {
    switch (entry->type()) {
    case HlsEntry::EntryType::kExtInf:
      if (need_ext_date_time) {
        SegmentInfoEntry* segment_info =
            reinterpret_cast<SegmentInfoEntry*>(entry.get());
        base::Time::Exploded time;
        segment_info->wall_time().UTCExplode(&time);
        base::StringAppendF(&content,
                            "#EXT-X-PROGRAM-DATE-TIME:"
                            "%4d-%02d-%02dT%02d:%02d:%02d.%03dZ\n",
                            time.year, time.month, time.day_of_month,
                            time.hour, time.minute, time.second,
                            time.millisecond);
        need_ext_date_time = false;
      }
      break;
    case HlsEntry::EntryType::kExtDiscontinuity:
      // we need to add a new program data time after a discontinuity
      need_ext_date_time = hls_params_.add_ext_x_program_date_time;
      break;
    default:
      break;
    }
    base::StringAppendF(&content, "%s\n", entry->ToString().c_str());
  }

  if (hls_params_.playlist_type == HlsPlaylistType::kVod) {
    content += "#EXT-X-ENDLIST\n";
  }

  if (!File::WriteFileAtomically(file_path.c_str(), content)) {
    LOG(ERROR) << "Failed to write playlist to: " << file_path;
    return false;
  }
  return true;
}

uint64_t MediaPlaylist::MaxBitrate() const {
  if (media_info_.has_bandwidth())
    return media_info_.bandwidth();
  return bandwidth_estimator_.Max();
}

uint64_t MediaPlaylist::AvgBitrate() const {
  return bandwidth_estimator_.Estimate();
}

double MediaPlaylist::GetLongestSegmentDuration() const {
  return longest_segment_duration_seconds_;
}

void MediaPlaylist::SetTargetDuration(uint32_t target_duration) {
  if (target_duration_set_) {
    if (target_duration_ == target_duration)
      return;
    VLOG(1) << "Updating target duration from " << target_duration << " to "
            << target_duration_;
  }
  target_duration_ = target_duration;
  target_duration_set_ = true;
}

int MediaPlaylist::GetNumChannels() const {
  return media_info_.audio_info().num_channels();
}

int MediaPlaylist::GetEC3JocComplexity() const {
  return media_info_.audio_info().codec_specific_data().ec3_joc_complexity();
}

bool MediaPlaylist::GetAC4ImsFlag() const {
  return media_info_.audio_info().codec_specific_data().ac4_ims_flag();
}

bool MediaPlaylist::GetAC4CbiFlag() const {
  return media_info_.audio_info().codec_specific_data().ac4_cbi_flag();
}

bool MediaPlaylist::GetDisplayResolution(uint32_t* width,
                                         uint32_t* height) const {
  DCHECK(width);
  DCHECK(height);
  if (media_info_.has_video_info()) {
    const double pixel_aspect_ratio =
        media_info_.video_info().pixel_height() > 0
            ? static_cast<double>(media_info_.video_info().pixel_width()) /
                  media_info_.video_info().pixel_height()
            : 1.0;
    *width = static_cast<uint32_t>(media_info_.video_info().width() *
                                   pixel_aspect_ratio);
    *height = media_info_.video_info().height();
    return true;
  }
  return false;
}

std::string MediaPlaylist::GetVideoRange() const {
  // Dolby Vision (dvh1 or dvhe) is always HDR.
  if (codec_.find("dvh") == 0)
    return "PQ";

  // HLS specification:
  // https://tools.ietf.org/html/draft-pantos-hls-rfc8216bis-02#section-4.4.4.2
  switch (media_info_.video_info().transfer_characteristics()) {
    case 1:
      return "SDR";
    case 16:
    case 18:
      return "PQ";
    default:
      // Leave it empty if we do not have the transfer characteristics
      // information.
      return "";
  }
}

double MediaPlaylist::GetFrameRate() const {
  if (media_info_.video_info().frame_duration() == 0)
    return 0;
  return static_cast<double>(time_scale_) /
         media_info_.video_info().frame_duration();
}

void MediaPlaylist::AddSegmentInfoEntry(const std::string& segment_file_name,
                                        int64_t start_time,
                                        int64_t duration,
                                        uint64_t start_byte_offset,
                                        uint64_t size,
                                        base::Time reference_time) {
  if (time_scale_ == 0) {
    LOG(WARNING) << "Timescale is not set and the duration for " << duration
                 << " cannot be calculated. The output will be wrong.";

    entries_.emplace_back(new SegmentInfoEntry(
        segment_file_name, 0.0, 0.0, use_byte_range_, start_byte_offset, size,
        previous_segment_end_offset_, reference_time));
    return;
  }

  // In order for the oldest segment to be accessible for at least
  // |time_shift_buffer_depth| seconds, the latest segment should not be in the
  // sliding window since the player could be playing any part of the latest
  // segment. So the current segment duration is added to the sum of segment
  // durations (in the manifest/playlist) after sliding the window.
  SlideWindow();

  const double segment_duration_seconds =
      static_cast<double>(duration) / time_scale_;
  longest_segment_duration_seconds_ =
      std::max(longest_segment_duration_seconds_, segment_duration_seconds);
  bandwidth_estimator_.AddBlock(size, segment_duration_seconds);
  current_buffer_depth_ += segment_duration_seconds;
  const base::Time wall_time = reference_time + base::TimeDelta::FromSeconds(start_time / time_scale_);

  if (!entries_.empty() && start_time < last_segment_start_time_) {
    LOG(WARNING)
        << "Insert a discontinuity tag after the segment with start time "
        << last_segment_start_time_ << " as the next segment starts at "
        << start_time << ".";
    entries_.emplace_back(new DiscontinuityEntry());
  }

  last_segment_start_time_ = start_time;
  entries_.emplace_back(new SegmentInfoEntry(
      segment_file_name, start_time, segment_duration_seconds, use_byte_range_,
      start_byte_offset, size, previous_segment_end_offset_, wall_time));
  previous_segment_end_offset_ = start_byte_offset + size - 1;
}

uint64_t MediaPlaylist::LastSegmentStartTime() const {
 return last_segment_start_time_;
}

void MediaPlaylist::AdjustLastSegmentInfoEntryDuration(int64_t next_timestamp) {
  if (time_scale_ == 0)
    return;

  const double next_timestamp_seconds =
      static_cast<double>(next_timestamp) / time_scale_;

  for (auto iter = entries_.rbegin(); iter != entries_.rend(); ++iter) {
    if (iter->get()->type() == HlsEntry::EntryType::kExtInf) {
      SegmentInfoEntry* segment_info =
          reinterpret_cast<SegmentInfoEntry*>(iter->get());

      const double segment_duration_seconds =
          next_timestamp_seconds -
          static_cast<double>(segment_info->start_time()) / time_scale_;
      // It could be negative if timestamp messed up.
      if (segment_duration_seconds > 0)
        segment_info->set_duration_seconds(segment_duration_seconds);
      longest_segment_duration_seconds_ =
          std::max(longest_segment_duration_seconds_, segment_duration_seconds);
      break;
    }
  }
}

// TODO(kqyang): Right now this class manages the segments including the
// deletion of segments when it is no longer needed. However, this class does
// not have access to the segment file paths, which is already translated to
// segment URLs by HlsNotifier. We have to re-generate segment file paths from
// segment template here in order to delete the old segments.
// To make the pipeline cleaner, we should move all file manipulations including
// segment management to an intermediate layer between HlsNotifier and
// MediaPlaylist.
void MediaPlaylist::SlideWindow() {
  if (hls_params_.time_shift_buffer_depth <= 0.0 ||
      hls_params_.playlist_type != HlsPlaylistType::kLive) {
    return;
  }
  DCHECK_GT(time_scale_, 0u);

  if (current_buffer_depth_ <= hls_params_.time_shift_buffer_depth)
    return;

  // Temporary list to hold the EXT-X-KEYs. For example, this allows us to
  // remove <3> without removing <1> and <2> below (<1> and <2> are moved to the
  // temporary list and added back later).
  //    #EXT-X-KEY   <1>
  //    #EXT-X-KEY   <2>
  //    #EXTINF      <3>
  //    #EXTINF      <4>
  std::list<std::unique_ptr<HlsEntry>> ext_x_keys;
  // Consecutive key entries are either fully removed or not removed at all.
  // Keep track of entry types so we know if it is consecutive key entries.
  HlsEntry::EntryType prev_entry_type = HlsEntry::EntryType::kExtInf;

  std::list<std::unique_ptr<HlsEntry>>::iterator last = entries_.begin();
  for (; last != entries_.end(); ++last) {
    HlsEntry::EntryType entry_type = last->get()->type();
    if (entry_type == HlsEntry::EntryType::kExtKey) {
      if (prev_entry_type != HlsEntry::EntryType::kExtKey)
        ext_x_keys.clear();
      ext_x_keys.push_back(std::move(*last));
    } else if (entry_type == HlsEntry::EntryType::kExtDiscontinuity) {
      ++discontinuity_sequence_number_;
    } else {
      DCHECK_EQ(entry_type, HlsEntry::EntryType::kExtInf);

      const SegmentInfoEntry& segment_info =
          *reinterpret_cast<SegmentInfoEntry*>(last->get());
      // Remove the current segment only if it falls completely out of time
      // shift buffer range.
      const bool segment_within_time_shift_buffer =
          current_buffer_depth_ - segment_info.duration_seconds() <
          hls_params_.time_shift_buffer_depth;
      if (segment_within_time_shift_buffer)
        break;
      current_buffer_depth_ -= segment_info.duration_seconds();
      RemoveOldSegment(segment_info.start_time());
      media_sequence_number_++;
    }
    prev_entry_type = entry_type;
  }
  entries_.erase(entries_.begin(), last);
  // Add key entries back.
  entries_.insert(entries_.begin(), std::make_move_iterator(ext_x_keys.begin()),
                  std::make_move_iterator(ext_x_keys.end()));
}

void MediaPlaylist::RemoveOldSegment(int64_t start_time) {
  if (hls_params_.preserved_segments_outside_live_window == 0)
    return;
  if (stream_type_ == MediaPlaylistStreamType::kVideoIFramesOnly)
    return;

  segments_to_be_removed_.push_back(
      media::GetSegmentName(media_info_.segment_template(), start_time,
                            media_sequence_number_, media_info_.bandwidth()));
  while (segments_to_be_removed_.size() >
         hls_params_.preserved_segments_outside_live_window) {
    VLOG(2) << "Deleting " << segments_to_be_removed_.front();
    if (!File::Delete(segments_to_be_removed_.front().c_str())) {
      LOG(WARNING) << "Failed to delete " << segments_to_be_removed_.front()
                   << "; Will retry later.";
      break;
    }
    segments_to_be_removed_.pop_front();
  }
}

}  // namespace hls
}  // namespace shaka
