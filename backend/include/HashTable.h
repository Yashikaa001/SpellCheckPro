// ============================================================================
//  HashTable.h - a hash table written from scratch (separate chaining).
//
//  HOW IT WORKS (the short version)
//  --------------------------------
//  1. We have an array of "buckets" (think: numbered drawers).
//  2. A HASH FUNCTION turns a word into a big number:  "hello" -> 261238937
//  3. number % bucketCount tells us which drawer the word lives in.
//  4. Looking a word up = compute the hash, open ONE drawer, compare a few words.
//     That is why it is fast: we never scan the whole dictionary.
//
//  COLLISIONS
//  ----------
//  Two different words can land in the same bucket (a "collision"). We handle that
//  with SEPARATE CHAINING: every bucket holds a small list (the "chain") of all the
//  words that hash to it. A good hash function keeps the chains short.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The hash functions you can pick from the dashboard.
enum class HashFunction {
    DJB2,    // h = h*33 + c       (classic, good spread)
    FNV1A,   // h = (h ^ c) * 16777619   (very good spread)
    SDBM,    // h = c + (h<<6) + (h<<16) - h
    SUM      // h = h + c          (deliberately BAD: anagrams collide, values stay small)
};

const char* hashFunctionName(HashFunction f);                       // "djb2", "fnv1a", ...
bool parseHashFunction(const std::string& name, HashFunction& out); // name -> enum
std::string hashFunctionFormula(HashFunction f);                    // human readable formula

// One step of the hash computation (used to show the calculation in the UI).
struct HashStep {
    char ch;
    uint32_t before;
    uint32_t after;
};

// Computes the hash of a word. If `trace` is given, every step is recorded.
uint32_t computeHash(const std::string& word, HashFunction f, std::vector<HashStep>* trace = nullptr);

// One dictionary word stored in the table.
struct DictEntry {
    std::string word;
    int freq = 0;          // how common the word is (Zipf * 100)
    bool custom = false;   // true if the user added it
};

// Details about one lookup (filled in by HashTable::find).
struct LookupInfo {
    uint32_t hash = 0;       // the raw hash value
    size_t bucket = 0;       // hash % bucketCount
    size_t chainLength = 0;  // how many words share that bucket
    size_t comparisons = 0;  // how many string comparisons the lookup needed
    size_t position = 0;     // 1-based position inside the chain (0 = not found)
    bool found = false;
};

// Summary numbers about the table (for the dashboard).
struct HashStats {
    size_t entries = 0;
    size_t buckets = 0;
    size_t usedBuckets = 0;
    size_t emptyBuckets = 0;
    size_t collisions = 0;           // words that share a bucket with an earlier word
    size_t longestChain = 0;
    double loadFactor = 0.0;         // entries / buckets
    double avgChainLength = 0.0;     // average length of NON-empty chains
    std::vector<size_t> chainHistogram;  // index = chain length, value = number of buckets
};

class HashTable {
public:
    explicit HashTable(size_t bucketCount = 20011, HashFunction function = HashFunction::DJB2);

    // Adds an entry. Returns false if the word is already present.
    bool insert(const DictEntry& entry);

    // Finds a word. Returns nullptr if missing. `info` (optional) receives lookup details.
    const DictEntry* find(const std::string& word, LookupInfo* info = nullptr) const;

    bool contains(const std::string& word) const { return find(word) != nullptr; }

    // Re-hashes every word into a new table (new size and/or new hash function).
    void rebuild(size_t newBucketCount, HashFunction newFunction);

    size_t bucketIndexFor(const std::string& word) const;
    const std::vector<DictEntry>& chain(size_t bucketIndex) const { return buckets_[bucketIndex]; }
    std::vector<size_t> bucketLoads() const;   // chain length of every bucket
    HashStats stats() const;

    size_t size() const { return count_; }
    size_t bucketCount() const { return buckets_.size(); }
    HashFunction function() const { return function_; }

private:
    std::vector<std::vector<DictEntry>> buckets_;   // each inner vector is one chain
    size_t count_ = 0;
    HashFunction function_;
};
