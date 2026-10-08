# dbc-decode

A small C++ library that parses a useful subset of CAN DBC files (the format
Vector made standard) and decodes raw CAN payloads into named, scaled
signals. Pure host-side code: byte buffers in, engineering values out. No
CAN hardware, no sockets, nothing target-specific.

## What it does

- Parses `BU_`, `BO_`, `SG_`, and `VAL_` lines out of a `.dbc` file.
  Everything else in the file (attributes, comments, the `NS_` phone book)
  is skipped instead of dying on it — real DBC exports are messy.
- Extracts signals from raw 8-byte payloads:
  - Intel (little-endian, `@1`) and Motorola (big-endian, `@0`) layouts,
    including Motorola signals that cross byte boundaries, with sign
    extension for both
  - `raw * factor + offset` scaling, min/max and units kept alongside
  - multiplexer-aware decoding: multiplexed signals (`m1`, `m2`, ...) are
    only returned when the switch (`M`) in the same frame selects them
  - signals that reach past the payload come back `ok=false` instead of
    reading out of bounds
- Resolves `VAL_` value descriptions ("Cruise", "Stopped") for decoded raw
  values, and prints them in the demo CLI

## Build and test

```sh
make          # builds build/test_dbc and build/decode_demo
make test     # runs the self-contained test suite
```

Strict flags throughout: `-Wall -Wextra -Werror -std=c++17 -pedantic`.
14 test groups, currently 99 checks, zero failures.

## Try it

```sh
./build/decode_demo demo/demo.dbc 0x100 800000A00F5AFF00
```

```
EngineData (0x100, 8 bytes)
  EngineSpeed
    raw=4000  ->  500 rpm  (Cruise)
  CoolantTemp
    raw=90  ->  50 degC
  ThrottlePos
    raw=255  ->  100 %
```

Multiplexed frame — the switch picks which signal is on the wire:

```sh
./build/decode_demo demo/demo.dbc 0x300 02C8000000000000
```

```
BodyControl (0x300, 8 bytes)
  WindowSelect
    raw=2  ->  2
  FrontRightPos
    raw=200  ->  100 mm
```

(`FrontLeftPos` belongs to selector value 1, so it stays out of the result —
same convention cantools uses.)

## Layout notes

The two endiannesses are genuinely different beasts. Intel signals are the
easy case: the DBC start bit *is* the LSB and bits march upward. Motorola
uses the sawtooth numbering where the start bit is the signal's MSB, and
bits walk down through the frame, wrapping from the bottom of one byte to
the top of the next. Getting that wrap wrong gives you plausible-looking
garbage, which is why the test suite has dedicated cross-byte cases
(`4|9@0+` over a byte boundary, signed Motorola, and a signal that runs
off the end of the payload).

## Why

I work with CAN logs a lot, and every "DBC decoder" I found either wanted a
whole toolchain or choked on perfectly normal files. This one is ~600 lines
of C++ with no dependencies and a parser that skips what it doesn't
understand instead of dying on it.

MIT licensed. Issues and odd DBC files welcome.
