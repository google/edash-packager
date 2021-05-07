// Copyright 2018 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/packed_audio/packed_audio_writer.h"

#include "packager/media/base/muxer_util.h"
#include "packager/media/formats/packed_audio/packed_audio_segmenter.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {

PackedAudioWriter::PackedAudioWriter(const MuxerOptions& muxer_options)
    : Muxer(muxer_options),
      transport_stream_timestamp_offset_(
          muxer_options.transport_stream_timestamp_offset_ms *
          kPackedAudioTimescale / 1000),
      segmenter_(new PackedAudioSegmenter(transport_stream_timestamp_offset_)) {
}

PackedAudioWriter::~PackedAudioWriter() = default;

Status PackedAudioWriter::InitializeMuxer() {
  if (streams().size() > 1u)
    return Status(error::MUXER_FAILURE, "Cannot handle more than one streams.");

  RETURN_IF_ERROR(segmenter_->Initialize(*streams()[0]));

  if (options().segment_template.empty()) {
    const std::string& file_name = options().output_file_name;
    DCHECK(!file_name.empty());
    output_file_.reset(File::Open(file_name.c_str(), "w"));
    if (!output_file_) {
      return Status(error::FILE_FAILURE,
                    "Cannot open file for write " + file_name);
    }
  }

  if (muxer_listener()) {
    muxer_listener()->OnMediaStart(options(), *streams().front(),
                                   kPackedAudioTimescale,
                                   MuxerListener::kContainerPackedAudio);
  }
  return Status::OK;
}

Status PackedAudioWriter::Finalize() {
  if (output_file_)
    RETURN_IF_ERROR(CloseFile(std::move(output_file_)));

  if (muxer_listener()) {
    muxer_listener()->OnMediaEnd(
        media_ranges_, total_duration_ * segmenter_->TimescaleScale());
  }
  return Status::OK;
}

Status PackedAudioWriter::AddMediaSample(size_t stream_id,
                                         const MediaSample& sample) {
  DCHECK_EQ(stream_id, 0u);
  return segmenter_->AddSample(sample);
}

Status PackedAudioWriter::FinalizeSegment(size_t stream_id,
                                          const SegmentInfo& segment_info) {
  DCHECK_EQ(stream_id, 0u);
  // PackedAudio does not support subsegment.
  if (segment_info.is_subsegment)
    return Status::OK;

  RETURN_IF_ERROR(segmenter_->FinalizeSegment());

  const uint64_t segment_timestamp =
      segment_info.start_timestamp * segmenter_->TimescaleScale();
  std::string segment_path =
      options().segment_template.empty()
          ? options().output_file_name
          : GetSegmentName(options().segment_template, segment_timestamp,
                           segment_info.segment_number, options().bandwidth);

  // Save |segment_size| as it will be cleared after writing.
  const size_t segment_size = segmenter_->segment_buffer()->Size();

  RETURN_IF_ERROR(WriteSegment(segment_path, segmenter_->segment_buffer()));
  total_duration_ += segment_info.duration;

  if (muxer_listener()) {
    muxer_listener()->OnNewSegment(
        segment_path, segment_timestamp + transport_stream_timestamp_offset_,
        segment_info.duration * segmenter_->TimescaleScale(), segment_size,
        segment_info.segment_number);
  }
  return Status::OK;
}

Status PackedAudioWriter::WriteSegment(const std::string& segment_path,
                                       BufferWriter* segment_buffer) {
  std::unique_ptr<File, FileCloser> file;
  if (output_file_) {
    // This is in single segment mode.
    Range range;
    range.start = media_ranges_.subsegment_ranges.empty()
                      ? 0
                      : (media_ranges_.subsegment_ranges.back().end + 1);
    range.end = range.start + segment_buffer->Size() - 1;
    media_ranges_.subsegment_ranges.push_back(range);
  } else {
    file.reset(File::Open(segment_path.c_str(), "w"));
    if (!file) {
      return Status(error::FILE_FAILURE,
                    "Cannot open file for write " + segment_path);
    }
  }

  RETURN_IF_ERROR(segment_buffer->WriteToFile(output_file_ ? output_file_.get()
                                                           : file.get()));

  if (file)
    RETURN_IF_ERROR(CloseFile(std::move(file)));
  return Status::OK;
}

Status PackedAudioWriter::CloseFile(std::unique_ptr<File, FileCloser> file) {
  std::string file_name = file->file_name();
  if (!file.release()->Close()) {
    return Status(
        error::FILE_FAILURE,
        "Cannot close file " + file_name +
            ", possibly file permission issue or running out of disk space.");
  }
  return Status::OK;
}

}  // namespace media
}  // namespace shaka
