HLS options
^^^^^^^^^^^

--hls_master_playlist_output <file_path>

    Output path for the master playlist for HLS. This flag must be used to
    output HLS.

--hls_base_url <url>

    The base URL for the Media Playlists and media files listed in the
    playlists. This is the prefix for the files.

--hls_key_uri <uri>

    The key uri for 'identity' and 'com.apple.streamingkeydelivery' (FairPlay)
    key formats. Ignored if the playlist is not encrypted or not using the above
    key formats.

--hls_playlist_type <type>

    VOD, EVENT, or LIVE. This defines the EXT-X-PLAYLIST-TYPE in the HLS
    specification. For hls_playlist_type of LIVE, EXT-X-PLAYLIST-TYPE tag is
    omitted.

--time_shift_buffer_depth <seconds>

    Guaranteed duration of the time shifting buffer for LIVE playlists, in
    seconds.

--preserved_segments_outside_live_window <num_segments>

    Segments outside the live window (defined by `time_shift_buffer_depth`
    above) are automatically removed except for the most recent X segments
    defined by this parameter. This is needed to accommodate latencies in
    various stages of content serving pipeline, so that the segments stay
    accessible as they may still be accessed by the player.

    The segments are not removed if the value is zero.

--default_language <language>

    The first audio/text rendition in a group tagged with this language will
    have 'DEFAULT' attribute set to 'YES'. This allows the player to choose the
    correct default language for the content.

    This applies to both audio and text tracks. The default language for text
    tracks can be overriden by  'default_text_language'.

--default_text_language <text_language>

    Same as above, but this applies to text tracks only, and overrides the
    default language for text tracks.

--hls_media_sequence_number <unsigned_number>

    HLS uses the EXT-X-MEDIA-SEQUENCE tag at the start of a live playlist in
    order to specify the first segment sequence number. This is because any
    live playlist have a limited number of segments, and they also keep
    updating with new segments while removing old ones. When a player refreshes
    the playlist, this information is important for keeping track of segments
    positions.

    When the packager starts, it naturally starts this count from zero. However,
    there are many situations where the packager may be restarted, without this
    meaning starting this value from zero (but continuing a previous sequence).
    The most common situations are problems in the encoder feeding the packager.

    With those cases in mind, this parameter allows to set the initial
    EXT-X-MEDIA-SEQUENCE value. This way, it's possible to continue the sequence
    number from previous packager run.

    For more information about the reasoning of this, please see issue
    `#691 <https://github.com/google/shaka-packager/issues/691>`_.

    The EXT-X-MEDIA-SEQUENCE documentation can be read here:
    https://tools.ietf.org/html/rfc8216#section-4.3.3.2.


--hls_ext_x_program_date_time <time_offset_ms>

    Adds an EXT-X-PROGRAM-DATE-TIME tag to every Media Segment, with fixed UTC
    timezone. Time offset is the difference in milliseconds between packaging time
    and live event time.

--hls_only=0|1

    Optional. Defaults to 0 if not specified. If it is set to 1, indicates the
    stream is HLS only.
