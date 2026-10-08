// dbc_decode.cpp - pull signals out of raw CAN payloads.
//
// Intel (little-endian) signals are the easy case: the start bit IS the LSB
// of the signal and the bits just march upward through the payload, LSB
// first. Motorola (@0) signals use DBC's sawtooth numbering: the start bit
// is the signal's MSB and the bits walk down through the frame, wrapping
// from the bottom of one byte to the top of the next.

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

// Extract a Motorola (big-endian) signal. In DBC's sawtooth numbering the
// start bit is the signal MSB; signal bit p (0 = MSB) sits at frame bit
// (start - p) while we stay inside the starting byte, then wraps to the
// top of the next byte. Returns false if the signal reaches past the
// payload instead of reading out of bounds.
bool extract_motorola(const uint8_t* payload, size_t plen, int start, int len,
                      uint64_t& raw) {
    raw = 0;
    int byte = start / 8; // byte holding the MSB
    int r = start % 8;    // bit index of the MSB within that byte
    for (int p = 0; p < len; ++p) {
        int fbit; // frame bit holding signal bit p
        if (p <= r) {
            fbit = byte * 8 + (r - p);
        } else {
            int q = p - (r + 1);
            int b2 = byte + 1 + q / 8;
            int r2 = 7 - (q % 8);
            fbit = b2 * 8 + r2;
        }
        if (fbit >= (int)plen * 8)
            return false;
        if ((payload[fbit / 8] >> (fbit % 8)) & 1u)
            raw |= 1ull << (len - 1 - p);
    }
    return true;
}

// Sign-extend a raw value of `len` bits to int64.
int64_t sign_extend(uint64_t raw, int len) {
    if (len < 64 && ((raw >> (len - 1)) & 1u))
        return (int64_t)(raw | (~0ull << len));
    return (int64_t)raw;
}

// "m3" -> 3. Garbage in -> -1 (treated as always-applicable by the caller).
int mux_selector(const std::string& mux) {
    if (mux.empty() || mux[0] != 'm')
        return -1;
    int val = 0;
    size_t i = 1;
    for (; i < mux.size() && mux[i] >= '0' && mux[i] <= '9'; ++i)
        val = val * 10 + (mux[i] - '0');
    return (i == 1) ? -1 : val;
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

    // Decode everything first, keeping the signal next to its result: the
    // multiplexer switch can sit after its multiplexed signals in the DBC.
    struct Entry {
        const Signal* sig;
        DecodedSignal dec;
    };
    std::vector<Entry> all;
    for (const auto& sig : msg->signals) {
        DecodedSignal d;
        d.name = sig.name;
        d.unit = sig.unit;

        uint64_t raw = 0;
        bool fits;
        if (sig.intel) {
            fits = sig.start_bit + sig.length <= (int)len * 8;
            if (fits)
                raw = extract_intel(payload, sig.start_bit, sig.length);
        } else {
            fits = extract_motorola(payload, len, sig.start_bit, sig.length,
                                    raw);
        }
        if (!fits) {
            d.ok = false; // signal runs past the end of this payload
            all.push_back({&sig, d});
            continue;
        }

        int64_t sval = sig.is_signed ? sign_extend(raw, sig.length)
                                     : (int64_t)raw;
        d.raw = sval;
        d.physical = sval * sig.factor + sig.offset;
        d.ok = true;
        all.push_back({&sig, d});
    }

    // The multiplexer switch ("M") decides which multiplexed signals apply.
    bool have_switch = false;
    int64_t switch_value = 0;
    for (const auto& e : all) {
        if (e.sig->mux == "M" && e.dec.ok) {
            have_switch = true;
            switch_value = e.dec.raw;
            break;
        }
    }

    for (const auto& e : all) {
        if (!e.sig->mux.empty() && e.sig->mux[0] == 'm' && have_switch) {
            int sel = mux_selector(e.sig->mux);
            if (sel >= 0 && sel != switch_value)
                continue; // not selected by the switch: leave it out
        }
        out.push_back(e.dec);
    }
    return out;
}

std::string value_description(const Database& db, uint32_t id,
                              const std::string& signal, int64_t raw) {
    auto it = db.value_tables.find({id, signal});
    if (it == db.value_tables.end())
        return "";
    for (const auto& pair : it->second) {
        if (pair.first == raw)
            return pair.second;
    }
    return "";
}

} // namespace dbc
