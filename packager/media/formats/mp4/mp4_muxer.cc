// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp4/mp4_muxer.h"

#include <algorithm>

#include "packager/base/strings/string_number_conversions.h"
#include "packager/base/time/clock.h"
#include "packager/base/time/time.h"
#include "packager/file/file.h"
#include "packager/media/base/aes_encryptor.h"
#include "packager/media/base/audio_stream_info.h"
#include "packager/media/base/fourccs.h"
#include "packager/media/base/key_source.h"
#include "packager/media/base/media_sample.h"
#include "packager/media/base/text_stream_info.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/codecs/es_descriptor.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/formats/mp4/box_definitions.h"
#include "packager/media/formats/mp4/multi_segment_segmenter.h"
#include "packager/media/formats/mp4/single_segment_segmenter.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {
namespace mp4 {

namespace {

// Sets the range start and end value from offset and size.
// |start| and |end| are for byte-range-spec specified in RFC2616.
void SetStartAndEndFromOffsetAndSize(size_t offset, size_t size, Range* range) {
  DCHECK(range);
  range->start = static_cast<uint32_t>(offset);
  // Note that ranges are inclusive. So we need - 1.
  range->end = range->start + static_cast<uint32_t>(size) - 1;
}

FourCC CodecToFourCC(Codec codec, H26xStreamFormat h26x_stream_format) {
  switch (codec) {
    case kCodecAV1:
      return FOURCC_av01;
    case kCodecH264:
      return h26x_stream_format ==
                     H26xStreamFormat::kNalUnitStreamWithParameterSetNalus
                 ? FOURCC_avc3
                 : FOURCC_avc1;
    case kCodecH265:
      return h26x_stream_format ==
                     H26xStreamFormat::kNalUnitStreamWithParameterSetNalus
                 ? FOURCC_hev1
                 : FOURCC_hvc1;
    case kCodecH265DolbyVision:
      return h26x_stream_format ==
                     H26xStreamFormat::kNalUnitStreamWithParameterSetNalus
                 ? FOURCC_dvhe
                 : FOURCC_dvh1;
    case kCodecVP8:
      return FOURCC_vp08;
    case kCodecVP9:
      return FOURCC_vp09;
    case kCodecAAC:
    case kCodecMP3:
      return FOURCC_mp4a;
    case kCodecAC3:
      return FOURCC_ac_3;
    case kCodecDTSC:
      return FOURCC_dtsc;
    case kCodecDTSH:
      return FOURCC_dtsh;
    case kCodecDTSL:
      return FOURCC_dtsl;
    case kCodecDTSE:
      return FOURCC_dtse;
    case kCodecDTSM:
      return FOURCC_dtsm;
    case kCodecEAC3:
      return FOURCC_ec_3;
    case kCodecAC4:
      return FOURCC_ac_4;
    case kCodecFlac:
      return FOURCC_fLaC;
    case kCodecOpus:
      return FOURCC_Opus;
    default:
      return FOURCC_NULL;
  }
}

void GenerateSinf(FourCC old_type,
                  const EncryptionConfig& encryption_config,
                  ProtectionSchemeInfo* sinf) {
  sinf->format.format = old_type;

  DCHECK_NE(encryption_config.protection_scheme, FOURCC_NULL);
  sinf->type.type = encryption_config.protection_scheme;

  // The version of cenc implemented here. CENC 4.
  const int kCencSchemeVersion = 0x00010000;
  sinf->type.version = kCencSchemeVersion;

  auto& track_encryption = sinf->info.track_encryption;
  track_encryption.default_is_protected = 1;

  track_encryption.default_crypt_byte_block =
      encryption_config.crypt_byte_block;
  track_encryption.default_skip_byte_block = encryption_config.skip_byte_block;
  switch (encryption_config.protection_scheme) {
    case FOURCC_cenc:
    case FOURCC_cbc1:
      DCHECK_EQ(track_encryption.default_crypt_byte_block, 0u);
      DCHECK_EQ(track_encryption.default_skip_byte_block, 0u);
      // CENCv3 10.1 ‘cenc’ AES-CTR scheme and 10.2 ‘cbc1’ AES-CBC scheme:
      // The version of the Track Encryption Box (‘tenc’) SHALL be 0.
      track_encryption.version = 0;
      break;
    case FOURCC_cbcs:
    case FOURCC_cens:
      // CENCv3 10.3 ‘cens’ AES-CTR subsample pattern encryption scheme and
      //        10.4 ‘cbcs’ AES-CBC subsample pattern encryption scheme:
      // The version of the Track Encryption Box (‘tenc’) SHALL be 1.
      track_encryption.version = 1;
      break;
    default:
      NOTREACHED() << "Unexpected protection scheme "
                   << encryption_config.protection_scheme;
  }

  track_encryption.default_per_sample_iv_size =
      encryption_config.per_sample_iv_size;
  track_encryption.default_constant_iv = encryption_config.constant_iv;
  track_encryption.default_kid = encryption_config.key_id;
}

// The roll distance is expressed in sample units and always takes negative
// values.
int16_t GetRollDistance(uint64_t seek_preroll_ns, uint32_t sampling_frequency) {
  const double kNanosecondsPerSecond = 1000000000;
  const double preroll_in_samples =
      seek_preroll_ns / kNanosecondsPerSecond * sampling_frequency;
  // Round to closest integer.
  return -static_cast<int16_t>(preroll_in_samples + 0.5);
}

}  // namespace

MP4Muxer::MP4Muxer(const MuxerOptions& options) : Muxer(options) {}
MP4Muxer::~MP4Muxer() {}

Status MP4Muxer::InitializeMuxer() {
  // Muxer will be delay-initialized after seeing the first sample.
  to_be_initialized_ = true;
  return Status::OK;
}

Status MP4Muxer::Finalize() {
  // This happens on streams that are not initialized, i.e. not going through
  // DelayInitializeMuxer, which can only happen if there are no samples from
  // the stream.
  if (!segmenter_) {
    DCHECK(to_be_initialized_);
    LOG(INFO) << "Skip stream '" << options().output_file_name
              << "' which does not contain any sample.";
    return Status::OK;
  }

  Status segmenter_finalized = segmenter_->Finalize();

  if (!segmenter_finalized.ok())
    return segmenter_finalized;

  FireOnMediaEndEvent();
  LOG(INFO) << "MP4 file '" << options().output_file_name << "' finalized.";
  return Status::OK;
}

Status MP4Muxer::AddSample(size_t stream_id, const MediaSample& sample) {
  if (to_be_initialized_) {
    RETURN_IF_ERROR(UpdateEditListOffsetFromSample(sample));
    RETURN_IF_ERROR(DelayInitializeMuxer());
    to_be_initialized_ = false;
  }
  DCHECK(segmenter_);
  return segmenter_->AddSample(stream_id, sample);
}

Status MP4Muxer::FinalizeSegment(size_t stream_id,
                                 const SegmentInfo& segment_info) {
  DCHECK(segmenter_);
  VLOG(3) << "Finalizing " << (segment_info.is_subsegment ? "sub" : "")
          << "segment " << segment_info.start_timestamp << " duration "
          << segment_info.duration << " index " << segment_info.segment_index;
  return segmenter_->FinalizeSegment(stream_id, segment_info);
}

Status MP4Muxer::DelayInitializeMuxer() {
  DCHECK(!streams().empty());

  std::unique_ptr<FileType> ftyp(new FileType);
  std::unique_ptr<Movie> moov(new Movie);

  ftyp->major_brand = FOURCC_mp41;
  ftyp->compatible_brands.push_back(FOURCC_iso8);
  ftyp->compatible_brands.push_back(FOURCC_isom);
  ftyp->compatible_brands.push_back(FOURCC_mp41);
  ftyp->compatible_brands.push_back(FOURCC_dash);

  if (streams().size() == 1) {
    FourCC codec_fourcc = FOURCC_NULL;
    if (streams()[0]->stream_type() == kStreamVideo) {
      codec_fourcc =
          CodecToFourCC(streams()[0]->codec(),
                        static_cast<const VideoStreamInfo*>(streams()[0].get())
                            ->h26x_stream_format());
      if (codec_fourcc != FOURCC_NULL)
        ftyp->compatible_brands.push_back(codec_fourcc);
    }

    // CMAF allows only one track/stream per file.
    // CMAF requires single initialization switching for AVC3/HEV1, which is not
    // supported yet.
    if (codec_fourcc != FOURCC_avc3 && codec_fourcc != FOURCC_hev1)
      ftyp->compatible_brands.push_back(FOURCC_cmfc);
  }

  moov->header.creation_time = IsoTimeNow();
  moov->header.modification_time = IsoTimeNow();
  moov->header.next_track_id = static_cast<uint32_t>(streams().size()) + 1;

  moov->tracks.resize(streams().size());
  moov->extends.tracks.resize(streams().size());

  // Initialize tracks.
  for (uint32_t i = 0; i < streams().size(); ++i) {
    const StreamInfo* stream = streams()[i].get();
    Track& trak = moov->tracks[i];
    trak.header.track_id = i + 1;

    TrackExtends& trex = moov->extends.tracks[i];
    trex.track_id = trak.header.track_id;
    trex.default_sample_description_index = 1;

    bool generate_trak_result = false;
    switch (stream->stream_type()) {
      case kStreamVideo:
        generate_trak_result = GenerateVideoTrak(
            static_cast<const VideoStreamInfo*>(stream), &trak);
        break;
      case kStreamAudio:
        generate_trak_result = GenerateAudioTrak(
            static_cast<const AudioStreamInfo*>(stream), &trak);
        break;
      case kStreamText:
        generate_trak_result =
            GenerateTextTrak(static_cast<const TextStreamInfo*>(stream), &trak);
        break;
      default:
        NOTIMPLEMENTED() << "Not implemented for stream type: "
                         << stream->stream_type();
    }
    if (!generate_trak_result)
      return Status(error::MUXER_FAILURE, "Failed to generate trak.");

    // Generate EditList if needed. See UpdateEditListOffsetFromSample() for
    // more information.
    if (edit_list_offset_.value() > 0) {
      EditListEntry entry;
      entry.media_time = edit_list_offset_.value();
      entry.media_rate_integer = 1;
      trak.edit.list.edits.push_back(entry);
    }

    if (stream->is_encrypted() && options().mp4_params.include_pssh_in_stream) {
      moov->pssh.clear();
      const auto& key_system_info = stream->encryption_config().key_system_info;
      for (const ProtectionSystemSpecificInfo& system : key_system_info) {
        if (system.psshs.empty())
          continue;
        ProtectionSystemSpecificHeader pssh;
        pssh.raw_box = system.psshs;
        moov->pssh.push_back(pssh);
      }
    }
  }

  if (options().segment_template.empty()) {
    segmenter_.reset(new SingleSegmentSegmenter(options(), std::move(ftyp),
                                                std::move(moov)));
  } else {
    segmenter_.reset(
        new MultiSegmentSegmenter(options(), std::move(ftyp), std::move(moov)));
  }

  const Status segmenter_initialized =
      segmenter_->Initialize(streams(), muxer_listener(), progress_listener());
  if (!segmenter_initialized.ok())
    return segmenter_initialized;

  FireOnMediaStartEvent();
  return Status::OK;
}

Status MP4Muxer::UpdateEditListOffsetFromSample(const MediaSample& sample) {
  if (edit_list_offset_)
    return Status::OK;

  const int64_t pts = sample.pts();
  const int64_t dts = sample.dts();
  // An EditList entry is inserted if one of the below conditions occur [4]:
  // (1) pts > dts for the first sample. Due to Chrome's dts bug [1], dts is
  //     used in buffered range API, while pts is used elsewhere (players,
  //     manifests, and Chrome's own appendWindow check etc.), this
  //     inconsistency creates various problems, including possible stalls
  //     during playback. Since Chrome adjusts pts only when seeing EditList
  //     [2], we can insert an EditList with the time equal to difference of pts
  //     and dts to make aligned buffered ranges using pts and dts. This
  //     effectively workarounds the dts bug. It is also recommended by ISO-BMFF
  //     specification [3].
  // (2) pts == dts and with pts < 0. This happens for some audio codecs where a
  //     negative presentation timestamp signals that the sample is not supposed
  //     to be shown, i.e. for audio priming. EditList is needed to encode
  //     negative timestamps.
  // [1] https://crbug.com/718641, fixed but behind MseBufferByPts, still not
  //     enabled as of M67.
  // [2] This is actually a bug, see https://crbug.com/354518. It looks like
  //     Chrome is planning to enable the fix for [1] before addressing this
  //     bug, so we are safe.
  // [3] ISO 14496-12:2015 8.6.6.1
  //     It is recommended that such an edit be used to establish a presentation
  //     time of 0 for the first presented sample, when composition offsets are
  //     used.
  // [4] ISO 23009-19:2018 7.5.13
  //     In two cases, an EditBox containing a single EditListBox with the
  //     following constraints may be present in the CMAF header of a CMAF track
  //     to adjust the presentation time of all media samples in the CMAF track.
  //     a) The first case is a video CMAF track file using v0 TrackRunBoxes
  //        with positive composition offsets to reorder video media samples.
  //     b) The second case is an audio CMAF track where each media sample's
  //        presentation time does not equal its composition time.
  const int64_t pts_dts_offset = pts - dts;
  if (pts_dts_offset > 0) {
    if (pts < 0) {
      LOG(ERROR) << "Negative presentation timestamp (" << pts
                 << ") is not supported when there is an offset between "
                    "presentation timestamp and decoding timestamp ("
                 << dts << ").";
      return Status(error::MUXER_FAILURE,
                    "Unsupported negative pts when there is an offset between "
                    "pts and dts.");
    }
    edit_list_offset_ = pts_dts_offset;
    return Status::OK;
  }
  if (pts_dts_offset < 0) {
    LOG(ERROR) << "presentation timestamp (" << pts
               << ") is not supposed to be greater than decoding timestamp ("
               << dts << ").";
    return Status(error::MUXER_FAILURE, "Not expecting pts < dts.");
  }
  edit_list_offset_ = std::max(-sample.pts(), static_cast<int64_t>(0));
  return Status::OK;
}

void MP4Muxer::InitializeTrak(const StreamInfo* info, Track* trak) {
  int64_t now = IsoTimeNow();
  trak->header.creation_time = now;
  trak->header.modification_time = now;
  trak->header.duration = 0;
  trak->media.header.creation_time = now;
  trak->media.header.modification_time = now;
  trak->media.header.timescale = info->time_scale();
  trak->media.header.duration = 0;
  if (!info->language().empty()) {
    // Strip off the subtag, if any.
    std::string main_language = info->language();
    size_t dash = main_language.find('-');
    if (dash != std::string::npos) {
      main_language.erase(dash);
    }

    // ISO-639-2/T main language code should be 3 characters.
    if (main_language.size() != 3) {
      LOG(WARNING) << "'" << main_language << "' is not a valid ISO-639-2 "
                   << "language code, ignoring.";
    } else {
      trak->media.header.language.code = main_language;
    }
  }
}

bool MP4Muxer::GenerateVideoTrak(const VideoStreamInfo* video_info,
                                 Track* trak) {
  InitializeTrak(video_info, trak);

  // width and height specify the track's visual presentation size as
  // fixed-point 16.16 values.
  uint32_t pixel_width = video_info->pixel_width();
  uint32_t pixel_height = video_info->pixel_height();
  if (pixel_width == 0 || pixel_height == 0) {
    LOG(WARNING) << "pixel width/height are not set. Assuming 1:1.";
    pixel_width = 1;
    pixel_height = 1;
  }
  const double sample_aspect_ratio =
      static_cast<double>(pixel_width) / pixel_height;
  trak->header.width = video_info->width() * sample_aspect_ratio * 0x10000;
  trak->header.height = video_info->height() * 0x10000;

  VideoSampleEntry video;
  video.format =
      CodecToFourCC(video_info->codec(), video_info->h26x_stream_format());
  video.width = video_info->width();
  video.height = video_info->height();
  video.codec_configuration.data = video_info->codec_config();
  if (!video.ParseExtraCodecConfigsVector(video_info->extra_config())) {
    LOG(ERROR) << "Malformed extra codec configs: "
               << base::HexEncode(video_info->extra_config().data(),
                                  video_info->extra_config().size());
    return false;
  }
  if (pixel_width != 1 || pixel_height != 1) {
    video.pixel_aspect.h_spacing = pixel_width;
    video.pixel_aspect.v_spacing = pixel_height;
  }

  SampleDescription& sample_description =
      trak->media.information.sample_table.description;
  sample_description.type = kVideo;
  sample_description.video_entries.push_back(video);

  if (video_info->is_encrypted()) {
    if (video_info->has_clear_lead()) {
      // Add a second entry for clear content.
      sample_description.video_entries.push_back(video);
    }
    // Convert the first entry to an encrypted entry.
    VideoSampleEntry& entry = sample_description.video_entries[0];
    GenerateSinf(entry.format, video_info->encryption_config(), &entry.sinf);
    entry.format = FOURCC_encv;
  }
  return true;
}

bool MP4Muxer::GenerateAudioTrak(const AudioStreamInfo* audio_info,
                                 Track* trak) {
  InitializeTrak(audio_info, trak);

  trak->header.volume = 0x100;

  AudioSampleEntry audio;
  audio.format =
      CodecToFourCC(audio_info->codec(), H26xStreamFormat::kUnSpecified);
  switch (audio_info->codec()) {
    case kCodecAAC: {
      DecoderConfigDescriptor* decoder_config =
          audio.esds.es_descriptor.mutable_decoder_config_descriptor();
      decoder_config->set_object_type(ObjectType::kISO_14496_3);  // MPEG4 AAC.
      decoder_config->set_max_bitrate(audio_info->max_bitrate());
      decoder_config->set_avg_bitrate(audio_info->avg_bitrate());
      decoder_config->mutable_decoder_specific_info_descriptor()->set_data(
          audio_info->codec_config());
      break;
    }
    case kCodecDTSC:
    case kCodecDTSH:
    case kCodecDTSL:
    case kCodecDTSE:
    case kCodecDTSM:
      audio.ddts.extra_data = audio_info->codec_config();
      audio.ddts.max_bitrate = audio_info->max_bitrate();
      audio.ddts.avg_bitrate = audio_info->avg_bitrate();
      audio.ddts.sampling_frequency = audio_info->sampling_frequency();
      audio.ddts.pcm_sample_depth = audio_info->sample_bits();
      break;
    case kCodecAC3:
      audio.dac3.data = audio_info->codec_config();
      break;
    case kCodecEAC3:
      audio.dec3.data = audio_info->codec_config();
      break;
    case kCodecAC4:
      audio.dac4.data = audio_info->codec_config();
      break;
    case kCodecFlac:
      audio.dfla.data = audio_info->codec_config();
      break;
    case kCodecMP3: {
      DecoderConfigDescriptor* decoder_config =
          audio.esds.es_descriptor.mutable_decoder_config_descriptor();
      uint32_t samplerate = audio_info->sampling_frequency();
      if (samplerate < 32000)
        decoder_config->set_object_type(ObjectType::kISO_13818_3_MPEG1);
      else
        decoder_config->set_object_type(ObjectType::kISO_11172_3_MPEG1);
      decoder_config->set_max_bitrate(audio_info->max_bitrate());
      decoder_config->set_avg_bitrate(audio_info->avg_bitrate());

      // For values of DecoderConfigDescriptor.objectTypeIndication
      // that refer to streams complying with ISO/IEC 11172-3 or
      // ISO/IEC 13818-3 the decoder specific information is empty
      // since all necessary data is contained in the bitstream frames
      // itself.
      break;
    }
    case kCodecOpus:
      audio.dops.opus_identification_header = audio_info->codec_config();
      break;
    default:
      NOTIMPLEMENTED() << " Unsupported audio codec " << audio_info->codec();
      return false;
  }

  if (audio_info->codec() == kCodecAC3 || audio_info->codec() == kCodecEAC3) {
    // AC3 and EC3 does not fill in actual channel count and sample size in
    // sample description entry. Instead, two constants are used.
    audio.channelcount = 2;
    audio.samplesize = 16;
  } else if (audio_info->codec() == kCodecAC4) {
    // ETSI TS 103 190-2, E.4.5 channelcount should be set to the total number
    // of audio outputchannels of the default audio presentation of that track
    audio.channelcount = audio_info->num_channels();
    // ETSI TS 103 190-2, E.4.6 samplesize shall be set to 16.
    audio.samplesize = 16;
  } else {
    audio.channelcount = audio_info->num_channels();
    audio.samplesize = audio_info->sample_bits();
  }
  audio.samplerate = audio_info->sampling_frequency();
  SampleTable& sample_table = trak->media.information.sample_table;
  SampleDescription& sample_description = sample_table.description;
  sample_description.type = kAudio;
  sample_description.audio_entries.push_back(audio);

  if (audio_info->is_encrypted()) {
    if (audio_info->has_clear_lead()) {
      // Add a second entry for clear content.
      sample_description.audio_entries.push_back(audio);
    }
    // Convert the first entry to an encrypted entry.
    AudioSampleEntry& entry = sample_description.audio_entries[0];
    GenerateSinf(entry.format, audio_info->encryption_config(), &entry.sinf);
    entry.format = FOURCC_enca;
  }

  if (audio_info->seek_preroll_ns() > 0) {
    sample_table.sample_group_descriptions.resize(1);
    SampleGroupDescription& sample_group_description =
        sample_table.sample_group_descriptions.back();
    sample_group_description.grouping_type = FOURCC_roll;
    sample_group_description.audio_roll_recovery_entries.resize(1);
    sample_group_description.audio_roll_recovery_entries[0].roll_distance =
        GetRollDistance(audio_info->seek_preroll_ns(), audio.samplerate);
    // sample to group box is not allowed in the init segment per CMAF
    // specification. It is put in the fragment instead.
  }
  return true;
}

bool MP4Muxer::GenerateTextTrak(const TextStreamInfo* text_info, Track* trak) {
  InitializeTrak(text_info, trak);

  if (text_info->codec_string() == "wvtt") {
    // Handle WebVTT.
    TextSampleEntry webvtt;
    webvtt.format = FOURCC_wvtt;

    // 14496-30:2014 7.5 Web Video Text Tracks Sample entry format.
    // In the sample entry, a WebVTT configuration box must occur, carrying
    // exactly the lines of the WebVTT file header, i.e. all text lines up to
    // but excluding the 'two or more line terminators' that end the header.
    webvtt.config.config = "WEBVTT";
    // The spec does not define a way to carry STYLE and REGION information in
    // the mp4 container.
    if (!text_info->codec_config().empty()) {
      LOG(INFO) << "Skipping possible style / region configuration as the spec "
                   "does not define a way to carry them inside ISO-BMFF files.";
    }

    // TODO(rkuroiwa): This should be the source file URI(s). Putting bogus
    // string for now so that the box will be there for samples with overlapping
    // cues.
    webvtt.label.source_label = "source_label";
    SampleDescription& sample_description =
        trak->media.information.sample_table.description;
    sample_description.type = kText;
    sample_description.text_entries.push_back(webvtt);
    return true;
  }
  NOTIMPLEMENTED() << text_info->codec_string()
                   << " handling not implemented yet.";
  return false;
}

base::Optional<Range> MP4Muxer::GetInitRangeStartAndEnd() {
  size_t range_offset = 0;
  size_t range_size = 0;
  const bool has_range = segmenter_->GetInitRange(&range_offset, &range_size);

  if (!has_range)
    return base::nullopt;

  Range range;
  SetStartAndEndFromOffsetAndSize(range_offset, range_size, &range);
  return range;
}

base::Optional<Range> MP4Muxer::GetIndexRangeStartAndEnd() {
  size_t range_offset = 0;
  size_t range_size = 0;
  const bool has_range = segmenter_->GetIndexRange(&range_offset, &range_size);

  if (!has_range)
    return base::nullopt;

  Range range;
  SetStartAndEndFromOffsetAndSize(range_offset, range_size, &range);
  return range;
}

void MP4Muxer::FireOnMediaStartEvent() {
  if (!muxer_listener())
    return;

  if (streams().size() > 1) {
    LOG(ERROR) << "MuxerListener cannot take more than 1 stream.";
    return;
  }
  DCHECK(!streams().empty()) << "Media started without a stream.";

  const uint32_t timescale = segmenter_->GetReferenceTimeScale();
  muxer_listener()->OnMediaStart(options(), *streams().front(), timescale,
                                 MuxerListener::kContainerMp4);
}

void MP4Muxer::FireOnMediaEndEvent() {
  if (!muxer_listener())
    return;

  MuxerListener::MediaRanges media_range;
  media_range.init_range = GetInitRangeStartAndEnd();
  media_range.index_range = GetIndexRangeStartAndEnd();
  media_range.subsegment_ranges = segmenter_->GetSegmentRanges();

  const float duration_seconds = static_cast<float>(segmenter_->GetDuration());
  muxer_listener()->OnMediaEnd(media_range, duration_seconds);
}

uint64_t MP4Muxer::IsoTimeNow() {
  // Time in seconds from Jan. 1, 1904 to epoch time, i.e. Jan. 1, 1970.
  const uint64_t kIsomTimeOffset = 2082844800l;
  return kIsomTimeOffset +
         (clock() ? clock()->Now() : base::Time::Now()).ToDoubleT();
}

}  // namespace mp4
}  // namespace media
}  // namespace shaka
