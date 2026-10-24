#include "model/json.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace rp::json {

const Value* Value::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : obj) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

namespace {

class Parser {
public:
    Parser(const std::string& text, int maxDepth) : s_(text), maxDepth_(maxDepth) {}

    Value document() {
        Value v = value(0);
        ws();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    size_t i_ = 0;
    int maxDepth_;

    [[noreturn]] void fail(const char* what) const {
        throw Error(std::string("replay.json: ") + what + " at byte " + std::to_string(i_));
    }

    void ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) ++i_;
    }

    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }

    Value value(int depth) {
        if (depth > maxDepth_) fail("nesting too deep");
        ws();
        if (i_ >= s_.size()) fail("unexpected end");
        Value v;
        const char c = s_[i_];
        if (c == '{') {
            v.type = Value::Type::Object;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') {
                ++i_;
                return v;
            }
            for (;;) {
                ws();
                if (i_ >= s_.size() || s_[i_] != '"') fail("expected a key");
                std::string key = string();
                ws();
                if (i_ >= s_.size() || s_[i_] != ':') fail("expected ':'");
                ++i_;
                v.obj.emplace_back(std::move(key), value(depth + 1));
                ws();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == '}') {
                    ++i_;
                    return v;
                }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Value::Type::Array;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') {
                ++i_;
                return v;
            }
            for (;;) {
                v.arr.push_back(value(depth + 1));
                ws();
                if (i_ < s_.size() && s_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < s_.size() && s_[i_] == ']') {
                    ++i_;
                    return v;
                }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            v.str = string();
            return v;
        }
        if (literal("true")) {
            v.type = Value::Type::Bool;
            v.b = true;
            return v;
        }
        if (literal("false")) {
            v.type = Value::Type::Bool;
            return v;
        }
        if (literal("null")) return v;
        if (c == '-' || (c >= '0' && c <= '9')) {
            const size_t start = i_;
            if (s_[i_] == '-') ++i_;
            while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.' || s_[i_] == 'e' ||
                                      s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-')) {
                ++i_;
            }
            const std::string tok = s_.substr(start, i_ - start);
            char* end = nullptr;
            errno = 0;
            const double d = std::strtod(tok.c_str(), &end);
            if (end != tok.c_str() + tok.size() || !std::isfinite(d)) fail("bad number");
            v.type = Value::Type::Number;
            v.num = d;
            return v;
        }
        fail("unexpected character");
    }

    void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s_[i_++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= unsigned(h - '0');
            else if (h >= 'a' && h <= 'f') v |= unsigned(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= unsigned(h - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    std::string string() {
        ++i_;  // the opening quote
        std::string out;
        for (;;) {
            if (i_ >= s_.size()) fail("unterminated string");
            const char c = s_[i_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= s_.size()) fail("unterminated string");
            const char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xd800 && cp < 0xdc00 && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        i_ += 2;
                        const unsigned lo = hex4();
                        if (lo >= 0xdc00 && lo < 0xe000) cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                        else cp = 0xfffd;
                    } else if (cp >= 0xd800 && cp < 0xe000) {
                        cp = 0xfffd;
                    }
                    utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }
};

}  // namespace

Value parse(const std::string& text, int maxDepth) { return Parser(text, maxDepth).document(); }

}  // namespace rp::json
