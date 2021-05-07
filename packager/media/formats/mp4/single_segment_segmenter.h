// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP4_SINGLE_SEGMENT_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_MP4_SINGLE_SEGMENT_SEGMENTER_H_

#include "packager/file/file_closer.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/formats/mp4/segmenter.h"

namespace shaka {
namespace media {
namespace mp4 {

/// Segmenter for MP4 Dash Video-On-Demand profile. A single MP4 file with a
/// single segment is created, i.e. with only one SIDX box. The generated media
/// file can contain one or many subsegments with subsegment duration
/// defined by @b MuxerOptions.segment_duration. A subsegment can contain one
/// or many fragments with fragment duration defined by @b
/// MuxerOptions.fragment_duration. The actual subsegment or fragment duration
/// may not match the requested duration exactly, but will be approximated. That
/// is, the Segmenter tries to end subsegment/fragment at the first sample with
/// overall subsegment/fragment duration not smaller than defined duration and
/// yet meet SAP requirements.
class SingleSegmentSegmenter : public Segmenter {
 public:
  SingleSegmentSegmenter(const MuxerOptions& options,
                         std::unique_ptr<FileType> ftyp,
                         std::unique_ptr<Movie> moov);
  ~SingleSegmentSegmenter() override;

  /// @name Segmenter implementation overrides.
  /// @{
  bool GetInitRange(size_t* offset, size_t* size) override;
  bool GetIndexRange(size_t* offset, size_t* size) override;
  std::vector<Range> GetSegmentRanges() override;
  /// @}

 private:
  // Segmenter implementation overrides.
  Status DoInitialize() override;
  Status DoFinalize() override;
  Status DoFinalizeSegment(int64_t segment_number) override;

  std::unique_ptr<SegmentIndex> vod_sidx_;
  std::string temp_file_name_;
  std::unique_ptr<File, FileCloser> temp_file_;

  DISALLOW_COPY_AND_ASSIGN(SingleSegmentSegmenter);
};

}  // namespace mp4
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_MP4_SINGLE_SEGMENT_SEGMENTER_H_
