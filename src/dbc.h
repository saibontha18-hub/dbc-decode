// dbc.h - tiny CAN DBC decoder: parse a useful subset of Vector DBC files
// and decode raw CAN payloads into named, scaled signals.
//
// No hardware, no sockets, no threads. Byte buffers in, numbers out.
// Written from scratch against a couple of sample DBC exports.
#ifndef DBC_DECODE_DBC_H
#define DBC_DECODE_DBC_H

#include <cstdint>
#include <istream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dbc {

struct Signal {
    std::string name;
    int start_bit = 0;   // DBC start bit, sawtooth numbering within the frame
    int length = 0;      // signal width in bits
    bool intel = true;   // true = little-endian (@1), false = Motorola (@0)
    bool is_signed = false;
    double factor = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    std::string unit;
    // "" = plain signal, "M" = multiplexor switch, "m3" = multiplexed (switch value 3)
    std::string mux;
};

struct Message {
    uint32_t id = 0;
    std::string name;
    int dlc = 8;
    std::string transmitter;
    std::vector<Signal> signals;
};

struct Database {
    std::vector<Message> messages;
    std::vector<std::string> nodes;

    // Value descriptions from VAL_ lines, keyed by (message id, signal name).
    std::map<std::pair<uint32_t, std::string>,
             std::vector<std::pair<int64_t, std::string>>>
        value_tables;

    const Message* find_message(uint32_t id) const;
};

struct DecodedSignal {
    std::string name;
    int64_t raw = 0;       // raw value after sign extension
    double physical = 0.0; // raw * factor + offset
    std::string unit;
    bool ok = false;
};

// Parse a DBC file from a stream. Returns false on malformed BO_/SG_/VAL_
// lines (with `err` set to something useful). Lines we don't understand are
// skipped rather than fatal -- DBC files carry a lot of stuff we don't need.
bool parse(std::istream& in, Database& db, std::string& err);

// Decode every signal of one message from a raw payload.
// Unknown id or payload shorter than the message DLC -> empty vector.
// Multiplexer-aware: multiplexed signals ("m3") are only returned when the
// multiplexer switch ("M") in the same message decodes to their selector
// value; the switch itself is always returned.
std::vector<DecodedSignal> decode_message(const Database& db, uint32_t id,
                                          const uint8_t* payload, size_t len);

// VAL_ lookup: the description for `raw` of `signal` in message `id`,
// or "" when the table has no entry for it.
std::string value_description(const Database& db, uint32_t id,
                              const std::string& signal, int64_t raw);

} // namespace dbc

#endif
