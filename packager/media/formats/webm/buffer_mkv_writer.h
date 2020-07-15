// Copyright 2020 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_WEBM_BUFFER_MKV_WRITER_H_
#define PACKAGER_MEDIA_FORMATS_WEBM_BUFFER_MKV_WRITER_H_

#include <memory>
#include <string>

#include "packager/file/file_closer.h"
#include "packager/status.h"
#include "packager/third_party/libwebm/src/mkvmuxer.hpp"
#include "packager/media/base/buffer_writer.h"

namespace shaka {
namespace media {

/// An implementation of IMkvWriter using a buffer.
class BufferMkvWriter : public mkvmuxer::IMkvWriter {
 public:
  BufferMkvWriter();
  ~BufferMkvWriter() override;

  /// Writes out @a len bytes of @a buf.
  /// @return 0 on success.
  mkvmuxer::int32 Write(const void* buf, mkvmuxer::uint32 len) override;
  /// @return Buffer size.
  mkvmuxer::int64 Position() const override;
  /// @return -1 as not supported.
  mkvmuxer::int32 Position(mkvmuxer::int64 position) override;
  /// @return false as writer is not seekable.
  bool Seekable() const override;
  /// Element start notification. Called whenever an element identifier is about
  /// to be written to the stream.  @a element_id is the element identifier, and
  /// @a position is the location in the WebM stream where the first octet of
  /// the element identifier will be written.
  /// Note: the |MkvId| enumeration in webmids.hpp defines element values.
  void ElementStartNotify(mkvmuxer::uint64 element_id,
                          mkvmuxer::int64 position) override;
  /// Creates a file with name @a file_name, flushes 
  /// current_buffer_ to it and closes the file.
  /// @param name The path to the file to open.
  /// @return File creation, buffer flushing and file close succeeded or failed. 
  virtual Status WriteToFile(const std::string& file_name);

 private:
  BufferWriter segment_buffer_;

  DISALLOW_COPY_AND_ASSIGN(BufferMkvWriter);
};

}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_WEBM_BUFFER_MKV_WRITER_H_
