#include "TextUtils.h"

#include <cctype>

namespace text {

// ---------------------------------------------------------------------------
//  Simple case helpers
// ---------------------------------------------------------------------------
std::string toLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool isAllUpper(const std::string& s) {
    bool sawLetter = false;
    for (char c : s) {
        if (c >= 'a' && c <= 'z') return false;
        if (c >= 'A' && c <= 'Z') sawLetter = true;
    }
    return sawLetter;
}

bool startsUpper(const std::string& s) {
    return !s.empty() && s[0] >= 'A' && s[0] <= 'Z';
}

std::string matchCase(const std::string& suggestion, const std::string& original) {
    std::string out = suggestion;
    if (original.size() > 1 && isAllUpper(original)) {
        for (char& c : out) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
    } else if (startsUpper(original) && !out.empty() && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Tokenizer helpers (private to this file)
// ---------------------------------------------------------------------------
namespace {

struct CodePoint {
    unsigned cp;   // the Unicode code point
    size_t len;    // how many UTF-8 bytes it used
};

// Reads one UTF-8 character starting at byte position i.
CodePoint decode(const std::string& s, size_t i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return {c, 1};
    size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    if (len == 1) return {0xFFFD, 1};                       // stray continuation byte
    if (i + len > s.size()) return {0xFFFD, s.size() - i};  // truncated character
    unsigned cp = (len == 2) ? (c & 0x1F) : (len == 3) ? (c & 0x0F) : (c & 0x07);
    for (size_t k = 1; k < len; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    }
    return {cp, len};
}

bool isAsciiLetter(unsigned cp) { return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'); }
bool isAsciiDigit(unsigned cp)  { return cp >= '0' && cp <= '9'; }
bool isApostrophe(unsigned cp)  { return cp == 0x27 || cp == 0x2019; }  // ' and the curly ’

// Punctuation / symbols that separate words (so "word—word" is two words).
bool isSeparator(unsigned cp) {
    if (cp < 0x80) return !isAsciiLetter(cp) && !isAsciiDigit(cp);
    if (cp >= 0xA0 && cp <= 0xBF) return true;       // no-break space, ©, «, ¡ ...
    if (cp == 0xD7 || cp == 0xF7) return true;       // × ÷
    if (cp >= 0x2000 && cp <= 0x206F) return true;   // general punctuation (— “ ” …)
    if (cp >= 0x20A0 && cp <= 0x2BFF) return true;   // currency, arrows, math, symbols
    if (cp >= 0x3000 && cp <= 0x303F) return true;   // CJK punctuation
    if (cp >= 0xFE00 && cp <= 0xFE0F) return true;   // variation selectors
    if (cp >= 0x1F000) return true;                  // emoji
    return false;
}

bool isAsciiSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// Finds whitespace-separated chunks that look like e-mails, URLs or #hashtags.
// Words inside them are not spell-checked.
std::vector<std::pair<size_t, size_t>> findSkipRanges(const std::string& s) {
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isAsciiSpace(s[i])) ++i;
        size_t a = i;
        while (i < s.size() && !isAsciiSpace(s[i])) ++i;
        if (a == i) break;
        std::string chunk = toLower(s.substr(a, i - a));
        bool skip = chunk.find('@') != std::string::npos ||
                    chunk.find("://") != std::string::npos ||
                    chunk.compare(0, 4, "www.") == 0 ||
                    chunk[0] == '#' ||
                    (chunk.find('/') != std::string::npos && chunk.find('.') != std::string::npos);
        if (skip) ranges.emplace_back(a, i);
    }
    return ranges;
}

bool insideAny(const std::vector<std::pair<size_t, size_t>>& ranges, size_t pos) {
    for (const auto& r : ranges) {
        if (pos >= r.first && pos < r.second) return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
//  tokenize
// ---------------------------------------------------------------------------
std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> tokens;
    const auto skipRanges = findSkipRanges(s);

    Token cur;
    bool inToken = false;
    bool foreign = false;   // token contains digits or non-ASCII letters
    size_t i = 0;
    size_t pos16 = 0;       // position in UTF-16 units

    auto begin = [&]() {
        if (!inToken) {
            inToken = true;
            foreign = false;
            cur = Token();
            cur.start = pos16;
            cur.startByte = i;
        }
    };
    auto flush = [&]() {
        if (!inToken) return;
        cur.end = pos16;
        cur.checkable = !foreign && !insideAny(skipRanges, cur.startByte);
        tokens.push_back(cur);
        inToken = false;
    };

    while (i < s.size()) {
        CodePoint c = decode(s, i);
        const size_t units = (c.cp >= 0x10000) ? 2 : 1;

        if (isAsciiLetter(c.cp)) {
            begin();
            cur.text.push_back(static_cast<char>(c.cp));
        } else if (isAsciiDigit(c.cp)) {
            begin();
            foreign = true;
        } else if (isApostrophe(c.cp)) {
            // An apostrophe belongs to the word only if a letter follows: don't, it's
            size_t next = i + c.len;
            bool letterFollows = inToken && next < s.size() && isAsciiLetter(static_cast<unsigned char>(s[next]));
            if (letterFollows) {
                cur.text.push_back('\'');
            } else {
                flush();
            }
        } else if (isSeparator(c.cp)) {
            flush();
        } else {
            // Any other non-ASCII character (é, ü, Cyrillic ...): part of a word we won't check.
            begin();
            foreign = true;
        }

        i += c.len;
        pos16 += units;
    }
    flush();
    return tokens;
}

}  // namespace text
