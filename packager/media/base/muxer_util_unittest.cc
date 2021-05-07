// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gtest/gtest.h>

#include "packager/media/base/muxer_util.h"

namespace shaka {
namespace media {

TEST(MuxerUtilTest, ValidateSegmentTemplate) {
  EXPECT_NE(Status::OK, ValidateSegmentTemplate(""));

  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Number$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Time$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Time$$Time$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("foo$Time$goo"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Number$_$Number$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Number$$Bandwidth$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Time$$Bandwidth$"));

  // Bandwidth without Number or Time should not be valid.
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$Bandwidth$"));

  // Escape sequence "$$".
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("foo$Time$__$$loo"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("foo$Time$$$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$$$Time$$$"));

  // Missing $Number$ / $Time$.
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$$"));
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("foo$$goo"));

  // $Number$, $Time$ should not co-exist.
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$Number$$Time$"));
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("foo$Number$_$Time$loo"));

  // $RepresentationID$ not implemented yet.
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$RepresentationID$__$Time$"));

  // Unknown identifier.
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$foo$$Time$"));
}

TEST(MuxerUtilTest, ValidateSegmentTemplateWithFormatTag) {
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Time%01d$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Time%05d$"));
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$Time%1d$"));
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$Time%$"));
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$Time%01$"));
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$Time%0xd$"));
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$Time%03xd$"));
  // $$ should not have any format tag.
  EXPECT_NE(Status::OK, ValidateSegmentTemplate("$%01d$$Time$"));
  // Format specifier edge cases.
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Time%00d$"));
  EXPECT_EQ(Status::OK, ValidateSegmentTemplate("$Time%005d$"));
}

TEST(MuxerUtilTest, GetSegmentName) {
  const uint64_t kSegmentStartTime = 180180;
  const uint32_t kSegmentNumber = 12;
  const uint32_t kBandwidth = 1234;
  EXPECT_EQ("12", GetSegmentName("$Number$", kSegmentStartTime, kSegmentNumber,
                                 kBandwidth));
  EXPECT_EQ("012", GetSegmentName("$Number%03d$", kSegmentStartTime,
                                  kSegmentNumber, kBandwidth));
  EXPECT_EQ("12$foo$00012",
            GetSegmentName("$Number%01d$$$foo$$$Number%05d$", kSegmentStartTime,
                           kSegmentNumber, kBandwidth));

  EXPECT_EQ("180180", GetSegmentName("$Time$", kSegmentStartTime,
                                     kSegmentNumber, kBandwidth));
  EXPECT_EQ("foo$_$18018000180180.m4s",
            GetSegmentName("foo$$_$$$Time%01d$$Time%08d$.m4s",
                           kSegmentStartTime, kSegmentNumber, kBandwidth));

  // Combo values.
  EXPECT_EQ("12-1234", GetSegmentName("$Number$-$Bandwidth$", kSegmentStartTime,
                                      kSegmentNumber, kBandwidth));
  EXPECT_EQ("012-001234",
            GetSegmentName("$Number%03d$-$Bandwidth%06d$", kSegmentStartTime,
                           kSegmentNumber, kBandwidth));

  // Format specifier edge cases.
  EXPECT_EQ("12", GetSegmentName("$Number%00d$", kSegmentStartTime,
                                 kSegmentNumber, kBandwidth));
  EXPECT_EQ("00012", GetSegmentName("$Number%005d$", kSegmentStartTime,
                                    kSegmentNumber, kBandwidth));
}

TEST(MuxerUtilTest, GetSegmentNameWithIndexZero) {
  const uint64_t kSegmentStartTime = 0;
  const uint32_t kSegmentNumber = 1;
  const uint32_t kBandwidth = 0;
  EXPECT_EQ("1", GetSegmentName("$Number$", kSegmentStartTime, kSegmentNumber,
                                kBandwidth));
  EXPECT_EQ("001", GetSegmentName("$Number%03d$", kSegmentStartTime,
                                  kSegmentNumber, kBandwidth));

  EXPECT_EQ("0", GetSegmentName("$Time$", kSegmentStartTime, kSegmentNumber,
                                kBandwidth));
  EXPECT_EQ("00000000.m4s", GetSegmentName("$Time%08d$.m4s", kSegmentStartTime,
                                           kSegmentNumber, kBandwidth));
}

TEST(MuxerUtilTest, GetSegmentNameLargeTime) {
  const uint64_t kSegmentStartTime = 1601599839840ULL;
  const uint32_t kSegmentNumber = 8888889;
  const uint32_t kBandwidth = 444444;
  EXPECT_EQ("1601599839840", GetSegmentName("$Time$", kSegmentStartTime,
                                            kSegmentNumber, kBandwidth));
}

}  // namespace media
}  // namespace shaka
