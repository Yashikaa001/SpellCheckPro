// ============================================================================
//  History.h - the correction history.
//
//  Two kinds of entries are stored (and saved to data/history.tsv so they survive
//  a restart):
//     "check"       - the user pressed "Check spelling" on some text
//     "correction"  - the user replaced a misspelled word with a suggestion
// ============================================================================
#pragma once

#include <string>
#include <vector>

struct HistoryEntry {
    long long id = 0;
    long long timestampMs = 0;
    std::string type;       // "check" or "correction"
    std::string text;       // check: the text that was checked (shortened)
    int words = 0;          // check: number of words checked
    int errors = 0;         // check: number of misspelled words
    double accuracy = 100;  // check: percent correct
    std::string from;       // correction: the wrong word
    std::string to;         // correction: the replacement
    int rank = -1;          // correction: which suggestion was used (0 = first)
};

class History {
public:
    // Loads old entries from `path` and remembers the path for saving.
    void load(const std::string& path);

    void addCheck(const std::string& text, int words, int errors, double accuracy);
    void addCorrection(const std::string& from, const std::string& to, int rank);
    void clear();

    // Newest entries first. `type` may be "", "check" or "correction".
    std::vector<HistoryEntry> list(const std::string& type, size_t limit) const;
    size_t size() const { return entries_.size(); }

private:
    void add(HistoryEntry entry);
    void save() const;

    std::vector<HistoryEntry> entries_;   // oldest first
    std::string path_;
    long long nextId_ = 1;

    static constexpr size_t kMaxEntries = 300;
    static constexpr size_t kMaxTextLength = 2000;
};
