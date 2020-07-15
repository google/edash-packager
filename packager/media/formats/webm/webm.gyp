# Copyright 2015 Google Inc. All rights reserved.
#
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file or at
# https://developers.google.com/open-source/licenses/bsd

{
  'variables': {
    'shaka_code': 1,
  },
  'targets': [
    {
      'target_name': 'webm',
      'type': '<(component)',
      'sources': [
        'buffer_mkv_writer.cc',
        'buffer_mkv_writer.h',
        'encryptor.cc',
        'encryptor.h',
        'mkv_writer.cc',
        'mkv_writer.h',
        'multi_segment_segmenter.cc',
        'multi_segment_segmenter.h',
        'seek_head.cc',
        'seek_head.h',
        'segmenter.cc',
        'segmenter.h',
        'single_segment_segmenter.cc',
        'single_segment_segmenter.h',
        'two_pass_single_segment_segmenter.cc',
        'two_pass_single_segment_segmenter.h',
        'webm_audio_client.cc',
        'webm_audio_client.h',
        'webm_cluster_parser.cc',
        'webm_cluster_parser.h',
        'webm_constants.cc',
        'webm_constants.h',
        'webm_content_encodings.cc',
        'webm_content_encodings.h',
        'webm_content_encodings_client.cc',
        'webm_content_encodings_client.h',
        'webm_crypto_helpers.cc',
        'webm_crypto_helpers.h',
        'webm_info_parser.cc',
        'webm_info_parser.h',
        'webm_parser.cc',
        'webm_parser.h',
        'webm_media_parser.cc',
        'webm_media_parser.h',
        'webm_muxer.cc',
        'webm_muxer.h',
        'webm_tracks_parser.cc',
        'webm_tracks_parser.h',
        'webm_video_client.cc',
        'webm_video_client.h',
        'webm_webvtt_parser.cc',
        'webm_webvtt_parser.h'
      ],
      'dependencies': [
        '../../../third_party/boringssl/boringssl.gyp:boringssl',
        '../../../third_party/gflags/gflags.gyp:gflags',
        '../../../third_party/libwebm/libwebm.gyp:mkvmuxer',
        '../../base/media_base.gyp:media_base',
        '../../codecs/codecs.gyp:codecs'
      ],
    },
    {
      'target_name': 'webm_unittest',
      'type': '<(gtest_target_type)',
      'sources': [
        'cluster_builder.cc',
        'cluster_builder.h',
        'encrypted_segmenter_unittest.cc',
        'encryptor_unittest.cc',
        'multi_segment_segmenter_unittest.cc',
        'segmenter_test_base.cc',
        'segmenter_test_base.h',
        'single_segment_segmenter_unittest.cc',
        'tracks_builder.cc',
        'tracks_builder.h',
        'webm_cluster_parser_unittest.cc',
        'webm_content_encodings_client_unittest.cc',
        'webm_parser_unittest.cc',
        'webm_tracks_parser_unittest.cc',
        'webm_webvtt_parser_unittest.cc',
      ],
      'dependencies': [
        '../../../file/file.gyp:file',
        '../../../testing/gtest.gyp:gtest',
        '../../../testing/gmock.gyp:gmock',
        '../../../third_party/libwebm/libwebm.gyp:mkvmuxer',
        '../../test/media_test.gyp:media_test_support',
        'webm',
      ]
    },
  ],
}
