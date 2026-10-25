// ============================================================================
//  SpellChecker.h - the "brain" of SpellCheck Pro.
//
//  It owns the dictionary and stores it in TWO data structures at once:
//
//     HashTable  ->  answers "is this word spelled correctly?"  (one bucket lookup)
//     Trie       ->  answers "which real words look like this one?" (fuzzy search)
//
//  Everything here is plain C++ with no networking: the HTTP layer (Api.cpp /
//  HttpServer.cpp) just calls these functions and turns the results into JSON.
// ============================================================================
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "EditDistance.h"
#include "HashTable.h"
#include "TextUtils.h"
#include "Trie.h"

// ---- suggestions ------------------------------------------------------------
struct Suggestion {
    std::string word;       // already capitalised like the original word
    int distance = 0;       // edit distance to the misspelled word
    int freq = 0;           // how common the suggestion is
    double similarity = 0;  // 0..1  (1 = identical)
};

struct SuggestResult {
    std::vector<Suggestion> suggestions;
    FuzzyStats fuzzy;           // how many trie nodes the search touched
    size_t candidates = 0;      // dictionary words found within the distance limit
    double ms = 0;              // time the search took
};

// ---- spell checking ---------------------------------------------------------
struct CheckOptions {
    int maxSuggestions = 5;
    int maxDistance = 2;
    bool ignoreCaps = true;                    // skip ALL-CAPS words such as NASA
    std::unordered_set<std::string> ignore;    // lower-case words to skip
};

struct MisspelledWord {
    std::string text;
    size_t start = 0;           // UTF-16 index where the word starts
    size_t end = 0;             // UTF-16 index just after the word
    size_t bucket = 0;          // hash bucket the word would live in
    std::string errorType;      // wrong_letter, extra_letter, missing_letter, swapped_letters, multiple, unknown
    SuggestResult result;
};

struct CheckResult {
    size_t bytes = 0;
    size_t words = 0;           // words that were checked
    size_t misspelled = 0;
    size_t ignored = 0;         // skipped because of the ignore list / ALL CAPS
    double accuracy = 100.0;    // percent of checked words that were correct
    double hashNsPerLookup = 0; // measured speed of the hash table
    double trieNsPerLookup = 0; // measured speed of a plain trie lookup (for comparison)
    double suggestMs = 0;       // time spent generating suggestions
    double totalMs = 0;
    std::vector<MisspelledWord> errors;
};

// ---- dictionary explorer ----------------------------------------------------
struct DictionaryQuery {
    std::string q;                  // search text
    std::string mode = "prefix";    // prefix | contains | suffix
    size_t minLen = 0;
    size_t maxLen = 100;
    bool customOnly = false;
    std::string sort = "alpha";     // alpha | freq | length
    bool descending = false;
    size_t page = 1;
    size_t pageSize = 50;
};

struct DictionaryPage {
    size_t total = 0;
    size_t page = 1;
    size_t pageSize = 50;
    size_t pages = 1;
    std::vector<const DictEntry*> items;
};

// ---- benchmark & self test --------------------------------------------------
struct BenchmarkTimes {
    size_t queries = 0;
    double totalMs = 0;
    double nsPerLookup = 0;
};

struct BenchmarkResult {
    size_t hits = 0;       // queries that exist in the dictionary
    size_t misses = 0;     // queries that do not
    BenchmarkTimes hash, trie, linear;
};

struct SelfTestCase {
    std::string typo;
    std::string expected;
    std::string top;       // best suggestion we produced ("" if none)
    int rank = 0;          // position of the expected word in our suggestions (0 = not found)
    bool skipped = false;  // expected word missing from dictionary, or typo is a real word
};

struct SelfTestResult {
    size_t total = 0;
    size_t tested = 0;
    size_t skipped = 0;
    size_t top1 = 0, top3 = 0, top5 = 0;
    double avgMs = 0;
    std::vector<SelfTestCase> cases;
};

struct AddWordResult {
    bool ok = false;
    std::string message;
    std::string word;
};

class SpellChecker {
public:
    // Loads "word<TAB>freq" lines. Returns the number of words added.
    size_t loadDictionary(const std::string& path);
    size_t loadCustomWords(const std::string& path);

    // Adds a user word to the hash table, trie and (if customPath is not empty) to the file.
    AddWordResult addWord(const std::string& word, const std::string& customPath);

    bool isKnownHash(const std::string& lowerWord) const;   // uses the hash table
    bool isKnownTrie(const std::string& lowerWord) const;   // uses the trie

    SuggestResult suggest(const std::string& word, int maxSuggestions, int maxDistance) const;
    CheckResult check(const std::string& text, const CheckOptions& options) const;

    DictionaryPage queryDictionary(const DictionaryQuery& query) const;
    BenchmarkResult runBenchmark(size_t queriesPerKind) const;
    SelfTestResult runSelfTest(const std::string& tsvPath) const;

    // Access for the API layer.
    HashTable& hashTable() { return hash_; }
    const HashTable& hashTable() const { return hash_; }
    const Trie& trie() const { return trie_; }
    const std::vector<DictEntry>& entries() const { return entries_; }
    size_t customCount() const;

private:
    bool addToStructures(const std::string& word, int freq, bool custom);

    HashTable hash_;                 // fast "is it a word?" lookups
    Trie trie_;                      // prefix tree for suggestions
    std::vector<DictEntry> entries_; // all words, alphabetical (for the explorer & linear benchmark)
};
