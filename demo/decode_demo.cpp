// decode_demo.cpp - point this at a DBC file and a raw CAN payload,
// get human-readable signals back out.
//
// usage: decode_demo <file.dbc> <id> <payload-hex>
//   id like 0x100 or 256, payload like 800000A00F5AFF00 (8 bytes, hex)

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "dbc.h"

namespace {

bool parse_hex(const std::string& s, uint8_t* out, size_t& len) {
    if (s.size() % 2 != 0 || s.size() > 16)
        return false;
    len = s.size() / 2;
    for (size_t i = 0; i < len; ++i) {
        if (!std::isxdigit((unsigned char)s[2 * i]) ||
            !std::isxdigit((unsigned char)s[2 * i + 1]))
            return false;
        out[i] =
            (uint8_t)std::stoul(s.substr(2 * i, 2), nullptr, 16);
    }
    return true;
}

uint32_t parse_id(const std::string& s) {
    int base = 10;
    std::string t = s;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        t = t.substr(2);
    }
    return (uint32_t)std::stoul(t, nullptr, base);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: decode_demo <file.dbc> <id> <payload-hex>\n";
        return 2;
    }

    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "can't open " << argv[1] << "\n";
        return 2;
    }
    dbc::Database db;
    std::string err;
    if (!dbc::parse(in, db, err)) {
        std::cerr << "parse failed: " << err << "\n";
        return 1;
    }

    uint32_t id = parse_id(argv[2]);
    uint8_t payload[8];
    size_t len = 0;
    if (!parse_hex(argv[3], payload, len)) {
        std::cerr << "payload must be up to 8 bytes of hex\n";
        return 2;
    }

    const dbc::Message* msg = db.find_message(id);
    if (!msg) {
        std::cerr << "id 0x" << std::hex << id << std::dec
                  << " not in " << argv[1] << "\n";
        return 1;
    }

    auto decoded = dbc::decode_message(db, id, payload, len);
    std::cout << msg->name << " (0x" << std::hex << id << std::dec << ", "
              << len << " bytes)\n";
    for (const auto& d : decoded) {
        if (!d.ok) {
            std::cout << "  " << d.name << ": <decode not supported yet>\n";
            continue;
        }
        char phys[64];
        std::snprintf(phys, sizeof(phys), "%g", d.physical);
        std::cout << "  " << d.name << "\n"
                  << "    raw=" << d.raw << "  ->  " << phys;
        if (!d.unit.empty())
            std::cout << " " << d.unit;
        std::cout << "\n";
    }
    return 0;
}
