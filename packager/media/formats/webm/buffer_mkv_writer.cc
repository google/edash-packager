// Copyright 2020 Google LLC. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/webm/buffer_mkv_writer.h"

namespace shaka {
namespace media {

BufferMkvWriter::BufferMkvWriter() {}

BufferMkvWriter::~BufferMkvWriter() {}

Status BufferMkvWriter::WriteToFile(const std::string& file_name) {

  std::unique_ptr<File, FileCloser> file;
      
  file.reset(File::Open(file_name.c_str(), "w"));
  if (!file)
    return Status(error::FILE_FAILURE, "Unable to open file for writing.");
  
  if (!segment_buffer_.WriteToFile(file.get()).ok())
    return Status(error::FILE_FAILURE, "Unable to write to file.");
  
  if (!file.release()->Close()) {
    return Status(
        error::FILE_FAILURE,
        "Cannot close file " + file_name +
            ", possibly file permission issue or running out of disk space.");
  }
  return Status::OK;
}

mkvmuxer::int32 BufferMkvWriter::Write(const void* buf, mkvmuxer::uint32 len) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(buf);
  segment_buffer_.AppendArray(data, len);
  return 0;
}

mkvmuxer::int64 BufferMkvWriter::Position() const {
  return segment_buffer_.Size();
}

mkvmuxer::int32 BufferMkvWriter::Position(mkvmuxer::int64 position) {
  return -1;
}

bool BufferMkvWriter::Seekable() const {
  return false;
}

void BufferMkvWriter::ElementStartNotify(mkvmuxer::uint64 element_id,
                                   mkvmuxer::int64 position) {}

}  // namespace media
}  // namespace shaka
