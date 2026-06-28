#pragma once

//
// json.hpp — Compact, dependency-free JSON implementation for MCP.
// Supports: null, bool, number (double/int64), string, array, object.
// Full parser + serializer with pretty-printing.
//

#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace json {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Tag { Null, Bool, Int, Double, String, Array, Object };

class Value {
public:
    Value() : tag_(Tag::Null) {}
    Value(std::nullptr_t) : tag_(Tag::Null) {}
    Value(bool b) : tag_(Tag::Bool), storage_(b) {}
    Value(int v) : tag_(Tag::Int), storage_(static_cast<int64_t>(v)) {}
    Value(int64_t v) : tag_(Tag::Int), storage_(v) {}
    Value(double d) : tag_(Tag::Double), storage_(d) {}
    Value(const char* s) : tag_(Tag::String), storage_(std::string(s)) {}
    Value(std::string s) : tag_(Tag::String), storage_(std::move(s)) {}
    Value(Array a) : tag_(Tag::Array), storage_(std::move(a)) {}
    Value(Object o) : tag_(Tag::Object), storage_(std::move(o)) {}

    static Value make_null()   { return Value(); }
    static Value make_array()  { return Value(Array{}); }
    static Value make_object() { return Value(Object{}); }

    Tag tag() const { return tag_; }

    bool is_null()   const { return tag_ == Tag::Null; }
    bool is_bool()   const { return tag_ == Tag::Bool; }
    bool is_int()    const { return tag_ == Tag::Int; }
    bool is_double() const { return tag_ == Tag::Double; }
    bool is_number() const { return tag_ == Tag::Int || tag_ == Tag::Double; }
    bool is_string() const { return tag_ == Tag::String; }
    bool is_array()  const { return tag_ == Tag::Array; }
    bool is_object() const { return tag_ == Tag::Object; }

    bool as_bool() const { return std::get<bool>(storage_); }

    int64_t as_int() const {
        if (tag_ == Tag::Int)   return std::get<int64_t>(storage_);
        if (tag_ == Tag::Double) return static_cast<int64_t>(std::get<double>(storage_));
        if (tag_ == Tag::Bool)  return std::get<bool>(storage_) ? 1 : 0;
        return 0;
    }

    double as_double() const {
        if (tag_ == Tag::Double) return std::get<double>(storage_);
        if (tag_ == Tag::Int)    return static_cast<double>(std::get<int64_t>(storage_));
        if (tag_ == Tag::Bool)   return std::get<bool>(storage_) ? 1.0 : 0.0;
        return 0.0;
    }

    const std::string& as_string() const { return std::get<std::string>(storage_); }
    std::string& as_string() { return std::get<std::string>(storage_); }
    const Array& as_array() const { return std::get<Array>(storage_); }
    Array& as_array() { return std::get<Array>(storage_); }
    const Object& as_object() const { return std::get<Object>(storage_); }
    Object& as_object() { return std::get<Object>(storage_); }

    // --- Object helpers ---
    bool has(std::string_view key) const {
        return tag_ == Tag::Object &&
               as_object().find(std::string(key)) != as_object().end();
    }

    const Value& at(std::string_view key) const {
        return as_object().at(std::string(key));
    }

    Value& operator[](std::string_view key) {
        if (tag_ != Tag::Object) { tag_ = Tag::Object; storage_ = Object{}; }
        auto& obj = as_object();
        auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            auto [inserted, _] = obj.emplace(std::string(key), Value());
            return inserted->second;
        }
        return it->second;
    }

    const Value& operator[](std::string_view key) const {
        return at(key);
    }

    const Value& get_or(std::string_view key, const Value& fallback) const {
        if (!has(key)) return fallback;
        return at(key);
    }

    // --- Array helpers ---
    size_t size() const {
        if (tag_ == Tag::Array) return as_array().size();
        if (tag_ == Tag::Object) return as_object().size();
        return 0;
    }

    Value& operator[](size_t i) { return as_array()[i]; }
    const Value& operator[](size_t i) const { return as_array()[i]; }

    void push_back(Value v) {
        if (tag_ != Tag::Array) { tag_ = Tag::Array; storage_ = Array{}; }
        as_array().push_back(std::move(v));
    }

    // --- Serializer ---
    std::string dump(int indent = -1) const {
        std::ostringstream ss;
        write(ss, indent >= 0 ? 0 : -1, indent);
        return ss.str();
    }

    // --- Parser ---
    static Value parse(std::string_view text);
    static bool try_parse(std::string_view text, Value& out, std::string& err);

private:
    void write(std::ostringstream& ss, int curIndent, int step) const;
    void write_string(std::ostringstream& ss, const std::string& s) const;

    Tag tag_ = Tag::Null;
    std::variant<std::monostate, bool, int64_t, double,
                 std::string, Array, Object> storage_;
};

// ============================================================
// Serializer implementation
// ============================================================

inline void Value::write_string(std::ostringstream& ss, const std::string& s) const {
    ss << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b";  break;
            case '\f': ss << "\\f";  break;
            case '\n': ss << "\\n";  break;
            case '\r': ss << "\\r";  break;
            case '\t': ss << "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    ss << buf;
                } else {
                    ss << static_cast<char>(c);
                }
        }
    }
    ss << '"';
}

inline void Value::write(std::ostringstream& ss, int curIndent, int step) const {
    auto pad = [&](int level) {
        if (step < 0) return;
        for (int i = 0; i < level * step; ++i) ss << ' ';
    };

    switch (tag_) {
        case Tag::Null:   ss << "null"; break;
        case Tag::Bool:   ss << (as_bool() ? "true" : "false"); break;
        case Tag::Int:    ss << as_int(); break;
        case Tag::Double: {
            double d = as_double();
            if (std::isnan(d) || std::isinf(d)) {
                ss << "null";
            } else {
                // Ensure integer-valued doubles still get a decimal point
                if (d == static_cast<int64_t>(d) && std::abs(d) < 1e17) {
                    ss << static_cast<int64_t>(d);
                } else {
                    ss << d;
                }
            }
            break;
        }
        case Tag::String:
            write_string(ss, as_string());
            break;
        case Tag::Array: {
            const auto& arr = as_array();
            if (arr.empty()) { ss << "[]"; break; }
            ss << '[';
            if (step >= 0) ss << '\n';
            for (size_t i = 0; i < arr.size(); ++i) {
                pad(curIndent + 1);
                arr[i].write(ss, curIndent + 1, step);
                if (i + 1 < arr.size()) ss << ',';
                if (step >= 0) ss << '\n';
            }
            pad(curIndent);
            ss << ']';
            break;
        }
        case Tag::Object: {
            const auto& obj = as_object();
            if (obj.empty()) { ss << "{}"; break; }
            ss << '{';
            if (step >= 0) ss << '\n';
            size_t i = 0;
            for (const auto& [k, v] : obj) {
                pad(curIndent + 1);
                write_string(ss, k);
                ss << (step >= 0 ? ": " : ":");
                v.write(ss, curIndent + 1, step);
                if (++i < obj.size()) ss << ',';
                if (step >= 0) ss << '\n';
            }
            pad(curIndent);
            ss << '}';
            break;
        }
    }
}

// ============================================================
// Parser implementation
// ============================================================

namespace detail {

struct Parser {
    std::string_view src;
    size_t pos = 0;
    std::string error;

    bool ws() {
        while (pos < src.size()) {
            char c = src[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos; continue; }
            if (c == '/' && pos + 1 < src.size()) {
                // Allow comments (non-standard but practical)
                if (src[pos + 1] == '/') {
                    pos += 2;
                    while (pos < src.size() && src[pos] != '\n') ++pos;
                    continue;
                }
                if (src[pos + 1] == '*') {
                    pos += 2;
                    while (pos + 1 < src.size() &&
                           !(src[pos] == '*' && src[pos + 1] == '/')) ++pos;
                    if (pos + 1 < src.size()) pos += 2;
                    continue;
                }
            }
            break;
        }
        return true;
    }

    bool peek(char expected) {
        ws();
        return pos < src.size() && src[pos] == expected;
    }

    bool consume(char expected) {
        ws();
        if (pos < src.size() && src[pos] == expected) { ++pos; return true; }
        return false;
    }

    [[nodiscard]] bool parse_value(Value& out) {
        ws();
        if (pos >= src.size()) { error = "Unexpected end of input"; return false; }
        char c = src[pos];
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == '"') { std::string s; if (!parse_string(s)) return false; out = std::move(s); return true; }
        if (c == 't' || c == 'f') return parse_bool(out);
        if (c == 'n') return parse_null(out);
        return parse_number(out);
    }

    [[nodiscard]] bool parse_string(std::string& out) {
        ws();
        if (pos >= src.size() || src[pos] != '"') { error = "Expected string"; return false; }
        ++pos;
        out.clear();
        while (pos < src.size()) {
            unsigned char c = src[pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= src.size()) { error = "Unterminated escape"; return false; }
                char e = src[pos++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        if (pos + 4 > src.size()) { error = "Bad unicode escape"; return false; }
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            cp <<= 4;
                            char h = src[pos++];
                            if (h >= '0' && h <= '9') cp |= h - '0';
                            else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                            else { error = "Bad hex digit"; return false; }
                        }
                        // Surrogate pair
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos + 6 > src.size() || src[pos] != '\\' || src[pos+1] != 'u') {
                                error = "Missing low surrogate"; return false;
                            }
                            pos += 2;
                            unsigned cp2 = 0;
                            for (int i = 0; i < 4; ++i) {
                                cp2 <<= 4;
                                char h = src[pos++];
                                if (h >= '0' && h <= '9') cp2 |= h - '0';
                                else if (h >= 'a' && h <= 'f') cp2 |= h - 'a' + 10;
                                else if (h >= 'A' && h <= 'F') cp2 |= h - 'A' + 10;
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (cp2 - 0xDC00);
                        }
                        // Encode as UTF-8
                        if (cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else if (cp < 0x10000) {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xF0 | (cp >> 18));
                            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: error = "Bad escape character"; return false;
                }
            } else {
                out += static_cast<char>(c);
            }
        }
        error = "Unterminated string";
        return false;
    }

    [[nodiscard]] bool parse_number(Value& out) {
        ws();
        size_t start = pos;
        bool is_double = false;
        if (pos < src.size() && src[pos] == '-') ++pos;
        while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        if (pos < src.size() && src[pos] == '.') { is_double = true; ++pos;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos; }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            is_double = true; ++pos;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        if (pos == start) { error = "Expected number"; return false; }
        std::string numstr(src.substr(start, pos - start));
        try {
            if (is_double) {
                out = std::stod(numstr);
            } else {
                out = static_cast<int64_t>(std::stoll(numstr));
            }
        } catch (...) {
            // Fallback to double for very large numbers
            try { out = std::stod(numstr); }
            catch (...) { error = "Invalid number: " + numstr; return false; }
        }
        return true;
    }

    [[nodiscard]] bool parse_bool(Value& out) {
        if (src.substr(pos, 4) == "true")  { pos += 4; out = true;  return true; }
        if (src.substr(pos, 5) == "false") { pos += 5; out = false; return true; }
        error = "Expected true/false"; return false;
    }

    [[nodiscard]] bool parse_null(Value& out) {
        if (src.substr(pos, 4) == "null") { pos += 4; out = nullptr; return true; }
        error = "Expected null"; return false;
    }

    [[nodiscard]] bool parse_array(Value& out) {
        ++pos; // consume '['
        Array arr;
        ws();
        if (peek(']')) { ++pos; out = std::move(arr); return true; }
        for (;;) {
            Value v;
            if (!parse_value(v)) return false;
            arr.push_back(std::move(v));
            ws();
            if (consume(',')) continue;
            if (consume(']')) break;
            error = "Expected ',' or ']'"; return false;
        }
        out = std::move(arr);
        return true;
    }

    [[nodiscard]] bool parse_object(Value& out) {
        ++pos; // consume '{'
        Object obj;
        ws();
        if (peek('}')) { ++pos; out = std::move(obj); return true; }
        for (;;) {
            std::string key;
            if (!parse_string(key)) return false;
            if (!consume(':')) { error = "Expected ':'"; return false; }
            Value v;
            if (!parse_value(v)) return false;
            obj[std::move(key)] = std::move(v);
            ws();
            if (consume(',')) continue;
            if (consume('}')) break;
            error = "Expected ',' or '}'"; return false;
        }
        out = std::move(obj);
        return true;
    }
};

} // namespace detail

inline Value Value::parse(std::string_view text) {
    detail::Parser p{text, 0, {}};
    Value result;
    if (!p.parse_value(result)) {
        throw std::runtime_error("JSON parse error at offset " +
            std::to_string(p.pos) + ": " + p.error);
    }
    return result;
}

inline bool Value::try_parse(std::string_view text, Value& out, std::string& err) {
    detail::Parser p{text, 0, {}};
    if (!p.parse_value(out)) { err = p.error; return false; }
    return true;
}

} // namespace json
