# Wesgr

Wesgr is a [Weston](http://wayland.freedesktop.org/)
JSON timeline data interpreter and grapher.
Its intention is to produce an SVG image with annotations,
describing the actions related to Weston's repaint loop.

The Weston JSON timeline format is still in development
and the Weston branch producing it is not yet public.

## Building

No autotools yet, so just do `make`. There is no target
for installing.

## Running

    ./wesgr testdata/timeline-1.log

It creates `graph.svg`.

## Example output

[Example output as SVG code](examples/graph.svg)

<object data="examples/graph.svg?raw=true" type="image/svg+xml">Embedding
SVG image did not work here.</object>

