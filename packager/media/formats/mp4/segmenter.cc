// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp4/segmenter.h"

#include <algorithm>

#include "packager/base/logging.h"
#include "packager/media/base/buffer_writer.h"
#include "packager/media/base/id3_tag.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/base/muxer_util.h"
#include "packager/media/base/stream_info.h"
#include "packager/media/chunking/chunking_handler.h"
#include "packager/media/event/progress_listener.h"
#include "packager/media/formats/mp4/box_definitions.h"
#include "packager/media/formats/mp4/fragmenter.h"
#include "packager/media/formats/mp4/key_frame_info.h"
#include "packager/version/version.h"

namespace shaka {
namespace media {
namespace mp4 {

namespace {

uint64_t Rescale(uint64_t time_in_old_scale,
                 uint32_t old_scale,
                 uint32_t new_scale) {
  return static_cast<double>(time_in_old_scale) / old_scale * new_scale;
}

}  // namespace

Segmenter::Segmenter(const MuxerOptions& options,
                     std::unique_ptr<FileType> ftyp,
                     std::unique_ptr<Movie> moov)
    : options_(options),
      ftyp_(std::move(ftyp)),
      moov_(std::move(moov)),
      moof_(new MovieFragment()),
      fragment_buffer_(new BufferWriter()),
      sidx_(new SegmentIndex()) {}

Segmenter::~Segmenter() {}

Status Segmenter::Initialize(
    const std::vector<std::shared_ptr<const StreamInfo>>& streams,
    MuxerListener* muxer_listener,
    ProgressListener* progress_listener) {
  DCHECK_LT(0u, streams.size());
  muxer_listener_ = muxer_listener;
  progress_listener_ = progress_listener;
  moof_->header.sequence_number = 0;

  moof_->tracks.resize(streams.size());
  fragmenters_.resize(streams.size());
  stream_durations_.resize(streams.size());

  for (uint32_t i = 0; i < streams.size(); ++i) {
    moof_->tracks[i].header.track_id = i + 1;
    if (streams[i]->stream_type() == kStreamVideo) {
      // Use the first video stream as the reference stream (which is 1-based).
      if (sidx_->reference_id == 0)
        sidx_->reference_id = i + 1;
    }

    const EditList& edit_list = moov_->tracks[i].edit.list;
    int64_t edit_list_offset = 0;
    if (edit_list.edits.size() > 0) {
      DCHECK_EQ(edit_list.edits.size(), 1u);
      edit_list_offset = edit_list.edits.front().media_time;
    }

    fragmenters_[i].reset(
        new Fragmenter(streams[i], &moof_->tracks[i], edit_list_offset));
  }

  // Choose the first stream if there is no VIDEO.
  if (sidx_->reference_id == 0)
    sidx_->reference_id = 1;
  sidx_->timescale = streams[GetReferenceStreamId()]->time_scale();

  // Use media duration as progress target.
  progress_target_ = streams[GetReferenceStreamId()]->duration();

  // Use the reference stream's time scale as movie time scale.
  moov_->header.timescale = sidx_->timescale;
  moof_->header.sequence_number = options_.mp4_params.initial_sequence_number;

  // Fill in version information.
  const std::string version = GetPackagerVersion();
  if (!version.empty()) {
    moov_->metadata.handler.handler_type = FOURCC_ID32;
    moov_->metadata.id3v2.language.code = "eng";

    Id3Tag id3_tag;
    id3_tag.AddPrivateFrame(GetPackagerProjectUrl(), version);
    CHECK(id3_tag.WriteToVector(&moov_->metadata.id3v2.id3v2_data));
  }
  return DoInitialize();
}

Status Segmenter::Finalize() {
  // Set movie duration. Note that the duration in mvhd, tkhd, mdhd should not
  // be touched, i.e. kept at 0. The updated moov box will be written to output
  // file for VOD and static live case only.
  moov_->extends.header.fragment_duration = 0;
  for (size_t i = 0; i < stream_durations_.size(); ++i) {
    uint64_t duration =
        Rescale(stream_durations_[i], moov_->tracks[i].media.header.timescale,
                moov_->header.timescale);
    if (duration > moov_->extends.header.fragment_duration)
      moov_->extends.header.fragment_duration = duration;
  }
  return DoFinalize();
}

Status Segmenter::AddSample(size_t stream_id, const MediaSample& sample) {
  // Set default sample duration if it has not been set yet.
  if (moov_->extends.tracks[stream_id].default_sample_duration == 0) {
    moov_->extends.tracks[stream_id].default_sample_duration =
        sample.duration();
  }

  DCHECK_LT(stream_id, fragmenters_.size());
  Fragmenter* fragmenter = fragmenters_[stream_id].get();
  if (fragmenter->fragment_finalized()) {
    return Status(error::FRAGMENT_FINALIZED,
                  "Current fragment is finalized already.");
  }

  Status status = fragmenter->AddSample(sample);
  if (!status.ok())
    return status;

  if (sample_duration_ == 0)
    sample_duration_ = sample.duration();
  stream_durations_[stream_id] += sample.duration();
  return Status::OK;
}

Status Segmenter::FinalizeSegment(size_t stream_id,
                                  const SegmentInfo& segment_info) {
  if (segment_info.key_rotation_encryption_config) {
    FinalizeFragmentForKeyRotation(
        stream_id, segment_info.is_encrypted,
        *segment_info.key_rotation_encryption_config);
  }

  DCHECK_LT(stream_id, fragmenters_.size());
  Fragmenter* fragmenter = fragmenters_[stream_id].get();
  DCHECK(fragmenter);
  Status status = fragmenter->FinalizeFragment();
  if (!status.ok())
    return status;

  // Check if all tracks are ready for fragmentation.
  for (const std::unique_ptr<Fragmenter>& fragmenter : fragmenters_) {
    if (!fragmenter->fragment_finalized())
      return Status::OK;
  }

  MediaData mdat;
  // Data offset relative to 'moof': moof size + mdat header size.
  // The code will also update box sizes for moof_ and its child boxes.
  uint64_t data_offset = moof_->ComputeSize() + mdat.HeaderSize();
  // 'traf' should follow 'mfhd' moof header box.
  uint64_t next_traf_position = moof_->HeaderSize() + moof_->header.box_size();
  for (size_t i = 0; i < moof_->tracks.size(); ++i) {
    TrackFragment& traf = moof_->tracks[i];
    if (traf.auxiliary_offset.offsets.size() > 0) {
      DCHECK_EQ(traf.auxiliary_offset.offsets.size(), 1u);
      DCHECK(!traf.sample_encryption.sample_encryption_entries.empty());

      next_traf_position += traf.box_size();
      // SampleEncryption 'senc' box should be the last box in 'traf'.
      // |auxiliary_offset| should point to the data of SampleEncryption.
      traf.auxiliary_offset.offsets[0] =
          next_traf_position - traf.sample_encryption.box_size() +
          traf.sample_encryption.HeaderSize() +
          sizeof(uint32_t);  // for sample count field in 'senc'
    }
    traf.runs[0].data_offset = data_offset + mdat.data_size;
    mdat.data_size += static_cast<uint32_t>(fragmenters_[i]->data()->Size());
  }

  // Generate segment reference.
  sidx_->references.resize(sidx_->references.size() + 1);
  fragmenters_[GetReferenceStreamId()]->GenerateSegmentReference(
      &sidx_->references[sidx_->references.size() - 1]);
  sidx_->references[sidx_->references.size() - 1].referenced_size =
      data_offset + mdat.data_size;

  const uint64_t moof_start_offset = fragment_buffer_->Size();

  // Write the fragment to buffer.
  moof_->Write(fragment_buffer_.get());
  mdat.WriteHeader(fragment_buffer_.get());

  bool first_key_frame = true;
  for (const std::unique_ptr<Fragmenter>& fragmenter : fragmenters_) {
    // https://goo.gl/xcFus6 6. Trick play requirements
    // 6.10. If using fMP4, I-frame segments must include the 'moof' header
    // associated with the I-frame. It also implies that only the first key
    // frame can be included.
    if (!fragmenter->key_frame_infos().empty() && first_key_frame) {
      const KeyFrameInfo& key_frame_info =
          fragmenter->key_frame_infos().front();
      first_key_frame = false;
      key_frame_infos_.push_back(
          {key_frame_info.timestamp, moof_start_offset,
           fragment_buffer_->Size() - moof_start_offset + key_frame_info.size});
    }
    fragment_buffer_->AppendBuffer(*fragmenter->data());
  }

  // Increase sequence_number for next fragment.
  ++moof_->header.sequence_number;

  for (std::unique_ptr<Fragmenter>& fragmenter : fragmenters_)
    fragmenter->ClearFragmentFinalized();
  if (!segment_info.is_subsegment) {
    Status status = DoFinalizeSegment();
    // Reset segment information to initial state.
    sidx_->references.clear();
    key_frame_infos_.clear();
    return status;
  }
  return Status::OK;
}

uint32_t Segmenter::GetReferenceTimeScale() const {
  return moov_->header.timescale;
}

double Segmenter::GetDuration() const {
  uint64_t duration = moov_->extends.header.fragment_duration;
  if (duration == 0) {
    // Handling the case where this is not properly initialized.
    return 0.0;
  }
  return static_cast<double>(duration) / moov_->header.timescale;
}

void Segmenter::UpdateProgress(uint64_t progress) {
  accumulated_progress_ += progress;

  if (!progress_listener_) return;
  if (progress_target_ == 0) return;
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

void Segmenter::SetComplete() {
  if (!progress_listener_) return;
  progress_listener_->OnProgress(1.0);
}

uint32_t Segmenter::GetReferenceStreamId() {
  DCHECK(sidx_);
  return sidx_->reference_id - 1;
}

void Segmenter::FinalizeFragmentForKeyRotation(
    size_t stream_id,
    bool fragment_encrypted,
    const EncryptionConfig& encryption_config) {
  if (options_.mp4_params.include_pssh_in_stream) {
    moof_->pssh.clear();
    const auto& key_system_info = encryption_config.key_system_info;
    for (const ProtectionSystemSpecificInfo& system : key_system_info) {
      if (system.psshs.empty())
        continue;
      ProtectionSystemSpecificHeader pssh;
      pssh.raw_box = system.psshs;
      moof_->pssh.push_back(pssh);
    }
  } else {
    LOG(WARNING)
        << "Key rotation and no pssh in stream may not work well together.";
  }

  // Skip the following steps if the current fragment is not going to be
  // encrypted. 'pssh' box needs to be included in the fragment, which is
  // performed above, regardless of whether the fragment is encrypted. This is
  // necessary for two reasons: 1) Requesting keys before reaching encrypted
  // content avoids playback delay due to license requests; 2) In Chrome, CDM
  // must be initialized before starting the playback and CDM can only be
  // initialized with a valid 'pssh'.
  if (!fragment_encrypted)
    return;

  DCHECK_LT(stream_id, moof_->tracks.size());
  TrackFragment& traf = moof_->tracks[stream_id];
  traf.sample_group_descriptions.resize(traf.sample_group_descriptions.size() +
                                        1);
  SampleGroupDescription& sample_group_description =
      traf.sample_group_descriptions.back();
  sample_group_description.grouping_type = FOURCC_seig;

  sample_group_description.cenc_sample_encryption_info_entries.resize(1);
  CencSampleEncryptionInfoEntry& sample_group_entry =
      sample_group_description.cenc_sample_encryption_info_entries.back();
  sample_group_entry.is_protected = 1;
  sample_group_entry.per_sample_iv_size = encryption_config.per_sample_iv_size;
  sample_group_entry.constant_iv = encryption_config.constant_iv;
  sample_group_entry.crypt_byte_block = encryption_config.crypt_byte_block;
  sample_group_entry.skip_byte_block = encryption_config.skip_byte_block;
  sample_group_entry.key_id = encryption_config.key_id;
}

}  // namespace mp4
}  // namespace media
}  // namespace shaka
