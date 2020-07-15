// Copyright 2015 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/webm/segmenter.h"

#include "packager/base/time/time.h"
#include "packager/media/base/audio_stream_info.h"
#include "packager/media/base/media_handler.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/muxer_util.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/codecs/vp_codec_configuration_record.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/event/progress_listener.h"
#include "packager/media/formats/webm/encryptor.h"
#include "packager/media/formats/webm/webm_constants.h"
#include "packager/third_party/libwebm/src/mkvmuxerutil.hpp"
#include "packager/third_party/libwebm/src/webmids.hpp"
#include "packager/version/version.h"

using mkvmuxer::AudioTrack;
using mkvmuxer::VideoTrack;

namespace shaka {
namespace media {
namespace webm {
namespace {
const int64_t kTimecodeScale = 1000000;
const int64_t kSecondsToNs = 1000000000L;

// Round to closest integer.
uint64_t Round(double value) {
  return static_cast<uint64_t>(value + 0.5);
}

// There are three different kinds of timestamp here:
//   (1) ISO-BMFF timestamp (seconds scaled by ISO-BMFF timescale)
//       This is used in our MediaSample and StreamInfo structures.
//   (2) WebM timecode (seconds scaled by kSecondsToNs / WebM timecode scale)
//       This is used in most WebM structures.
//   (3) Nanoseconds (seconds scaled by kSecondsToNs)
//       This is used in some WebM structures, e.g. Frame.
// We use Nanoseconds as intermediate format here for conversion, in
// uint64_t/int64_t, which is sufficient to represent a time as large as 292
// years.

uint64_t BmffTimestampToNs(uint64_t timestamp, uint64_t time_scale) {
  // Casting to double is needed otherwise kSecondsToNs * timestamp may overflow
  // uint64_t/int64_t.
  return Round(static_cast<double>(timestamp) / time_scale * kSecondsToNs);
}

uint64_t NsToBmffTimestamp(uint64_t ns, uint64_t time_scale) {
  // Casting to double is needed otherwise ns * time_scale may overflow
  // uint64_t/int64_t.
  return Round(static_cast<double>(ns) / kSecondsToNs * time_scale);
}

uint64_t NsToWebMTimecode(uint64_t ns, uint64_t timecode_scale) {
  return ns / timecode_scale;
}

uint64_t WebMTimecodeToNs(uint64_t timecode, uint64_t timecode_scale) {
  return timecode * timecode_scale;
}

}  // namespace

Segmenter::Segmenter(const MuxerOptions& options) : options_(options) {}

Segmenter::~Segmenter() {}

Status Segmenter::Initialize(const StreamInfo& info,
                             ProgressListener* progress_listener,
                             MuxerListener* muxer_listener) {
  is_encrypted_ = info.is_encrypted();
  duration_ = info.duration();
  time_scale_ = info.time_scale();

  muxer_listener_ = muxer_listener;

  // Use media duration as progress target.
  progress_target_ = info.duration();
  progress_listener_ = progress_listener;

  segment_info_.Init();
  segment_info_.set_timecode_scale(kTimecodeScale);

  const std::string version = GetPackagerVersion();
  if (!version.empty()) {
    segment_info_.set_writing_app(
        (GetPackagerProjectUrl() + " version " + version).c_str());
  }

  if (options().segment_template.empty()) {
    // Set an initial duration so the duration element is written; will be
    // overwritten at the end.  This works because this is a float and floats
    // are always the same size.
    segment_info_.set_duration(1);
  }

  // Create the track info.
  // The seed is only used to create a UID which we overwrite later.
  unsigned int seed = 0;
  std::unique_ptr<mkvmuxer::Track> track;
  Status status;
  switch (info.stream_type()) {
    case kStreamVideo: {
      std::unique_ptr<VideoTrack> video_track(new VideoTrack(&seed));
      status = InitializeVideoTrack(static_cast<const VideoStreamInfo&>(info),
                                    video_track.get());
      track = std::move(video_track);
      break;
    }
    case kStreamAudio: {
      std::unique_ptr<AudioTrack> audio_track(new AudioTrack(&seed));
      status = InitializeAudioTrack(static_cast<const AudioStreamInfo&>(info),
                                    audio_track.get());
      track = std::move(audio_track);
      break;
    }
    default:
      NOTIMPLEMENTED() << "Not implemented for stream type: "
                       << info.stream_type();
      status = Status(error::UNIMPLEMENTED, "Not implemented for stream type");
  }
  if (!status.ok())
    return status;

  if (info.is_encrypted()) {
    if (info.encryption_config().per_sample_iv_size != kWebMIvSize)
      return Status(error::MUXER_FAILURE, "Incorrect size WebM encryption IV.");
    status = UpdateTrackForEncryption(info.encryption_config().key_id,
                                      track.get());
    if (!status.ok())
      return status;
  }

  tracks_.AddTrack(track.get(), info.track_id());
  // number() is only available after the above instruction.
  track_id_ = track->number();
  // |tracks_| owns |track|.
  track.release();
  return DoInitialize();
}

Status Segmenter::Finalize() {
  if (prev_sample_) {
    uint64_t duration =
        prev_sample_->pts() - first_timestamp_ + prev_sample_->duration();
    segment_info_.set_duration(FromBmffTimestamp(duration));
  }
  return DoFinalize();
}

Status Segmenter::AddSample(const MediaSample& source_sample) {
  std::shared_ptr<MediaSample> sample(source_sample.Clone());

  if (sample_duration_ == 0) {
    first_timestamp_ = sample->pts();
    sample_duration_ = sample->duration();
    if (muxer_listener_)
      muxer_listener_->OnSampleDurationReady(sample_duration_);
  }

  UpdateProgress(sample->duration());

  // This writes frames in a delay.  Meaning that the previous frame is written
  // on this call to AddSample.  The current frame is stored until the next
  // call.  This is done to determine which frame is the last in a Cluster.
  // This first block determines if this is a new Cluster and writes the
  // previous frame first before creating the new Cluster.

  Status status;
  if (new_segment_ || new_subsegment_) {
    status = NewSegment(sample->pts(), new_subsegment_);
  } else {
    status = WriteFrame(false /* write_duration */);
  }
  if (!status.ok())
    return status;

  if (is_encrypted_)
    UpdateFrameForEncryption(sample.get());

  new_subsegment_ = false;
  new_segment_ = false;
  prev_sample_ = sample;
  return Status::OK;
}

Status Segmenter::FinalizeSegment(uint64_t start_timestamp,
                                  uint64_t duration_timestamp,
                                  bool is_subsegment) {
  if (is_subsegment)
    new_subsegment_ = true;
  else
    new_segment_ = true;
  return WriteFrame(true /* write duration */);
}

float Segmenter::GetDurationInSeconds() const {
  return WebMTimecodeToNs(segment_info_.duration(),
                          segment_info_.timecode_scale()) /
         static_cast<double>(kSecondsToNs);
}

uint64_t Segmenter::FromBmffTimestamp(uint64_t bmff_timestamp) {
  return NsToWebMTimecode(
      BmffTimestampToNs(bmff_timestamp, time_scale_),
      segment_info_.timecode_scale());
}

uint64_t Segmenter::FromWebMTimecode(uint64_t webm_timecode) {
  return NsToBmffTimestamp(
      WebMTimecodeToNs(webm_timecode, segment_info_.timecode_scale()),
      time_scale_);
}

Status Segmenter::WriteSegmentHeader(uint64_t file_size, mkvmuxer::IMkvWriter* writer) {
  Status error_status(error::FILE_FAILURE, "Error writing segment header.");

  if (!WriteEbmlHeader(writer))
    return error_status;

  if (WriteID(writer, mkvmuxer::kMkvSegment) != 0)
    return error_status;

  const uint64_t segment_size_size = 8;
  segment_payload_pos_ = writer->Position() + segment_size_size;
  if (file_size > 0) {
    // We want the size of the segment element, so subtract the header.
    if (WriteUIntSize(writer, file_size - segment_payload_pos_,
                      segment_size_size) != 0)
      return error_status;
    if (!seek_head_.Write(writer))
      return error_status;
  } else {
    if (SerializeInt(writer, mkvmuxer::kEbmlUnknownValue, segment_size_size) !=
        0)
      return error_status;
    // We don't know the header size, so write a placeholder.
    if (!seek_head_.WriteVoid(writer))
      return error_status;
  }

  seek_head_.set_info_pos(writer->Position() - segment_payload_pos_);
  if (!segment_info_.Write(writer))
    return error_status;

  seek_head_.set_tracks_pos(writer->Position() - segment_payload_pos_);
  if (!tracks_.Write(writer))
    return error_status;

  return Status::OK;
}

Status Segmenter::SetCluster(uint64_t start_webm_timecode,
                             uint64_t position,
                             mkvmuxer::IMkvWriter* writer) {
  const uint64_t scale = segment_info_.timecode_scale();
  cluster_.reset(new mkvmuxer::Cluster(start_webm_timecode, position, scale));
  cluster_->Init(writer);
  return Status::OK;
}

void Segmenter::UpdateProgress(uint64_t progress) {
  accumulated_progress_ += progress;
  if (!progress_listener_ || progress_target_ == 0)
    return;
  // It might happen that accumulated progress exceeds progress_target due to
  // computation errors, e.g. rounding error. Cap it so it never reports > 100%
  // progress.
  if (accumulated_progress_ >= progress_target_) {
    progress_listener_->OnProgress(1.0);
  } else {
    progress_listener_->OnProgress(static_cast<double>(accumulated_progress_) /
                                   progress_target_);
  }
}

Status Segmenter::InitializeVideoTrack(const VideoStreamInfo& info,
                                       VideoTrack* track) {
  if (info.codec() == kCodecAV1) {
    track->set_codec_id("V_AV1");
    if (!track->SetCodecPrivate(info.codec_config().data(),
                                info.codec_config().size())) {
      return Status(error::INTERNAL_ERROR,
                    "Private codec data required for AV1 streams");
    }
  } else if (info.codec() == kCodecVP8) {
    track->set_codec_id("V_VP8");
  } else if (info.codec() == kCodecVP9) {
    track->set_codec_id("V_VP9");

    // The |StreamInfo::codec_config| field is stored using the MP4 format; we
    // need to convert it to the WebM format.
    VPCodecConfigurationRecord vp_config;
    if (!vp_config.ParseMP4(info.codec_config())) {
      return Status(error::INTERNAL_ERROR,
                    "Unable to parse VP9 codec configuration");
    }

    mkvmuxer::Colour colour;
    if (vp_config.matrix_coefficients() != AVCOL_SPC_UNSPECIFIED) {
      colour.set_matrix_coefficients(vp_config.matrix_coefficients());
    }
    if (vp_config.transfer_characteristics() != AVCOL_TRC_UNSPECIFIED) {
      colour.set_transfer_characteristics(vp_config.transfer_characteristics());
    }
    if (vp_config.color_primaries() != AVCOL_PRI_UNSPECIFIED) {
      colour.set_primaries(vp_config.color_primaries());
    }
    if (!track->SetColour(colour)) {
      return Status(error::INTERNAL_ERROR,
                    "Failed to setup color element for VPx streams");
    }

    std::vector<uint8_t> codec_config;
    vp_config.WriteWebM(&codec_config);
    if (!track->SetCodecPrivate(codec_config.data(), codec_config.size())) {
      return Status(error::INTERNAL_ERROR,
                    "Private codec data required for VPx streams");
    }
  } else {
    LOG(ERROR) << "Only VP8, VP9 and AV1 video codecs are supported in WebM.";
    return Status(error::UNIMPLEMENTED,
                  "Only VP8, VP9 and AV1 video codecs are supported in WebM.");
  }

  track->set_uid(info.track_id());
  if (!info.language().empty())
    track->set_language(info.language().c_str());
  track->set_type(mkvmuxer::Tracks::kVideo);
  track->set_width(info.width());
  track->set_height(info.height());
  track->set_display_height(info.height());
  track->set_display_width(info.width() * info.pixel_width() /
                           info.pixel_height());
  return Status::OK;
}

Status Segmenter::InitializeAudioTrack(const AudioStreamInfo& info,
                                       AudioTrack* track) {
  if (info.codec() == kCodecOpus) {
    track->set_codec_id(mkvmuxer::Tracks::kOpusCodecId);
  } else if (info.codec() == kCodecVorbis) {
    track->set_codec_id(mkvmuxer::Tracks::kVorbisCodecId);
  } else {
    LOG(ERROR) << "Only Vorbis and Opus audio codec are supported in WebM.";
    return Status(error::UNIMPLEMENTED,
                  "Only Vorbis and Opus audio codecs are supported in WebM.");
  }
  if (!track->SetCodecPrivate(info.codec_config().data(),
                              info.codec_config().size())) {
    return Status(error::INTERNAL_ERROR,
                  "Private codec data required for audio streams");
  }

  track->set_uid(info.track_id());
  if (!info.language().empty())
    track->set_language(info.language().c_str());
  track->set_type(mkvmuxer::Tracks::kAudio);
  track->set_sample_rate(info.sampling_frequency());
  track->set_channels(info.num_channels());
  track->set_seek_pre_roll(info.seek_preroll_ns());
  track->set_codec_delay(info.codec_delay_ns());
  return Status::OK;
}

Status Segmenter::WriteFrame(bool write_duration) {
  // Create a frame manually so we can create non-SimpleBlock frames.  This
  // is required to allow the frame duration to be added.  If the duration
  // is not set, then a SimpleBlock will still be written.
  mkvmuxer::Frame frame;

  if (!frame.Init(prev_sample_->data(), prev_sample_->data_size())) {
    return Status(error::MUXER_FAILURE,
                  "Error adding sample to segment: Frame::Init failed");
  }

  if (write_duration) {
    frame.set_duration(
        BmffTimestampToNs(prev_sample_->duration(), time_scale_));
  }
  frame.set_is_key(prev_sample_->is_key_frame());
  frame.set_timestamp(
      BmffTimestampToNs(prev_sample_->pts(), time_scale_));
  frame.set_track_number(track_id_);

  if (prev_sample_->side_data_size() > 0) {
    uint64_t block_add_id;
    // First 8 bytes of side_data is the BlockAddID element's value, which is
    // done to mimic ffmpeg behavior. See webm_cluster_parser.cc for details.
    CHECK_GT(prev_sample_->side_data_size(), sizeof(block_add_id));
    memcpy(&block_add_id, prev_sample_->side_data(), sizeof(block_add_id));
    if (!frame.AddAdditionalData(
            prev_sample_->side_data() + sizeof(block_add_id),
            prev_sample_->side_data_size() - sizeof(block_add_id),
            block_add_id)) {
      return Status(
          error::MUXER_FAILURE,
          "Error adding sample to segment: Frame::AddAditionalData Failed");
    }
  }

  if (!prev_sample_->is_key_frame() && !frame.CanBeSimpleBlock()) {
    frame.set_reference_block_timestamp(
        BmffTimestampToNs(reference_frame_timestamp_, time_scale_));
  }

  // GetRelativeTimecode will return -1 if the relative timecode is too large
  // to fit in the frame.
  if (cluster_->GetRelativeTimecode(NsToWebMTimecode(
          frame.timestamp(), cluster_->timecode_scale())) < 0) {
    const double segment_duration =
        static_cast<double>(frame.timestamp() -
                            WebMTimecodeToNs(cluster_->timecode(),
                                             cluster_->timecode_scale())) /
        kSecondsToNs;
    LOG(ERROR) << "Error adding sample to segment: segment too large, "
               << segment_duration
               << " seconds. Please check your GOP size and segment duration.";
    return Status(error::MUXER_FAILURE,
                  "Error adding sample to segment: segment too large");
  }

  if (!cluster_->AddFrame(&frame)) {
    return Status(error::MUXER_FAILURE,
                  "Error adding sample to segment: Cluster::AddFrame failed");
  }

  // A reference frame is needed for non-keyframes.  Having a reference to the
  // previous block is good enough.
  // See libwebm Segment::AddGenericFrame
  reference_frame_timestamp_ = prev_sample_->pts();
  return Status::OK;
}

}  // namespace webm
}  // namespace media
}  // namespace shaka
