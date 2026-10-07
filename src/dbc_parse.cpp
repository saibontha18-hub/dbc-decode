// dbc_parse.cpp - line-oriented parser for the DBC subset we care about.
//
// We only need: BU_ (node list), BO_ (message frames), SG_ (signal layout)
// and VAL_ (value descriptions). Everything else -- NS_, BS_, attributes,
// comments -- is skipped. DBC files in the wild are messy, so unknown lines
// never fail the parse; only a line that *claims* to be BO_/SG_/VAL_ but
// doesn't scan right is an error.

#include "dbc.h"

#include <cctype>
#include <sstream>

namespace dbc {
namespace {

// Drop leading/trailing whitespace.
std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a]))
        ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1]))
        --b;
    return s.substr(a, b - a);
}

// Split on whitespace; quoted sections stay glued together.
// "a \"b c\" d" -> ["a", "\"b c\"", "d"]
std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;
    for (char c : s) {
        if (c == '"')
            in_quotes = !in_quotes;
        if (std::isspace((unsigned char)c) && !in_quotes) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

// Split on a single delimiter, trimming each piece.
std::vector<std::string> split_on(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(cur));
    return out;
}

// "24|16@1+" -> start=24, len=16, intel=true, sign='+'. Returns false if
// the field doesn't scan.
bool parse_layout(const std::string& tok, int& start, int& len, bool& intel,
                  char& sign) {
    int order = 1;
    if (std::sscanf(tok.c_str(), "%d|%d@%d%c", &start, &len, &order, &sign) != 4)
        return false;
    if (len <= 0 || len > 64 || start < 0 || start >= 64)
        return false;
    if (order != 0 && order != 1)
        return false;
    if (sign != '+' && sign != '-')
        return false;
    intel = (order == 1);
    return true;
}

// "(0.125,0)" -> factor=0.125, offset=0
bool parse_factor_offset(const std::string& tok, double& factor,
                         double& offset) {
    if (tok.size() < 5 || tok.front() != '(' || tok.back() != ')')
        return false;
    auto parts = split_on(tok.substr(1, tok.size() - 2), ',');
    if (parts.size() != 2)
        return false;
    try {
        factor = std::stod(parts[0]);
        offset = std::stod(parts[1]);
    } catch (...) {
        return false;
    }
    return true;
}

// "[0|8000]" -> min=0, max=8000
bool parse_range(const std::string& tok, double& lo, double& hi) {
    if (tok.size() < 5 || tok.front() != '[' || tok.back() != ']')
        return false;
    auto parts = split_on(tok.substr(1, tok.size() - 2), '|');
    if (parts.size() != 2)
        return false;
    try {
        lo = std::stod(parts[0]);
        hi = std::stod(parts[1]);
    } catch (...) {
        return false;
    }
    return true;
}

// Parse one BO_ line into db->messages.
bool parse_message(const std::string& line, Database& db, std::string& err) {
    // BO_ 256 EngineData: 8 PCM
    auto words = split_ws(line);
    if (words.size() < 4 || words[0] != "BO_") {
        err = "bad BO_ line";
        return false;
    }
    Message m;
    try {
        m.id = (uint32_t)std::stoul(words[1]);
    } catch (...) {
        err = "bad message id";
        return false;
    }
    m.name = words[2];
    if (!m.name.empty() && m.name.back() == ':')
        m.name.pop_back();
    else {
        err = "expected ':' after message name";
        return false;
    }
    try {
        m.dlc = std::stoi(words[3]);
    } catch (...) {
        err = "bad DLC";
        return false;
    }
    if (words.size() > 4)
        m.transmitter = words[4];
    db.messages.push_back(m);
    return true;
}

// Parse one SG_ line into the most recent message.
bool parse_signal(const std::string& line, Database& db, std::string& err) {
    if (db.messages.empty()) {
        err = "SG_ before any BO_";
        return false;
    }
    // SG_ EngineSpeed : 24|16@1+ (0.125,0) [0|8000] "rpm" TCM
    // SG_ Foo m2 : 0|8@1+ (1,0) [0|255] "" TCM   (multiplexed)
    // SG_ Mode M : 8|4@1+ (1,0) [0|15] "" TCM    (multiplexor switch)
    std::string rest = trim(line.substr(3));
    if (rest.empty()) {
        err = "empty SG_ line";
        return false;
    }

    Signal sig;
    size_t pos = 0;
    while (pos < rest.size() && !std::isspace((unsigned char)rest[pos]))
        ++pos;
    sig.name = rest.substr(0, pos);
    rest = trim(rest.substr(pos));

    // Optional multiplexing marker before the colon.
    if (!rest.empty() && (rest[0] == 'm' || rest[0] == 'M')) {
        size_t end = 0;
        while (end < rest.size() && !std::isspace((unsigned char)rest[end]))
            ++end;
        sig.mux = rest.substr(0, end);
        rest = trim(rest.substr(end));
    }

    if (rest.empty() || rest[0] != ':') {
        err = "expected ':' in SG_ line for " + sig.name;
        return false;
    }
    rest = trim(rest.substr(1));

    auto words = split_ws(rest);
    if (words.size() < 4) {
        err = "not enough fields in SG_ line for " + sig.name;
        return false;
    }
    char sign = '+';
    if (!parse_layout(words[0], sig.start_bit, sig.length, sig.intel, sign)) {
        err = "bad signal layout in SG_ line for " + sig.name;
        return false;
    }
    sig.is_signed = (sign == '-');
    if (!parse_factor_offset(words[1], sig.factor, sig.offset)) {
        err = "bad factor/offset in SG_ line for " + sig.name;
        return false;
    }
    if (!parse_range(words[2], sig.minimum, sig.maximum)) {
        err = "bad min/max in SG_ line for " + sig.name;
        return false;
    }
    // Unit is the quoted token; strip the quotes (it may be "").
    std::string unit = words[3];
    if (unit.size() >= 2 && unit.front() == '"' && unit.back() == '"')
        unit = unit.substr(1, unit.size() - 2);
    sig.unit = unit;

    db.messages.back().signals.push_back(sig);
    return true;
}

// Parse one VAL_ line into db->value_tables.
// VAL_ 256 EngineSpeed 0 "Stopped" 4000 "Cruise" ;
bool parse_val(const std::string& line, Database& db, std::string& err) {
    auto words = split_ws(line);
    if (words.size() < 4 || words[0] != "VAL_") {
        err = "bad VAL_ line";
        return false;
    }
    uint32_t id;
    try {
        id = (uint32_t)std::stoul(words[1]);
    } catch (...) {
        err = "bad VAL_ message id";
        return false;
    }
    std::string sig_name = words[2];
    std::vector<std::pair<int64_t, std::string>> pairs;
    for (size_t i = 3; i < words.size(); ++i) {
        if (words[i] == ";")
            break;
        int64_t value;
        try {
            value = std::stoll(words[i]);
        } catch (...) {
            err = "bad VAL_ value";
            return false;
        }
        if (i + 1 >= words.size()) {
            err = "VAL_ value without description";
            return false;
        }
        std::string desc = words[++i];
        if (desc.size() >= 2 && desc.front() == '"' && desc.back() == '"')
            desc = desc.substr(1, desc.size() - 2);
        pairs.push_back({value, desc});
    }
    db.value_tables[{id, sig_name}] = pairs;
    return true;
}

} // namespace

bool parse(std::istream& in, Database& db, std::string& err) {
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty())
            continue;
        if (line.rfind("BU_:", 0) == 0) {
            auto words = split_ws(line);
            for (size_t i = 1; i < words.size(); ++i)
                db.nodes.push_back(words[i]);
        } else if (line.rfind("BO_ ", 0) == 0) {
            std::string why;
            if (!parse_message(line, db, why)) {
                err = "line " + std::to_string(lineno) + ": " + why;
                return false;
            }
        } else if (line.rfind("SG_ ", 0) == 0) {
            std::string why;
            if (!parse_signal(line, db, why)) {
                err = "line " + std::to_string(lineno) + ": " + why;
                return false;
            }
        } else if (line.rfind("VAL_ ", 0) == 0) {
            std::string why;
            if (!parse_val(line, db, why)) {
                err = "line " + std::to_string(lineno) + ": " + why;
                return false;
            }
        }
        // VERSION, NS_, BS_, attributes, comments: not our problem.
    }
    return true;
}

const Message* Database::find_message(uint32_t id) const {
    for (const auto& m : messages) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

} // namespace dbc
