// ============================================================================
//  Json.h - a tiny helper that builds JSON text.
//
//  The backend only needs to SEND JSON to the browser (requests use plain query
//  parameters or plain text), so a small writer is enough - no external library.
//
//  Example:
//      JsonWriter w;
//      w.beginObject();
//      w.field("name", "teh");              // "name":"teh"
//      w.key("numbers").beginArray();       // "numbers":[
//      w.value(1).value(2);                 //   1,2
//      w.endArray();                        // ]
//      w.endObject();
//      std::string json = w.str();          // {"name":"teh","numbers":[1,2]}
// ============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

class JsonWriter {
public:
    JsonWriter& beginObject() { prepareValue(); out_ += '{'; first_.push_back(true); return *this; }
    JsonWriter& endObject()   { out_ += '}'; first_.pop_back(); return *this; }
    JsonWriter& beginArray()  { prepareValue(); out_ += '['; first_.push_back(true); return *this; }
    JsonWriter& endArray()    { out_ += ']'; first_.pop_back(); return *this; }

    // Starts an object member: writes  "name":  and waits for the value.
    JsonWriter& key(const std::string& name) {
        if (!first_.back()) out_ += ',';
        first_.back() = false;
        writeString(name);
        out_ += ':';
        afterKey_ = true;
        return *this;
    }

    // ---- values ---------------------------------------------------------
    JsonWriter& value(const std::string& s) { prepareValue(); writeString(s); return *this; }
    JsonWriter& value(const char* s)        { return value(std::string(s)); }
    JsonWriter& value(bool b)               { prepareValue(); out_ += b ? "true" : "false"; return *this; }
    JsonWriter& value(int n)                { return writeInteger(static_cast<long long>(n)); }
    JsonWriter& value(long n)               { return writeInteger(static_cast<long long>(n)); }
    JsonWriter& value(long long n)          { return writeInteger(n); }
    JsonWriter& value(unsigned n)           { return writeInteger(static_cast<long long>(n)); }
    JsonWriter& value(unsigned long n)      { return writeInteger(static_cast<long long>(n)); }
    JsonWriter& value(unsigned long long n) { return writeInteger(static_cast<long long>(n)); }
    JsonWriter& value(double d) {
        prepareValue();
        if (!std::isfinite(d)) { out_ += "null"; return *this; }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f", d);
        out_ += buf;
        return *this;
    }
    JsonWriter& nullValue() { prepareValue(); out_ += "null"; return *this; }

    // Shortcut for  key(name).value(v)
    template <typename T>
    JsonWriter& field(const std::string& name, const T& v) { key(name); return value(v); }

    const std::string& str() const { return out_; }

private:
    JsonWriter& writeInteger(long long n) { prepareValue(); out_ += std::to_string(n); return *this; }

    // Inserts the comma between array items / skips it right after a key.
    void prepareValue() {
        if (afterKey_) { afterKey_ = false; return; }
        if (!first_.empty()) {
            if (!first_.back()) out_ += ',';
            first_.back() = false;
        }
    }

    // Writes a JSON string literal with proper escaping.
    void writeString(const std::string& s) {
        out_ += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n";  break;
                case '\r': out_ += "\\r";  break;
                case '\t': out_ += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out_ += buf;
                    } else {
                        out_ += static_cast<char>(c);
                    }
            }
        }
        out_ += '"';
    }

    std::string out_;
    std::vector<bool> first_;   // one entry per open object/array: "no item written yet?"
    bool afterKey_ = false;     // true right after key(): the next value needs no comma
};
