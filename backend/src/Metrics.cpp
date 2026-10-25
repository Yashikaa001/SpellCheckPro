#include "Metrics.h"

#include "TextUtils.h"

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

Metrics::Metrics() { reset(); }

void Metrics::reset() {
    startedAt = std::chrono::steady_clock::now();
    checks = wordsChecked = misspelled = ignored = 0;
    misspelledWithSuggestion = suggestionSearches = nodesVisitedTotal = 0;
    suggestMsTotal = 0;
    hashLookups = trieLookups = 0;
    hashNsTotal = trieNsTotal = 0;
    correctionsApplied = correctionsTop1 = 0;
    errorTypes.clear();
    misspelledCounts.clear();
    recent.clear();
}

void Metrics::recordCheck(const CheckResult& r) {
    ++checks;
    wordsChecked += r.words;
    misspelled += r.misspelled;
    ignored += r.ignored;

    // Speed numbers are weighted by how many lookups the check made.
    if (r.words > 0) {
        hashLookups += r.words;
        hashNsTotal += r.hashNsPerLookup * static_cast<double>(r.words);
        trieLookups += r.words;
        trieNsTotal += r.trieNsPerLookup * static_cast<double>(r.words);
    }

    for (const MisspelledWord& w : r.errors) {
        ++misspelledCounts[text::toLower(w.text)];
        ++errorTypes[w.errorType];
        ++suggestionSearches;
        suggestMsTotal += w.result.ms;
        nodesVisitedTotal += w.result.fuzzy.nodesVisited;
        if (!w.result.suggestions.empty()) ++misspelledWithSuggestion;
    }

    RecentCheck rc;
    rc.timeMs = nowMs();
    rc.words = r.words;
    rc.errors = r.misspelled;
    rc.accuracy = r.accuracy;
    rc.hashNs = r.hashNsPerLookup;
    rc.trieNs = r.trieNsPerLookup;
    rc.suggestMs = r.suggestMs;
    rc.totalMs = r.totalMs;
    recent.push_back(rc);
    while (recent.size() > kMaxRecent) recent.pop_front();
}

void Metrics::recordLookup(double hashNs, double trieNs) {
    ++hashLookups;
    hashNsTotal += hashNs;
    ++trieLookups;
    trieNsTotal += trieNs;
}

void Metrics::recordCorrection(int rank) {
    ++correctionsApplied;
    if (rank == 0) ++correctionsTop1;
}
