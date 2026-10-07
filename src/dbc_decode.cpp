// dbc_decode.cpp - pull signals out of raw CAN payloads.
//
// Intel (little-endian) signals are the easy case: the start bit IS the LSB
// of the signal and the bits just march upward through the payload, LSB
// first. Motorola (@0) signals use DBC's sawtooth numbering and get their
// own decoder on day two; for now they come back ok=false rather than wrong.

#include "dbc.h"

namespace dbc {
namespace {

// Extract a little-endian signal. start_bit is the LSB, bits run
// (start, start+1, ...) and land in the raw value LSB-first.
uint64_t extract_intel(const uint8_t* payload, int start, int len) {
    uint64_t raw = 0;
    for (int i = 0; i < len; ++i) {
        int bit = start + i;
        if ((payload[bit / 8] >> (bit % 8)) & 1u)
            raw |= 1ull << i;
    }
    return raw;
}

} // namespace

std::vector<DecodedSignal> decode_message(const Database& db, uint32_t id,
                                          const uint8_t* payload, size_t len) {
    std::vector<DecodedSignal> out;
    const Message* msg = db.find_message(id);
    if (!msg)
        return out; // unknown id: nothing to decode, not an error
    if ((int)len < msg->dlc)
        return out; // short payload: refuse rather than read past the end

    for (const auto& sig : msg->signals) {
        DecodedSignal d;
        d.name = sig.name;
        d.unit = sig.unit;

        if (!sig.intel) {
            // Motorola decoding lands on day two.
            d.ok = false;
            out.push_back(d);
            continue;
        }
        if (sig.start_bit + sig.length > (int)len * 8) {
            d.ok = false; // signal runs past the end of this payload
            out.push_back(d);
            continue;
        }

        uint64_t raw = extract_intel(payload, sig.start_bit, sig.length);
        int64_t sval = (int64_t)raw;
        if (sig.is_signed && sig.length < 64 &&
            (raw >> (sig.length - 1)) & 1u) {
            // Sign-extend from the signal width.
            sval = (int64_t)(raw | (~0ull << sig.length));
        }
        d.raw = sval;
        d.physical = sval * sig.factor + sig.offset;
        d.ok = true;
        out.push_back(d);
    }
    return out;
}

} // namespace dbc
