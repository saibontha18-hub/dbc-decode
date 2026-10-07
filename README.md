# dbc-decode

A small C++ library that parses a useful subset of CAN DBC files (the format
Vector made standard) and decodes raw CAN payloads into named, scaled
signals. Pure host-side code: byte buffers in, engineering values out. No
CAN hardware, no sockets, nothing target-specific.

## What it does

- Parses `BU_`, `BO_`, `SG_`, and `VAL_` lines out of a `.dbc` file
- Extracts signals from raw 8-byte payloads:
  - Intel (little-endian, `@1`) bit layout with sign extension
  - `raw * factor + offset` scaling, min/max and units kept alongside
  - multiplexing markers (`M` / `m3`) parsed on day one, mux-aware decode on day two
  - Motorola (`@0`) signals parsed on day one, decoded on day two (they come
    back `ok=false` for now rather than silently wrong)
- Ships a demo CLI: point it at a DBC and a hex payload, get signals printed

## Build and test

```sh
make          # builds build/test_dbc and build/decode_demo
make test     # runs the self-contained test suite
```

Strict flags throughout: `-Wall -Wextra -Werror -std=c++17 -pedantic`.

## Try it

```sh
./build/decode_demo demo/demo.dbc 0x100 800000A00F5AFF00
```

```
EngineData (0x100, 8 bytes)
  EngineSpeed
    raw=4000  ->  500 rpm
  CoolantTemp
    raw=90  ->  50 degC
  ThrottlePos
    raw=255  ->  100 %
```

## Status

Day one of a two-day build. Day two adds Motorola signal extraction,
multiplexer-aware decoding, and `VAL_` value descriptions in the demo output.

## Why

I work with CAN logs a lot, and every "DBC decoder" I found either wanted a
whole toolchain or choked on perfectly normal files. This one is ~400 lines
of C++ with no dependencies and a parser that skips what it doesn't
understand instead of dying on it.

MIT licensed. Issues and odd DBC files welcome.
