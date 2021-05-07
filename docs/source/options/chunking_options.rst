Chunking options
^^^^^^^^^^^^^^^^

--segment_duration <seconds>

    Segment duration in seconds. If single_segment is specified, this parameter
    sets the duration of a subsegment; otherwise, this parameter sets the
    duration of a segment. Actual segment durations may not be exactly as
    requested.

--fragment_duration <seconds>

    Fragment duration in seconds. Should not be larger than the segment
    duration. Actual fragment durations may not be exactly as requested.

--segment_sap_aligned

    Force segments to begin with stream access points. Default enabled.

--fragment_sap_aligned

   Force fragments to begin with stream access points. This flag implies
   *segment_sap_aligned*. Default enabled.

--start_segment_number

   Indicates the startNumber in DASH SegmentTemplate and HLS segment name.
