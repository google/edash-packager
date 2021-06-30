DASH options
^^^^^^^^^^^^

--generate_static_live_mpd

    If enabled, generates static mpd. If segment_template is specified in
    stream descriptors, shaka-packager generates dynamic mpd by default; if
    this flag is enabled, shaka-packager generates static mpd instead. Note
    that if segment_template is not specified, shaka-packager always generates
    static mpd regardless of the value of this flag.

--mpd_output <file_path>

    MPD output file name.

--base_urls <comma_separated_urls>

    Comma separated BaseURLs for the MPD:
        **<url>[,<url>]...**.

    The values will be added as <BaseURL> element(s) immediately under the <MPD>
    element.

--min_buffer_time <seconds>

    Specifies, in seconds, a common duration used in the definition of the MPD
    Representation data rate.

--minimum_update_period <seconds>

    Indicates to the player how often to refresh the media presentation
    description in seconds. This value is used for dynamic MPD only.

--suggested_presentation_delay <seconds>

    Specifies a delay, in seconds, to be added to the media presentation time.
    This value is used for dynamic MPD only.

--time_shift_buffer_depth <seconds>

    Guaranteed duration of the time shifting buffer for dynamic media
    presentations, in seconds.

--preserved_segments_outside_live_window <num_segments>

    Segments outside the live window (defined by `time_shift_buffer_depth`
    above) are automatically removed except for the most recent X segments
    defined by this parameter. This is needed to accommodate latencies in
    various stages of content serving pipeline, so that the segments stay
    accessible as they may still be accessed by the player.

    The segments are not removed if the value is zero.

--utc_timings <scheme_id_uri_value_pairs>

    Comma separated UTCTiming schemeIdUri and value pairs for the MPD:
        **<scheme_id_uri>=<value>[,<scheme_id_uri>=<value>]...**

    This value is used for dynamic MPD only.

--default_language <language>

    Any audio/text tracks tagged with this language will have
    <Role ... value=\"main\" /> in the manifest.  This allows the player to
    choose the correct default language for the content.

    This applies to both audio and text tracks. The default language for text
    tracks can be overriden by  'default_text_language'.

--default_text_language <text_language>

    Same as above, but this applies to text tracks only, and overrides the
    default language for text tracks.

--allow_approximate_segment_timeline

    For live profile only.

    If enabled, segments with close duration (i.e. with difference less than
    one sample) are considered to have the same duration. This enables
    MPD generator to generate less SegmentTimeline entries. If all segments
    are of the same duration except the last one, we will do further
    optimization to use SegmentTemplate@duration instead and omit
    SegmentTimeline completely.

    Ignored if $Time$ is used in segment template, since $Time$ requires
    accurate Segment Timeline.

--dash_only=0|1

    Optional. Defaults to 0 if not specified. If it is set to 1, indicates the
    stream is DASH only.

--allow_codec_switching

    If enabled, allow adaptive switching between different codecs, if they have 
    the same language, media type (audio, video etc) and container type.

--dash_label <label_name>

    Optional. Will add Label tag to adapation set and will be taken into 
    consideration along with codecs, language, media type (audio, video etc) 
    and container type to create different adaptation sets.
