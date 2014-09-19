# Wesgr

Wesgr is a [Weston](http://wayland.freedesktop.org/)
JSON timeline data interpreter and grapher.
Its intention is to produce an SVG image with annotations,
describing the actions related to Weston's repaint loop.

The Weston JSON timeline format is still in development,
but there is a [WIP branch](http://cgit.collabora.com/git/user/pq/weston.git/log/?h=JSON-timeline-WIP).

## Building

No autotools yet, so just do `make`. There is no target
for installing.

## Running

    ./wesgr -i testdata/timeline-1.log -o graph.svg

It creates `graph.svg`.

## Example output

This is a recording from Weston's DRM backend with two outputs.
You can find the input data as `testdata/timeline-3.log`, and you can
generate these with `make demo`.

An overview of the whole recording, as PNG because the SVG is 960 kB:
![Example output](http://ppaalanen.github.io/wesgr/examples/sample3-overview.png
"The whole recording")

A sub-range of the recording:
![Example output](http://ppaalanen.github.io/wesgr/examples/sample3-detail.svg
"A subset of the recording")
