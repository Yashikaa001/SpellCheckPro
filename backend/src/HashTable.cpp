#include "HashTable.h"

#include <algorithm>

// ---------------------------------------------------------------------------
//  Hash functions
// ---------------------------------------------------------------------------
const char* hashFunctionName(HashFunction f) {
    switch (f) {
        case HashFunction::DJB2:  return "djb2";
        case HashFunction::FNV1A: return "fnv1a";
        case HashFunction::SDBM:  return "sdbm";
        case HashFunction::SUM:   return "sum";
    }
    return "djb2";
}

bool parseHashFunction(const std::string& name, HashFunction& out) {
    if (name == "djb2")  { out = HashFunction::DJB2;  return true; }
    if (name == "fnv1a") { out = HashFunction::FNV1A; return true; }
    if (name == "sdbm")  { out = HashFunction::SDBM;  return true; }
    if (name == "sum")   { out = HashFunction::SUM;   return true; }
    return false;
}

std::string hashFunctionFormula(HashFunction f) {
    switch (f) {
        case HashFunction::DJB2:  return "h = 5381;  for each char c:  h = h * 33 + c";
        case HashFunction::FNV1A: return "h = 2166136261;  for each char c:  h = (h XOR c) * 16777619";
        case HashFunction::SDBM:  return "h = 0;  for each char c:  h = c + (h << 6) + (h << 16) - h";
        case HashFunction::SUM:   return "h = 0;  for each char c:  h = h + c";
    }
    return "";
}

uint32_t computeHash(const std::string& word, HashFunction f, std::vector<HashStep>* trace) {
    // All maths is done on 32-bit unsigned numbers, so values simply wrap around.
    uint32_t h = 0;
    switch (f) {
        case HashFunction::DJB2:  h = 5381u;       break;
        case HashFunction::FNV1A: h = 2166136261u; break;
        default:                  h = 0u;          break;
    }

    for (char ch : word) {
        const uint32_t c = static_cast<unsigned char>(ch);
        const uint32_t before = h;
        switch (f) {
            case HashFunction::DJB2:  h = h * 33u + c;                          break;
            case HashFunction::FNV1A: h = (h ^ c) * 16777619u;                  break;
            case HashFunction::SDBM:  h = c + (h << 6) + (h << 16) - h;         break;
            case HashFunction::SUM:   h = h + c;                                break;
        }
        if (trace) trace->push_back({ch, before, h});
    }
    return h;
}

// ---------------------------------------------------------------------------
//  HashTable
// ---------------------------------------------------------------------------
HashTable::HashTable(size_t bucketCount, HashFunction function)
    : buckets_(bucketCount == 0 ? 1 : bucketCount), function_(function) {}

size_t HashTable::bucketIndexFor(const std::string& word) const {
    return computeHash(word, function_) % buckets_.size();
}

bool HashTable::insert(const DictEntry& entry) {
    std::vector<DictEntry>& chain = buckets_[bucketIndexFor(entry.word)];
    for (const DictEntry& existing : chain) {
        if (existing.word == entry.word) return false;   // duplicate
    }
    chain.push_back(entry);
    ++count_;
    return true;
}

const DictEntry* HashTable::find(const std::string& word, LookupInfo* info) const {
    const uint32_t hash = computeHash(word, function_);
    const size_t bucket = hash % buckets_.size();
    const std::vector<DictEntry>& chain = buckets_[bucket];

    if (info) {
        *info = LookupInfo();
        info->hash = hash;
        info->bucket = bucket;
        info->chainLength = chain.size();
    }

    size_t comparisons = 0;
    for (size_t i = 0; i < chain.size(); ++i) {
        ++comparisons;
        if (chain[i].word == word) {
            if (info) {
                info->comparisons = comparisons;
                info->position = i + 1;
                info->found = true;
            }
            return &chain[i];
        }
    }
    if (info) info->comparisons = comparisons;
    return nullptr;
}

void HashTable::rebuild(size_t newBucketCount, HashFunction newFunction) {
    // Move every entry out, resize, then insert them again with the new settings.
    std::vector<DictEntry> all;
    all.reserve(count_);
    for (auto& chain : buckets_) {
        for (auto& e : chain) all.push_back(std::move(e));
    }

    buckets_.assign(newBucketCount == 0 ? 1 : newBucketCount, std::vector<DictEntry>());
    function_ = newFunction;
    count_ = 0;
    for (auto& e : all) insert(e);
}

std::vector<size_t> HashTable::bucketLoads() const {
    std::vector<size_t> loads;
    loads.reserve(buckets_.size());
    for (const auto& chain : buckets_) loads.push_back(chain.size());
    return loads;
}

HashStats HashTable::stats() const {
    HashStats s;
    s.entries = count_;
    s.buckets = buckets_.size();
    size_t usedTotal = 0;
    for (const auto& chain : buckets_) {
        const size_t len = chain.size();
        if (len == 0) {
            ++s.emptyBuckets;
        } else {
            ++s.usedBuckets;
            usedTotal += len;
        }
        s.longestChain = std::max(s.longestChain, len);
    }
    s.collisions = count_ - s.usedBuckets;
    s.loadFactor = static_cast<double>(count_) / static_cast<double>(buckets_.size());
    s.avgChainLength = s.usedBuckets ? static_cast<double>(usedTotal) / static_cast<double>(s.usedBuckets) : 0.0;

    s.chainHistogram.assign(s.longestChain + 1, 0);
    for (const auto& chain : buckets_) ++s.chainHistogram[chain.size()];
    return s;
}
