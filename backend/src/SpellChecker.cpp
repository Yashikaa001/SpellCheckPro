#include "SpellChecker.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>

namespace {

using Clock = std::chrono::steady_clock;

// Writing the result of a timed loop into a volatile variable stops the compiler
// from deleting the loop because "nobody uses the answer".
volatile size_t g_sink = 0;

double elapsedNs(Clock::time_point a, Clock::time_point b) {
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

double elapsedMs(Clock::time_point a, Clock::time_point b) { return elapsedNs(a, b) / 1e6; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

bool endsWithPossessive(const std::string& w) {
    return w.size() > 2 && w[w.size() - 2] == '\'' && w.back() == 's';
}

}  // namespace

// ---------------------------------------------------------------------------
//  Loading words
// ---------------------------------------------------------------------------
bool SpellChecker::addToStructures(const std::string& word, int freq, bool custom) {
    if (word.empty() || !trie_.insert(word, freq)) return false;   // the trie validates letters
    DictEntry entry;
    entry.word = word;
    entry.freq = freq;
    entry.custom = custom;
    hash_.insert(entry);
    entries_.push_back(entry);
    return true;
}

static size_t loadFile(const std::string& path, bool custom,
                       const std::function<bool(const std::string&, int, bool)>& add) {
    std::ifstream in(path);
    if (!in) return 0;

    size_t loaded = 0;
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (firstLine) {   // remove a UTF-8 byte-order mark if an editor added one
            firstLine = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::string word = line;
        int freq = custom ? 400 : 300;
        const size_t sep = line.find_first_of("\t ");
        if (sep != std::string::npos) {
            word = line.substr(0, sep);
            const std::string rest = trim(line.substr(sep + 1));
            if (!rest.empty()) freq = std::atoi(rest.c_str());
        }
        if (add(text::toLower(word), freq, custom)) ++loaded;
    }
    return loaded;
}

size_t SpellChecker::loadDictionary(const std::string& path) {
    const size_t n = loadFile(path, false, [this](const std::string& w, int f, bool c) {
        return addToStructures(w, f, c);
    });
    std::sort(entries_.begin(), entries_.end(),
              [](const DictEntry& a, const DictEntry& b) { return a.word < b.word; });
    return n;
}

size_t SpellChecker::loadCustomWords(const std::string& path) {
    const size_t n = loadFile(path, true, [this](const std::string& w, int f, bool c) {
        return addToStructures(w, f, c);
    });
    std::sort(entries_.begin(), entries_.end(),
              [](const DictEntry& a, const DictEntry& b) { return a.word < b.word; });
    return n;
}

AddWordResult SpellChecker::addWord(const std::string& rawWord, const std::string& customPath) {
    AddWordResult r;
    const std::string word = text::toLower(trim(rawWord));
    r.word = word;

    if (word.empty() || word.size() > 40) {
        r.message = "Enter a word between 1 and 40 letters.";
        return r;
    }
    if (word.front() == '\'' || word.back() == '\'') {
        r.message = "A word cannot start or end with an apostrophe.";
        return r;
    }
    for (char c : word) {
        if (Trie::charToIndex(c) < 0) {
            r.message = "Only the letters a-z and apostrophes are allowed.";
            return r;
        }
    }
    if (hash_.contains(word)) {
        r.message = "\"" + word + "\" is already in the dictionary.";
        return r;
    }

    const int freq = 400;
    trie_.insert(word, freq);
    DictEntry entry;
    entry.word = word;
    entry.freq = freq;
    entry.custom = true;
    hash_.insert(entry);
    auto pos = std::lower_bound(entries_.begin(), entries_.end(), word,
                                [](const DictEntry& e, const std::string& w) { return e.word < w; });
    entries_.insert(pos, entry);

    if (!customPath.empty()) {
        std::ofstream out(customPath, std::ios::app);
        if (out) out << word << "\t" << freq << "\n";
    }
    r.ok = true;
    r.message = "Added \"" + word + "\" to the dictionary.";
    return r;
}

size_t SpellChecker::customCount() const {
    size_t n = 0;
    for (const DictEntry& e : entries_) {
        if (e.custom) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
//  Lookups. "dog's" is accepted when "dog" is a word.
// ---------------------------------------------------------------------------
bool SpellChecker::isKnownHash(const std::string& w) const {
    if (hash_.contains(w)) return true;
    return endsWithPossessive(w) && hash_.contains(w.substr(0, w.size() - 2));
}

bool SpellChecker::isKnownTrie(const std::string& w) const {
    if (trie_.contains(w)) return true;
    return endsWithPossessive(w) && trie_.contains(w.substr(0, w.size() - 2));
}

// ---------------------------------------------------------------------------
//  Suggestions: trie fuzzy search, then rank the candidates.
// ---------------------------------------------------------------------------
SuggestResult SpellChecker::suggest(const std::string& original, int maxSuggestions, int maxDistance) const {
    SuggestResult result;
    const std::string word = text::toLower(original);
    if (word.empty() || word.size() > 40 || maxSuggestions <= 0) return result;

    // Very short words: allowing 2 edits would match half the dictionary.
    const int limit = word.size() <= 2 ? std::min(maxDistance, 1) : maxDistance;

    const auto t0 = Clock::now();
    std::vector<FuzzyMatch> matches = trie_.fuzzySearch(word, limit, &result.fuzzy);

    struct Scored {
        FuzzyMatch match;
        double score;
    };
    std::vector<Scored> scored;
    for (const FuzzyMatch& m : matches) {
        if (m.distance == 0) continue;   // the word itself
        // LOWER score = better suggestion.
        //  - the edit distance dominates (100 points per edit)
        //  - people rarely mistype the FIRST letter, so a different first letter costs 35
        //  - common words win ties: up to ~66 points bonus for very frequent words
        double score = m.distance * 100.0;
        if (m.word[0] != word[0]) score += 35.0;
        // Swapped neighbouring letters (teh/the, dgo/dog) are the most common typing slip:
        // same letters, different order. Give those candidates a bonus.
        if (m.distance == 1 && m.word.size() == word.size()) {
            std::string a = word, b = m.word;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            if (a == b) score -= 15.0;
        }
        score -= m.freq / 12.0;
        // A forgotten apostrophe is the most common typo of all: "dont" -> "don't".
        if (m.word.find('\'') != std::string::npos) {
            std::string noApostrophe;
            for (char c : m.word) {
                if (c != '\'') noApostrophe += c;
            }
            if (noApostrophe == word) score -= 60.0;
        }
        scored.push_back({m, score});
    }
    result.candidates = scored.size();

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score < b.score;
        return a.match.word < b.match.word;
    });

    for (size_t i = 0; i < scored.size() && static_cast<int>(i) < maxSuggestions; ++i) {
        const FuzzyMatch& m = scored[i].match;
        Suggestion s;
        s.word = text::matchCase(m.word, original);
        s.distance = m.distance;
        s.freq = m.freq;
        const size_t longest = std::max(word.size(), m.word.size());
        s.similarity = 1.0 - static_cast<double>(m.distance) / static_cast<double>(longest);
        result.suggestions.push_back(s);
    }
    result.ms = elapsedMs(t0, Clock::now());
    return result;
}

// ---------------------------------------------------------------------------
//  Classify a typo by comparing it with its best suggestion.
// ---------------------------------------------------------------------------
static std::string classifyError(const std::string& typoLower, const SuggestResult& r) {
    if (r.suggestions.empty()) return "unknown";
    if (r.suggestions[0].distance != 1) return "multiple";
    const edit::Alignment a = edit::align(typoLower, text::toLower(r.suggestions[0].word));
    for (const edit::EditOp& op : a.ops) {
        if (op.type == 'S') return "wrong_letter";
        if (op.type == 'D') return "extra_letter";     // typed word has one letter too many
        if (op.type == 'I') return "missing_letter";   // typed word lacks one letter
        if (op.type == 'T') return "swapped_letters";
    }
    return "multiple";
}

// ---------------------------------------------------------------------------
//  check: the whole pipeline for a paragraph of text.
// ---------------------------------------------------------------------------
CheckResult SpellChecker::check(const std::string& input, const CheckOptions& options) const {
    const auto tStart = Clock::now();
    CheckResult result;
    result.bytes = input.size();

    // 1. Split the text into words and decide which ones need checking.
    const std::vector<text::Token> tokens = text::tokenize(input);
    struct Candidate {
        const text::Token* token;
        std::string lower;
    };
    std::vector<Candidate> toCheck;
    for (const text::Token& t : tokens) {
        if (!t.checkable) continue;   // digits, URLs, e-mail addresses ...
        std::string lower = text::toLower(t.text);
        if (options.ignore.count(lower)) { ++result.ignored; continue; }
        if (options.ignoreCaps && t.text.size() >= 2 && text::isAllUpper(t.text)) { ++result.ignored; continue; }
        toCheck.push_back({&t, std::move(lower)});
    }
    const size_t n = toCheck.size();
    result.words = n;

    // 2. Look every word up in the HASH TABLE. This is the real answer used below.
    //    To get a measurable speed we repeat the pass until ~2000 lookups were made.
    std::vector<char> known(n, 1);
    if (n > 0) {
        const size_t reps = std::max<size_t>(1, 2000 / n);

        auto h0 = Clock::now();
        for (size_t r = 0; r < reps; ++r) {
            for (size_t i = 0; i < n; ++i) {
                const bool k = isKnownHash(toCheck[i].lower);
                if (r == 0) known[i] = k ? 1 : 0;
                g_sink = g_sink + (k ? 1 : 0);
            }
        }
        result.hashNsPerLookup = elapsedNs(h0, Clock::now()) / static_cast<double>(reps * n);

        // 3. Same words through the TRIE, only to compare how fast it is.
        auto t0 = Clock::now();
        for (size_t r = 0; r < reps; ++r) {
            for (size_t i = 0; i < n; ++i) {
                g_sink = g_sink + (isKnownTrie(toCheck[i].lower) ? 1 : 0);
            }
        }
        result.trieNsPerLookup = elapsedNs(t0, Clock::now()) / static_cast<double>(reps * n);
    }

    // 4. Suggestions for every misspelled word (Trie + edit distance).
    for (size_t i = 0; i < n; ++i) {
        if (known[i]) continue;
        const text::Token& tok = *toCheck[i].token;
        MisspelledWord w;
        w.text = tok.text;
        w.start = tok.start;
        w.end = tok.end;
        w.bucket = hash_.bucketIndexFor(toCheck[i].lower);
        w.result = suggest(tok.text, options.maxSuggestions, options.maxDistance);
        w.errorType = classifyError(toCheck[i].lower, w.result);
        result.suggestMs += w.result.ms;
        result.errors.push_back(std::move(w));
    }

    result.misspelled = result.errors.size();
    result.accuracy = n == 0 ? 100.0
                             : 100.0 * static_cast<double>(n - result.misspelled) / static_cast<double>(n);
    result.totalMs = elapsedMs(tStart, Clock::now());
    return result;
}

// ---------------------------------------------------------------------------
//  Dictionary explorer: filter, sort, paginate.
// ---------------------------------------------------------------------------
DictionaryPage SpellChecker::queryDictionary(const DictionaryQuery& q) const {
    const std::string needle = text::toLower(q.q);
    std::vector<const DictEntry*> matches;

    for (const DictEntry& e : entries_) {
        const size_t len = e.word.size();
        if (len < q.minLen || len > q.maxLen) continue;
        if (q.customOnly && !e.custom) continue;
        if (!needle.empty()) {
            if (q.mode == "prefix") {
                if (len < needle.size() || e.word.compare(0, needle.size(), needle) != 0) continue;
            } else if (q.mode == "suffix") {
                if (len < needle.size() || e.word.compare(len - needle.size(), needle.size(), needle) != 0) continue;
            } else {
                if (e.word.find(needle) == std::string::npos) continue;
            }
        }
        matches.push_back(&e);
    }

    // entries_ is already alphabetical; other sort orders are stable so ties stay alphabetical.
    if (q.sort == "freq") {
        std::stable_sort(matches.begin(), matches.end(), [&](const DictEntry* a, const DictEntry* b) {
            return q.descending ? a->freq > b->freq : a->freq < b->freq;
        });
    } else if (q.sort == "length") {
        std::stable_sort(matches.begin(), matches.end(), [&](const DictEntry* a, const DictEntry* b) {
            return q.descending ? a->word.size() > b->word.size() : a->word.size() < b->word.size();
        });
    } else if (q.descending) {
        std::reverse(matches.begin(), matches.end());
    }

    DictionaryPage page;
    page.total = matches.size();
    page.pageSize = std::max<size_t>(1, q.pageSize);
    page.pages = std::max<size_t>(1, (page.total + page.pageSize - 1) / page.pageSize);
    page.page = std::min(std::max<size_t>(1, q.page), page.pages);
    const size_t from = (page.page - 1) * page.pageSize;
    const size_t to = std::min(page.total, from + page.pageSize);
    for (size_t i = from; i < to; ++i) page.items.push_back(matches[i]);
    return page;
}

// ---------------------------------------------------------------------------
//  Benchmark: hash table vs trie vs scanning a plain list. All real timings.
// ---------------------------------------------------------------------------
BenchmarkResult SpellChecker::runBenchmark(size_t n) const {
    BenchmarkResult result;
    if (entries_.empty()) return result;

    // Build the queries: n real words and n made-up words, mixed together.
    std::mt19937 rng(20240601);
    std::vector<std::string> queries;
    queries.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        const std::string& real = entries_[rng() % entries_.size()].word;
        queries.push_back(real);

        std::string fake = entries_[rng() % entries_.size()].word + "qz";   // "qz" ending never occurs
        while (hash_.contains(fake)) fake += "q";
        queries.push_back(fake);
    }
    result.hits = n;
    result.misses = n;

    auto measure = [&](size_t count, BenchmarkTimes& out, const std::function<bool(const std::string&)>& lookup) {
        out.queries = count;
        const auto t0 = Clock::now();
        size_t found = 0;
        for (size_t i = 0; i < count; ++i) found += lookup(queries[i]) ? 1 : 0;
        const auto t1 = Clock::now();
        g_sink = g_sink + found;
        out.totalMs = elapsedMs(t0, t1);
        out.nsPerLookup = count ? elapsedNs(t0, t1) / static_cast<double>(count) : 0.0;
    };

    measure(queries.size(), result.hash, [&](const std::string& q) { return hash_.contains(q); });
    measure(queries.size(), result.trie, [&](const std::string& q) { return trie_.contains(q); });

    // Scanning the list is SLOW, so it only gets the first 1000 queries (still ~50% hits).
    const size_t linearCount = std::min<size_t>(queries.size(), 1000);
    measure(linearCount, result.linear, [&](const std::string& q) {
        for (const DictEntry& e : entries_) {
            if (e.word == q) return true;
        }
        return false;
    });
    return result;
}

// ---------------------------------------------------------------------------
//  Self test: feed known misspellings and see where the right word ranks.
// ---------------------------------------------------------------------------
SelfTestResult SpellChecker::runSelfTest(const std::string& path) const {
    SelfTestResult result;
    std::ifstream in(path);
    if (!in) return result;

    double totalMs = 0;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;

        SelfTestCase c;
        c.typo = text::toLower(trim(line.substr(0, tab)));
        c.expected = text::toLower(trim(line.substr(tab + 1)));
        ++result.total;

        if (!isKnownHash(c.expected) || isKnownHash(c.typo)) {
            c.skipped = true;
            ++result.skipped;
            result.cases.push_back(c);
            continue;
        }

        const SuggestResult s = suggest(c.typo, 10, 2);
        totalMs += s.ms;
        ++result.tested;
        if (!s.suggestions.empty()) c.top = s.suggestions[0].word;
        for (size_t i = 0; i < s.suggestions.size(); ++i) {
            if (text::toLower(s.suggestions[i].word) == c.expected) {
                c.rank = static_cast<int>(i) + 1;
                break;
            }
        }
        if (c.rank == 1) ++result.top1;
        if (c.rank >= 1 && c.rank <= 3) ++result.top3;
        if (c.rank >= 1 && c.rank <= 5) ++result.top5;
        result.cases.push_back(c);
    }
    result.avgMs = result.tested ? totalMs / static_cast<double>(result.tested) : 0.0;
    return result;
}
