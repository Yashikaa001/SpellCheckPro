// ============================================================================
//  Metrics.h - running statistics shown on the dashboard.
//
//  Every number here is measured by the real backend while you use the app:
//  how many checks ran, how fast the hash table / trie answered, how accurate the
//  text was, which words are misspelled most often, and so on.
// ============================================================================
#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>

#include "SpellChecker.h"

// One point of the "live" charts (one spell check).
struct RecentCheck {
    long long timeMs = 0;    // when it happened (milliseconds since 1970)
    size_t words = 0;
    size_t errors = 0;
    double accuracy = 100;
    double hashNs = 0;
    double trieNs = 0;
    double suggestMs = 0;
    double totalMs = 0;
};

class Metrics {
public:
    Metrics();

    void recordCheck(const CheckResult& result);
    void recordLookup(double hashNs, double trieNs);          // from the Lookup tool
    void recordCorrection(int rank);                          // user applied a suggestion (rank 0 = first)
    void reset();

    // ---- counters (read by the API layer) ----
    std::chrono::steady_clock::time_point startedAt;
    size_t checks = 0;
    size_t wordsChecked = 0;
    size_t misspelled = 0;
    size_t ignored = 0;
    size_t misspelledWithSuggestion = 0;
    size_t suggestionSearches = 0;
    double suggestMsTotal = 0;
    size_t nodesVisitedTotal = 0;

    size_t hashLookups = 0;
    double hashNsTotal = 0;
    size_t trieLookups = 0;
    double trieNsTotal = 0;

    size_t correctionsApplied = 0;
    size_t correctionsTop1 = 0;

    std::map<std::string, size_t> errorTypes;                 // wrong_letter -> count ...
    std::unordered_map<std::string, size_t> misspelledCounts; // "teh" -> 3
    std::deque<RecentCheck> recent;                           // newest at the back

    static constexpr size_t kMaxRecent = 40;
};

// Milliseconds since 1970 (used for timestamps).
long long nowMs();
