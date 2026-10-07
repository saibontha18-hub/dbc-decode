// test_dbc.cpp - self-contained checks for the day-one parser and decoder.
// No framework; a CHECK that trips prints the line and we count failures.
// Build: make test

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#include "dbc.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n";          \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))

static void ok(const char* name) { std::cout << "ok - " << name << "\n"; }

// A small DBC covering the day-one subset: two Intel signals, one signed,
// one Motorola (decode deferred to day two), mux markers, and a VAL_ table.
static const char* kTestDbc = R"(
VERSION ""

BU_: PCM TCM

BO_ 256 EngineData: 8 PCM
 SG_ EngineSpeed : 24|16@1+ (0.125,0) [0|8000] "rpm" TCM
 SG_ CoolantTemp : 40|8@1- (1,-40) [-40|215] "degC" TCM

BO_ 768 BigEndianMsg: 8 PCM
 SG_ Counter : 7|12@0+ (1,0) [0|4095] "" TCM

BO_ 1024 MuxMsg: 8 PCM
 SG_ Mode M : 0|4@1+ (1,0) [0|15] "" TCM
 SG_ ValueA m1 : 8|8@1+ (1,0) [0|255] "" TCM

VAL_ 256 EngineSpeed 0 "Stopped" 4000 "Cruise" ;
)";

static dbc::Database parse_test_db() {
    dbc::Database db;
    std::string err;
    std::istringstream in(kTestDbc);
    if (!dbc::parse(in, db, err)) {
        std::cerr << "test dbc failed to parse: " << err << "\n";
        std::exit(1);
    }
    return db;
}

static void test_parse_messages() {
    auto db = parse_test_db();
    CHECK(db.messages.size() == 3);
    CHECK(db.messages[0].id == 256);
    CHECK(db.messages[0].name == "EngineData");
    CHECK(db.messages[0].dlc == 8);
    CHECK(db.messages[0].transmitter == "PCM");
    CHECK(db.messages[1].id == 768);
    CHECK(db.find_message(256) != nullptr);
    CHECK(db.find_message(999) == nullptr);
    ok("parse messages");
}

static void test_parse_nodes() {
    auto db = parse_test_db();
    CHECK(db.nodes.size() == 2);
    CHECK(db.nodes[0] == "PCM");
    CHECK(db.nodes[1] == "TCM");
    ok("parse node list");
}

static void test_parse_signal_fields() {
    auto db = parse_test_db();
    const auto& s = db.messages[0].signals[0];
    CHECK(s.name == "EngineSpeed");
    CHECK(s.start_bit == 24);
    CHECK(s.length == 16);
    CHECK(s.intel == true);
    CHECK(s.is_signed == false);
    CHECK_NEAR(s.factor, 0.125, 1e-12);
    CHECK_NEAR(s.offset, 0.0, 1e-12);
    CHECK_NEAR(s.minimum, 0.0, 1e-12);
    CHECK_NEAR(s.maximum, 8000.0, 1e-12);
    CHECK(s.unit == "rpm");
    const auto& t = db.messages[0].signals[1];
    CHECK(t.is_signed == true);
    CHECK_NEAR(t.offset, -40.0, 1e-12);
    CHECK(t.unit == "degC");
    const auto& m = db.messages[1].signals[0];
    CHECK(m.intel == false); // Motorola: parsed, not yet decoded
    ok("parse signal fields");
}

static void test_parse_mux_markers() {
    auto db = parse_test_db();
    const auto& mux = db.messages[2];
    CHECK(mux.signals.size() == 2);
    CHECK(mux.signals[0].mux == "M");
    CHECK(mux.signals[1].mux == "m1");
    ok("parse mux markers");
}

static void test_parse_val_table() {
    auto db = parse_test_db();
    auto it = db.value_tables.find({256, "EngineSpeed"});
    CHECK(it != db.value_tables.end());
    CHECK(it->second.size() == 2);
    CHECK(it->second[0].first == 0);
    CHECK(it->second[0].second == "Stopped");
    CHECK(it->second[1].first == 4000);
    CHECK(it->second[1].second == "Cruise");
    ok("parse VAL_ table");
}

// EngineData payload: 80 00 00 A0 0F 5A FF 00
//   EngineSpeed = bytes 3-4 = 0x0FA0 = 4000 -> 500 rpm
//   CoolantTemp = byte 5 = 0x5A = 90 (signed) -> 50 degC
static void test_decode_engine_data() {
    auto db = parse_test_db();
    uint8_t p[8] = {0x80, 0x00, 0x00, 0xA0, 0x0F, 0x5A, 0xFF, 0x00};
    auto out = dbc::decode_message(db, 256, p, 8);
    CHECK(out.size() == 2);
    CHECK(out[0].name == "EngineSpeed");
    CHECK(out[0].ok);
    CHECK(out[0].raw == 4000);
    CHECK_NEAR(out[0].physical, 500.0, 1e-9);
    CHECK(out[1].name == "CoolantTemp");
    CHECK(out[1].ok);
    CHECK(out[1].raw == 90);
    CHECK_NEAR(out[1].physical, 50.0, 1e-9);
    ok("decode engine data");
}

static void test_decode_signed_negative() {
    auto db = parse_test_db();
    // CoolantTemp byte = 0xEC = -20 signed -> -60 degC
    uint8_t p[8] = {0, 0, 0, 0, 0, 0xEC, 0, 0};
    auto out = dbc::decode_message(db, 256, p, 8);
    CHECK(out[1].ok);
    CHECK(out[1].raw == -20);
    CHECK_NEAR(out[1].physical, -60.0, 1e-9);
    ok("decode signed negative");
}

static void test_decode_motorola_deferred() {
    auto db = parse_test_db();
    uint8_t p[8] = {0xFF, 0xFF, 0, 0, 0, 0, 0, 0};
    auto out = dbc::decode_message(db, 768, p, 8);
    CHECK(out.size() == 1);
    CHECK(!out[0].ok); // day two; must not silently mis-decode
    ok("motorola decode deferred");
}

static void test_decode_bad_inputs() {
    auto db = parse_test_db();
    uint8_t p[8] = {0};
    CHECK(dbc::decode_message(db, 1234, p, 8).empty()); // unknown id
    CHECK(dbc::decode_message(db, 256, p, 3).empty());  // short payload
    ok("decode rejects bad inputs");
}

static void test_parse_garbage() {
    dbc::Database db;
    std::string err;
    std::istringstream in("BO_ not a real line\n");
    CHECK(!dbc::parse(in, db, err));
    CHECK(!err.empty());
    // ...but unknown-but-well-formed lines are skipped, not fatal.
    dbc::Database db2;
    std::string err2;
    std::istringstream in2("VERSION \"x\"\nBA_ \"whatever\" BO_ 1 X;\n");
    CHECK(dbc::parse(in2, db2, err2));
    ok("parse error handling");
}

static void test_parse_demo_file() {
    // The shipped sample has to parse and decode too.
    dbc::Database db;
    std::string err;
    std::ifstream in("demo/demo.dbc");
    CHECK(!!in);
    CHECK(dbc::parse(in, db, err));
    CHECK(db.messages.size() == 2);
    uint8_t p[8] = {0x80, 0x00, 0x00, 0xA0, 0x0F, 0x5A, 0xFF, 0x00};
    auto out = dbc::decode_message(db, 256, p, 8);
    CHECK(out.size() == 3);
    CHECK_NEAR(out[0].physical, 500.0, 1e-9);   // EngineSpeed
    CHECK_NEAR(out[1].physical, 50.0, 1e-9);    // CoolantTemp
    CHECK_NEAR(out[2].physical, 100.0, 1e-6);   // ThrottlePos = 255 * 100/255
    uint8_t q[8] = {0x80, 0x80, 0, 0, 0, 0, 0, 0};
    auto flags = dbc::decode_message(db, 512, q, 8);
    CHECK(flags.size() == 2);
    CHECK(flags[0].raw == 1); // OilPressureLow
    CHECK(flags[1].raw == 1); // CheckEngine
    ok("parse and decode demo.dbc");
}

int main() {
    test_parse_messages();
    test_parse_nodes();
    test_parse_signal_fields();
    test_parse_mux_markers();
    test_parse_val_table();
    test_decode_engine_data();
    test_decode_signed_negative();
    test_decode_motorola_deferred();
    test_decode_bad_inputs();
    test_parse_garbage();
    test_parse_demo_file();

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
