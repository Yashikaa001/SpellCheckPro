// ============================================================================
//  TextUtils.h - small text helpers: lower-casing, case matching and the
//  TOKENIZER that splits a paragraph into words.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace text {

// "Hello" -> "hello"   (ASCII only; the dictionary is plain a-z)
std::string toLower(const std::string& s);

// true for "NASA", false for "Nasa" / "nasa" / "123"
bool isAllUpper(const std::string& s);

// true for "Hello", false for "hello"
bool startsUpper(const std::string& s);

// Gives `suggestion` the same capitalisation style as `original`:
//   original "Teh"  + suggestion "the" -> "The"
//   original "TEH"  + suggestion "the" -> "THE"
std::string matchCase(const std::string& suggestion, const std::string& original);

// One word found inside the input text.
struct Token {
    std::string text;        // the word as typed (curly apostrophes become ')
    size_t start = 0;        // where it starts, in UTF-16 units (= JavaScript string index)
    size_t end = 0;          // one past its last character (UTF-16 units)
    size_t startByte = 0;    // where it starts in the UTF-8 input (internal use)
    bool checkable = true;   // false for words with digits / accents, e-mail addresses, URLs ...
};

// Splits UTF-8 text into words.
//  * A word is a run of letters; an apostrophe is allowed INSIDE a word ("don't").
//  * Positions are reported in UTF-16 units so the browser can highlight exact ranges
//    even when the text contains emojis or accented characters.
//  * Tokens that should not be spell-checked (contain digits or non-English letters,
//    look like an e-mail / URL / #hashtag) are returned with checkable = false.
std::vector<Token> tokenize(const std::string& utf8);

}  // namespace text
