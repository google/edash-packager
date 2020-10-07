// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP2T_TS_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_TS_SEGMENTER_H_

#include <memory>

#include "packager/file/file.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/formats/mp2t/pes_packet_generator.h"
#include "packager/media/formats/mp2t/ts_writer.h"
#include "packager/status.h"

namespace shaka {
namespace media {

class KeySource;
class MuxerListener;

namespace mp2t {

// TODO(rkuroiwa): For now, this implements multifile segmenter. Like other
// make this an abstract super class and implement multifile and single file
// segmenters.
class TsSegmenter {
 public:
  // TODO(rkuroiwa): Add progress listener?
  /// @param options is the options for this muxer. This must stay valid
  ///        throughout the life time of the instance.
  /// @param listener is the MuxerListener that should be used to notify events.
  ///        This may be null, in which case no events are sent.
  TsSegmenter(const MuxerOptions& options, MuxerListener* listener);
  ~TsSegmenter();

  /// Initialize the object.
  /// @param stream_info is the stream info for the segmenter.
  /// @return OK on success.
  Status Initialize(const StreamInfo& stream_info);

  /// Finalize the segmenter.
  /// @return OK on success.
  Status Finalize();

  /// @param sample gets added to this object.
  /// @return OK on success.
  Status AddSample(const MediaSample& sample);

  /// Flush all the samples that are (possibly) buffered and write them to the
  /// current segment, this will close the file. If a file is not already opened
  /// before calling this, this will open one and write them to file.
  /// @param start_timestamp is the segment's start timestamp in the input
  ///        stream's time scale.
  /// @param duration is the segment's duration in the input stream's time
  ///        scale.
  /// @param segment_index is the segment index.
  // TODO(kqyang): Remove the usage of segment start timestamp and duration in
  // xx_segmenter, which could cause confusions on which is the source of truth
  // as the segment start timestamp and duration could be tracked locally.
  Status FinalizeSegment(uint64_t start_timestamp,
                         uint64_t duration,
                         int64_t segment_index);

  /// Only for testing.
  void InjectTsWriterForTesting(std::unique_ptr<TsWriter> writer);

  /// Only for testing.
  void InjectPesPacketGeneratorForTesting(
      std::unique_ptr<PesPacketGenerator> generator);

  /// Only for testing.
  void SetSegmentStartedForTesting(bool value);

 private:
  Status StartSegmentIfNeeded(int64_t next_pts);

  // Writes PES packets (carried in TsPackets) to a buffer.
  Status WritePesPackets();

  const MuxerOptions& muxer_options_;
  MuxerListener* const listener_;

  // Codec for the stream.
  Codec codec_ = kUnknownCodec;
  std::vector<uint8_t> audio_codec_config_;

  const uint32_t transport_stream_timestamp_offset_ = 0;
  // Scale used to scale the input stream to TS's timesccale (which is 90000).
  // Used for calculating the duration in seconds fo the current segment.
  double timescale_scale_ = 1.0;

  std::unique_ptr<TsWriter> ts_writer_;

  BufferWriter segment_buffer_;

  // Set to true if segment_buffer_ is initialized, set to false after
  // FinalizeSegment() succeeds.
  bool segment_started_ = false;
  std::unique_ptr<PesPacketGenerator> pes_packet_generator_;

  int64_t segment_start_timestamp_ = -1;
  DISALLOW_COPY_AND_ASSIGN(TsSegmenter);
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
#endif  // PACKAGER_MEDIA_FORMATS_MP2T_TS_SEGMENTER_H_
