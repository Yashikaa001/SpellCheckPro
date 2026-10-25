// ============================================================================
//  Api.cpp - the HTTP server and all JSON endpoints.
//
//  Endpoints (all under /api, all return JSON):
//
//   Spell checking
//     POST /api/check?maxSuggestions=5&maxDistance=2&ignoreCaps=1&ignore=a,b&record=1
//          body = the text (plain text)       -> misspelled words + suggestions + timings
//     GET  /api/suggest?word=teh&max=5&distance=2
//     GET  /api/edit?a=kitten&b=sitting        -> edit-distance table and edit steps
//
//   Hash table
//     GET  /api/hash/overview                  -> stats, bucket loads, worst buckets
//     GET  /api/hash/lookup?word=hello         -> hash steps, bucket, chain, timing
//     GET  /api/hash/bucket?index=123          -> every word stored in that bucket
//     POST /api/hash/rebuild?buckets=5003&fn=fnv1a
//
//   Trie
//     GET  /api/trie/stats
//     GET  /api/trie/node?prefix=ca            -> children of one node
//     GET  /api/trie/tree?prefix=ca&depth=2    -> a small sub-tree for drawing
//     GET  /api/trie/path?word=cat             -> the nodes visited while spelling a word
//     GET  /api/trie/autocomplete?prefix=ca&limit=10
//
//   Dashboard / tools
//     GET  /api/stats                          -> all dashboard numbers
//     GET  /api/benchmark?n=3000               -> hash vs trie vs linear scan
//     GET  /api/selftest                       -> accuracy on the built-in typo list
//     GET  /api/dictionary?q=...&mode=prefix&sort=alpha&page=1&pageSize=50
//     POST /api/dictionary/add?word=blockchain
//     GET  /api/history?type=&limit=100
//     POST /api/history/correction?from=teh&to=the&rank=0
//     POST /api/history/clear
//     POST /api/metrics/reset
// ============================================================================
#include "Api.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>

#include "Json.h"
#include "TextUtils.h"
#include "httplib.h"

namespace {

using Clock = std::chrono::steady_clock;

// The server handles requests on several threads; one mutex keeps our data safe.
std::mutex g_mutex;

// ---------------------------------------------------------------------------
//  Small helpers for reading query parameters
// ---------------------------------------------------------------------------
std::string param(const httplib::Request& req, const std::string& name, const std::string& fallback = "") {
    return req.has_param(name) ? req.get_param_value(name) : fallback;
}

long long intParam(const httplib::Request& req, const std::string& name, long long fallback,
                   long long lo, long long hi) {
    if (!req.has_param(name)) return fallback;
    const std::string v = req.get_param_value(name);
    char* end = nullptr;
    const long long n = std::strtoll(v.c_str(), &end, 10);
    if (end == v.c_str()) return fallback;
    return std::min(hi, std::max(lo, n));
}

bool boolParam(const httplib::Request& req, const std::string& name, bool fallback) {
    if (!req.has_param(name)) return fallback;
    const std::string v = req.get_param_value(name);
    return v == "1" || v == "true" || v == "on";
}

void sendJson(httplib::Response& res, const JsonWriter& w, int status = 200) {
    res.status = status;
    res.set_content(w.str(), "application/json; charset=utf-8");
}

void sendError(httplib::Response& res, int status, const std::string& message) {
    JsonWriter w;
    w.beginObject().field("error", message).endObject();
    sendJson(res, w, status);
}

// Keeps only the characters the dictionary understands.
std::string cleanWord(const std::string& raw) {
    std::string out;
    for (char c : text::toLower(raw)) {
        if (Trie::charToIndex(c) >= 0) out += c;
    }
    return out;
}

std::vector<std::string> splitCommas(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == ',') { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    parts.push_back(cur);
    return parts;
}

double nsBetween(Clock::time_point a, Clock::time_point b) {
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

// ---------------------------------------------------------------------------
//  JSON writers for the shared result types
// ---------------------------------------------------------------------------
void writeFuzzy(JsonWriter& w, const SuggestResult& r, size_t trieNodes) {
    w.key("fuzzy").beginObject();
    w.field("nodesVisited", r.fuzzy.nodesVisited);
    w.field("subtreesPruned", r.fuzzy.subtreesPruned);
    w.field("wordsPruned", r.fuzzy.wordsPruned);
    w.field("trieNodes", trieNodes);
    w.field("candidates", r.candidates);
    w.field("ms", r.ms);
    w.endObject();
}

void writeSuggestions(JsonWriter& w, const SuggestResult& r) {
    w.key("suggestions").beginArray();
    for (const Suggestion& s : r.suggestions) {
        w.beginObject();
        w.field("word", s.word);
        w.field("distance", s.distance);
        w.field("freq", s.freq);
        w.field("similarity", s.similarity);
        w.endObject();
    }
    w.endArray();
}

void writeHashOverview(JsonWriter& w, const HashTable& table) {
    const HashStats s = table.stats();
    w.beginObject();
    w.field("function", hashFunctionName(table.function()));
    w.field("formula", hashFunctionFormula(table.function()));
    w.field("entries", s.entries);
    w.field("buckets", s.buckets);
    w.field("usedBuckets", s.usedBuckets);
    w.field("emptyBuckets", s.emptyBuckets);
    w.field("collisions", s.collisions);
    w.field("longestChain", s.longestChain);
    w.field("loadFactor", s.loadFactor);
    w.field("avgChainLength", s.avgChainLength);

    w.key("histogram").beginArray();
    for (size_t n : s.chainHistogram) w.value(n);
    w.endArray();

    const std::vector<size_t> loads = table.bucketLoads();
    w.key("loads").beginArray();
    for (size_t n : loads) w.value(n);
    w.endArray();

    // The 8 buckets with the longest chains, with a few sample words each.
    std::vector<size_t> order(loads.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    const size_t top = std::min<size_t>(8, order.size());
    std::partial_sort(order.begin(), order.begin() + static_cast<long>(top), order.end(),
                      [&](size_t a, size_t b) { return loads[a] != loads[b] ? loads[a] > loads[b] : a < b; });
    w.key("worst").beginArray();
    for (size_t k = 0; k < top; ++k) {
        const size_t idx = order[k];
        if (loads[idx] < 2) break;
        w.beginObject();
        w.field("bucket", idx);
        w.field("length", loads[idx]);
        w.key("words").beginArray();
        const auto& chain = table.chain(idx);
        for (size_t i = 0; i < chain.size() && i < 6; ++i) w.value(chain[i].word);
        w.endArray();
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

// Writes a small sub-tree of the trie (used to draw the tree).
void writeTrieTree(JsonWriter& w, const TrieNode* node, const std::string& prefix, char ch,
                   int depth, size_t maxChildren) {
    w.beginObject();
    w.field("ch", std::string(1, ch ? ch : ' '));
    w.field("prefix", prefix);
    w.field("isWord", node->isWord);
    w.field("freq", node->freq);
    w.field("words", node->wordCount);

    size_t childCount = 0;
    for (int i = 0; i < TRIE_ALPHABET; ++i) {
        if (node->children[i]) ++childCount;
    }
    w.field("childCount", childCount);

    w.key("children").beginArray();
    if (depth > 0) {
        // Show the busiest branches first when there are too many to draw.
        std::vector<int> idx;
        for (int i = 0; i < TRIE_ALPHABET; ++i) {
            if (node->children[i]) idx.push_back(i);
        }
        if (idx.size() > maxChildren) {
            std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
                return node->children[a]->wordCount > node->children[b]->wordCount;
            });
            idx.resize(maxChildren);
            std::sort(idx.begin(), idx.end());
        }
        for (int i : idx) {
            const char c = Trie::indexToChar(i);
            writeTrieTree(w, node->children[i].get(), prefix + c, c, depth - 1, maxChildren);
        }
    }
    w.endArray();
    w.endObject();
}

// ---------------------------------------------------------------------------
//  Route handlers
// ---------------------------------------------------------------------------
void handleCheck(AppState& app, const httplib::Request& req, httplib::Response& res) {
    CheckOptions opt;
    opt.maxSuggestions = static_cast<int>(intParam(req, "maxSuggestions", 5, 1, 10));
    opt.maxDistance = static_cast<int>(intParam(req, "maxDistance", 2, 1, 3));
    opt.ignoreCaps = boolParam(req, "ignoreCaps", true);
    for (const std::string& w : splitCommas(param(req, "ignore"))) {
        const std::string clean = cleanWord(w);
        if (!clean.empty()) opt.ignore.insert(clean);
    }
    const bool record = boolParam(req, "record", true);

    std::lock_guard<std::mutex> lock(g_mutex);
    const CheckResult r = app.checker.check(req.body, opt);
    if (record) {
        app.metrics.recordCheck(r);
        if (r.words > 0) {
            app.history.addCheck(req.body, static_cast<int>(r.words), static_cast<int>(r.misspelled), r.accuracy);
        }
    }

    const size_t trieNodes = app.checker.trie().nodeCount();
    JsonWriter w;
    w.beginObject();
    w.key("summary").beginObject();
    w.field("words", r.words);
    w.field("misspelled", r.misspelled);
    w.field("correct", r.words - r.misspelled);
    w.field("ignored", r.ignored);
    w.field("accuracy", r.accuracy);
    w.field("bytes", r.bytes);
    w.field("hashNsPerLookup", r.hashNsPerLookup);
    w.field("trieNsPerLookup", r.trieNsPerLookup);
    w.field("suggestMs", r.suggestMs);
    w.field("totalMs", r.totalMs);
    w.field("recorded", record);
    w.endObject();

    w.key("errors").beginArray();
    for (const MisspelledWord& e : r.errors) {
        w.beginObject();
        w.field("word", e.text);
        w.field("start", e.start);
        w.field("end", e.end);
        w.field("bucket", e.bucket);
        w.field("type", e.errorType);
        writeSuggestions(w, e.result);
        writeFuzzy(w, e.result, trieNodes);
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleSuggest(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const std::string word = param(req, "word");
    std::lock_guard<std::mutex> lock(g_mutex);
    const SuggestResult r = app.checker.suggest(word, static_cast<int>(intParam(req, "max", 5, 1, 20)),
                                                static_cast<int>(intParam(req, "distance", 2, 1, 3)));
    JsonWriter w;
    w.beginObject();
    w.field("word", word);
    w.field("known", app.checker.isKnownHash(text::toLower(word)));
    writeSuggestions(w, r);
    writeFuzzy(w, r, app.checker.trie().nodeCount());
    w.endObject();
    sendJson(res, w);
}

void handleEdit(AppState&, const httplib::Request& req, httplib::Response& res) {
    const std::string a = text::toLower(param(req, "a")).substr(0, 24);
    const std::string b = text::toLower(param(req, "b")).substr(0, 24);
    const edit::Alignment al = edit::align(a, b);

    JsonWriter w;
    w.beginObject();
    w.field("a", a);
    w.field("b", b);
    w.field("distance", al.distance);
    w.key("matrix").beginArray();
    for (const auto& row : al.matrix) {
        w.beginArray();
        for (int v : row) w.value(v);
        w.endArray();
    }
    w.endArray();
    w.key("path").beginArray();
    for (const auto& p : al.path) {
        w.beginArray().value(p.first).value(p.second).endArray();
    }
    w.endArray();
    w.key("ops").beginArray();
    for (const edit::EditOp& op : al.ops) {
        w.beginObject();
        w.field("type", std::string(1, op.type));
        w.field("i", op.i);
        w.field("j", op.j);
        w.field("from", op.from ? std::string(1, op.from) : std::string());
        w.field("to", op.to ? std::string(1, op.to) : std::string());
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleHashOverview(AppState& app, const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_mutex);
    JsonWriter w;
    writeHashOverview(w, app.checker.hashTable());
    sendJson(res, w);
}

void handleHashLookup(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const std::string word = cleanWord(param(req, "word"));
    if (word.empty()) return sendError(res, 400, "Type a word to look up.");

    std::lock_guard<std::mutex> lock(g_mutex);
    const HashTable& table = app.checker.hashTable();

    std::vector<HashStep> steps;
    computeHash(word, table.function(), &steps);

    LookupInfo info;
    table.find(word, &info);

    // Measure real speed: repeat each lookup many times.
    const int reps = 20000;
    size_t sink = 0;
    auto h0 = Clock::now();
    for (int i = 0; i < reps; ++i) sink += table.contains(word) ? 1 : 0;
    const double hashNs = nsBetween(h0, Clock::now()) / reps;
    auto t0 = Clock::now();
    for (int i = 0; i < reps; ++i) sink += app.checker.trie().contains(word) ? 1 : 0;
    const double trieNs = nsBetween(t0, Clock::now()) / reps;
    if (sink == 1) std::cout << "";   // keeps the compiler from removing the loops
    app.metrics.recordLookup(hashNs, trieNs);

    JsonWriter w;
    w.beginObject();
    w.field("word", word);
    w.field("function", hashFunctionName(table.function()));
    w.field("formula", hashFunctionFormula(table.function()));
    w.field("found", info.found);
    w.field("hash", static_cast<unsigned long long>(info.hash));
    w.field("buckets", table.bucketCount());
    w.field("bucket", info.bucket);
    w.field("chainLength", info.chainLength);
    w.field("comparisons", info.comparisons);
    w.field("position", info.position);
    w.field("hashNs", hashNs);
    w.field("trieNs", trieNs);

    w.key("steps").beginArray();
    for (const HashStep& s : steps) {
        w.beginObject();
        w.field("ch", std::string(1, s.ch));
        w.field("code", static_cast<int>(static_cast<unsigned char>(s.ch)));
        w.field("before", static_cast<unsigned long long>(s.before));
        w.field("after", static_cast<unsigned long long>(s.after));
        w.endObject();
    }
    w.endArray();

    w.key("chain").beginArray();
    const auto& chain = table.chain(info.bucket);
    for (size_t i = 0; i < chain.size() && i < 40; ++i) {
        w.beginObject();
        w.field("word", chain[i].word);
        w.field("hash", static_cast<unsigned long long>(computeHash(chain[i].word, table.function())));
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleHashBucket(AppState& app, const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const HashTable& table = app.checker.hashTable();
    const long long idx = intParam(req, "index", 0, 0, static_cast<long long>(table.bucketCount()) - 1);

    JsonWriter w;
    w.beginObject();
    w.field("bucket", idx);
    w.key("words").beginArray();
    for (const DictEntry& e : table.chain(static_cast<size_t>(idx))) {
        w.beginObject();
        w.field("word", e.word);
        w.field("hash", static_cast<unsigned long long>(computeHash(e.word, table.function())));
        w.field("freq", e.freq);
        w.field("custom", e.custom);
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleHashRebuild(AppState& app, const httplib::Request& req, httplib::Response& res) {
    HashFunction fn;
    if (!parseHashFunction(param(req, "fn", "djb2"), fn)) return sendError(res, 400, "Unknown hash function.");
    const size_t buckets = static_cast<size_t>(intParam(req, "buckets", 20011, 16, 200003));

    std::lock_guard<std::mutex> lock(g_mutex);
    const auto t0 = Clock::now();
    app.checker.hashTable().rebuild(buckets, fn);
    const double ms = nsBetween(t0, Clock::now()) / 1e6;

    JsonWriter w;
    w.beginObject();
    w.field("rebuildMs", ms);
    w.key("overview");
    writeHashOverview(w, app.checker.hashTable());
    w.endObject();
    sendJson(res, w);
}

void handleTrieStats(AppState& app, const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const TrieStats s = app.checker.trie().computeStats();
    JsonWriter w;
    w.beginObject();
    w.field("nodes", s.nodeCount);
    w.field("words", s.wordCount);
    w.field("maxDepth", s.maxDepth);
    w.field("leaves", s.leafCount);
    w.field("avgBranching", s.avgBranching);
    w.field("memoryBytes", s.memoryBytes);
    w.field("sharing", s.nodeCount ? 1.0 - static_cast<double>(s.nodeCount) / static_cast<double>(
                                               std::max<size_t>(1, [&] {
                                                   size_t letters = 0;
                                                   for (const DictEntry& e : app.checker.entries()) letters += e.word.size();
                                                   return letters;
                                               }()))
                                  : 0.0);
    w.key("nodesPerDepth").beginArray();
    for (size_t n : s.nodesPerDepth) w.value(n);
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleTrieNode(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const std::string prefix = cleanWord(param(req, "prefix"));
    std::lock_guard<std::mutex> lock(g_mutex);
    const TrieNodeInfo info = app.checker.trie().describe(prefix);

    JsonWriter w;
    w.beginObject();
    w.field("prefix", prefix);
    w.field("exists", info.exists);
    w.field("isWord", info.isWord);
    w.field("freq", info.freq);
    w.field("subtreeWords", info.subtreeWords);
    w.key("children").beginArray();
    for (const TrieChildInfo& c : info.children) {
        w.beginObject();
        w.field("ch", std::string(1, c.ch));
        w.field("isWord", c.isWord);
        w.field("freq", c.freq);
        w.field("words", c.subtreeWords);
        w.field("childCount", c.childCount);
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleTrieTree(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const std::string prefix = cleanWord(param(req, "prefix"));
    const int depth = static_cast<int>(intParam(req, "depth", 2, 0, 4));
    const size_t maxChildren = static_cast<size_t>(intParam(req, "maxChildren", 9, 1, 27));

    std::lock_guard<std::mutex> lock(g_mutex);
    const TrieNode* node = app.checker.trie().find(prefix);
    if (!node) return sendError(res, 404, "No dictionary word starts with \"" + prefix + "\".");

    JsonWriter w;
    writeTrieTree(w, node, prefix, prefix.empty() ? '\0' : prefix.back(), depth, maxChildren);
    sendJson(res, w);
}

void handleTriePath(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const std::string word = cleanWord(param(req, "word"));
    std::lock_guard<std::mutex> lock(g_mutex);
    const Trie& trie = app.checker.trie();

    JsonWriter w;
    w.beginObject();
    w.field("word", word);
    w.key("nodes").beginArray();
    size_t reached = 0;
    bool complete = true;
    for (size_t i = 0; i <= word.size(); ++i) {
        const std::string prefix = word.substr(0, i);
        const TrieNodeInfo info = trie.describe(prefix);
        if (!info.exists) { complete = false; break; }
        reached = i;
        w.beginObject();
        w.field("prefix", prefix);
        w.field("ch", i == 0 ? std::string() : std::string(1, prefix.back()));
        w.field("isWord", info.isWord);
        w.field("words", info.subtreeWords);
        w.key("children").beginArray();
        for (const TrieChildInfo& c : info.children) w.value(std::string(1, c.ch));
        w.endArray();
        w.endObject();
    }
    w.endArray();
    w.field("reached", reached);
    w.field("complete", complete);
    w.field("isWord", complete && trie.contains(word));
    w.endObject();
    sendJson(res, w);
}

void handleAutocomplete(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const std::string prefix = cleanWord(param(req, "prefix"));
    const size_t limit = static_cast<size_t>(intParam(req, "limit", 10, 1, 50));
    std::lock_guard<std::mutex> lock(g_mutex);

    const auto t0 = Clock::now();
    const std::vector<FuzzyMatch> words = app.checker.trie().autocomplete(prefix, limit);
    const double ms = nsBetween(t0, Clock::now()) / 1e6;
    const TrieNode* node = app.checker.trie().find(prefix);

    JsonWriter w;
    w.beginObject();
    w.field("prefix", prefix);
    w.field("total", node ? node->wordCount : 0);
    w.field("ms", ms);
    w.key("words").beginArray();
    for (const FuzzyMatch& m : words) {
        w.beginObject().field("word", m.word).field("freq", m.freq).endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleStats(AppState& app, const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Metrics& m = app.metrics;
    const SpellChecker& sc = app.checker;
    const HashStats hs = sc.hashTable().stats();
    const TrieStats ts = sc.trie().computeStats();

    // word-length distribution of the dictionary
    std::vector<size_t> lengths;
    size_t letters = 0;
    for (const DictEntry& e : sc.entries()) {
        if (lengths.size() <= e.word.size()) lengths.resize(e.word.size() + 1, 0);
        ++lengths[e.word.size()];
        letters += e.word.size();
    }

    const double uptime = std::chrono::duration<double>(Clock::now() - m.startedAt).count();
    const double accuracy = m.wordsChecked ? 100.0 * static_cast<double>(m.wordsChecked - m.misspelled) /
                                                 static_cast<double>(m.wordsChecked)
                                           : 100.0;

    JsonWriter w;
    w.beginObject();

    w.key("dictionary").beginObject();
    w.field("words", sc.entries().size());
    w.field("baseWords", app.baseWordCount);
    w.field("customWords", sc.customCount());
    w.field("avgLength", sc.entries().empty() ? 0.0 : static_cast<double>(letters) / static_cast<double>(sc.entries().size()));
    w.key("lengths").beginArray();
    for (size_t n : lengths) w.value(n);
    w.endArray();
    w.endObject();

    w.key("hash").beginObject();
    w.field("function", hashFunctionName(sc.hashTable().function()));
    w.field("buckets", hs.buckets);
    w.field("loadFactor", hs.loadFactor);
    w.field("collisions", hs.collisions);
    w.field("longestChain", hs.longestChain);
    w.field("usedBuckets", hs.usedBuckets);
    w.endObject();

    w.key("trie").beginObject();
    w.field("nodes", ts.nodeCount);
    w.field("maxDepth", ts.maxDepth);
    w.field("memoryBytes", ts.memoryBytes);
    w.endObject();

    w.key("session").beginObject();
    w.field("uptimeSeconds", uptime);
    w.field("checks", m.checks);
    w.field("wordsChecked", m.wordsChecked);
    w.field("misspelled", m.misspelled);
    w.field("ignored", m.ignored);
    w.field("accuracy", accuracy);
    w.field("avgHashNs", m.hashLookups ? m.hashNsTotal / static_cast<double>(m.hashLookups) : 0.0);
    w.field("avgTrieNs", m.trieLookups ? m.trieNsTotal / static_cast<double>(m.trieLookups) : 0.0);
    w.field("lookups", m.hashLookups);
    w.field("avgSuggestMs", m.suggestionSearches ? m.suggestMsTotal / static_cast<double>(m.suggestionSearches) : 0.0);
    w.field("avgNodesVisited", m.suggestionSearches ? static_cast<double>(m.nodesVisitedTotal) / static_cast<double>(m.suggestionSearches) : 0.0);
    w.field("suggestionCoverage", m.misspelled ? 100.0 * static_cast<double>(m.misspelledWithSuggestion) / static_cast<double>(m.misspelled) : 0.0);
    w.field("correctionsApplied", m.correctionsApplied);
    w.field("firstChoiceRate", m.correctionsApplied ? 100.0 * static_cast<double>(m.correctionsTop1) / static_cast<double>(m.correctionsApplied) : 0.0);
    w.endObject();

    w.key("errorTypes").beginObject();
    for (const auto& kv : m.errorTypes) w.field(kv.first, kv.second);
    w.endObject();

    std::vector<std::pair<std::string, size_t>> top(m.misspelledCounts.begin(), m.misspelledCounts.end());
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    w.key("topMisspelled").beginArray();
    for (size_t i = 0; i < top.size() && i < 8; ++i) {
        w.beginObject().field("word", top[i].first).field("count", top[i].second).endObject();
    }
    w.endArray();

    w.key("recent").beginArray();
    for (const RecentCheck& r : m.recent) {
        w.beginObject();
        w.field("time", r.timeMs);
        w.field("words", r.words);
        w.field("errors", r.errors);
        w.field("accuracy", r.accuracy);
        w.field("hashNs", r.hashNs);
        w.field("trieNs", r.trieNs);
        w.field("suggestMs", r.suggestMs);
        w.field("totalMs", r.totalMs);
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleBenchmark(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const size_t n = static_cast<size_t>(intParam(req, "n", 3000, 100, 20000));
    std::lock_guard<std::mutex> lock(g_mutex);
    const BenchmarkResult r = app.checker.runBenchmark(n);

    JsonWriter w;
    w.beginObject();
    w.field("hits", r.hits);
    w.field("misses", r.misses);
    w.field("dictionarySize", app.checker.entries().size());
    const std::pair<const char*, const BenchmarkTimes*> rows[] = {
        {"hash", &r.hash}, {"trie", &r.trie}, {"linear", &r.linear}};
    for (const auto& row : rows) {
        w.key(row.first).beginObject();
        w.field("queries", row.second->queries);
        w.field("totalMs", row.second->totalMs);
        w.field("nsPerLookup", row.second->nsPerLookup);
        w.endObject();
    }
    w.endObject();
    sendJson(res, w);
}

void handleSelfTest(AppState& app, const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const SelfTestResult r = app.checker.runSelfTest(app.selfTestPath);

    JsonWriter w;
    w.beginObject();
    w.field("total", r.total);
    w.field("tested", r.tested);
    w.field("skipped", r.skipped);
    w.field("top1", r.top1);
    w.field("top3", r.top3);
    w.field("top5", r.top5);
    w.field("top1Pct", r.tested ? 100.0 * static_cast<double>(r.top1) / static_cast<double>(r.tested) : 0.0);
    w.field("top3Pct", r.tested ? 100.0 * static_cast<double>(r.top3) / static_cast<double>(r.tested) : 0.0);
    w.field("top5Pct", r.tested ? 100.0 * static_cast<double>(r.top5) / static_cast<double>(r.tested) : 0.0);
    w.field("avgMs", r.avgMs);
    w.key("cases").beginArray();
    for (const SelfTestCase& c : r.cases) {
        if (c.skipped) continue;
        w.beginObject();
        w.field("typo", c.typo);
        w.field("expected", c.expected);
        w.field("top", c.top);
        w.field("rank", c.rank);
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleDictionary(AppState& app, const httplib::Request& req, httplib::Response& res) {
    DictionaryQuery q;
    q.q = param(req, "q");
    q.mode = param(req, "mode", "prefix");
    q.minLen = static_cast<size_t>(intParam(req, "minLen", 0, 0, 100));
    q.maxLen = static_cast<size_t>(intParam(req, "maxLen", 100, 0, 100));
    q.customOnly = boolParam(req, "customOnly", false);
    q.sort = param(req, "sort", "alpha");
    q.descending = param(req, "dir", "asc") == "desc";
    q.page = static_cast<size_t>(intParam(req, "page", 1, 1, 1000000));
    q.pageSize = static_cast<size_t>(intParam(req, "pageSize", 50, 1, 200));

    std::lock_guard<std::mutex> lock(g_mutex);
    const DictionaryPage page = app.checker.queryDictionary(q);

    JsonWriter w;
    w.beginObject();
    w.field("total", page.total);
    w.field("page", page.page);
    w.field("pages", page.pages);
    w.field("pageSize", page.pageSize);
    w.key("items").beginArray();
    for (const DictEntry* e : page.items) {
        w.beginObject();
        w.field("word", e->word);
        w.field("freq", e->freq);
        w.field("length", e->word.size());
        w.field("custom", e->custom);
        w.field("bucket", app.checker.hashTable().bucketIndexFor(e->word));
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleAddWord(AppState& app, const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const AddWordResult r = app.checker.addWord(param(req, "word"), app.customPath);
    JsonWriter w;
    w.beginObject();
    w.field("ok", r.ok);
    w.field("message", r.message);
    w.field("word", r.word);
    w.field("totalWords", app.checker.entries().size());
    if (r.ok) w.field("bucket", app.checker.hashTable().bucketIndexFor(r.word));
    w.endObject();
    sendJson(res, w, r.ok ? 200 : 400);
}

void handleHistory(AppState& app, const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto items = app.history.list(param(req, "type"), static_cast<size_t>(intParam(req, "limit", 100, 1, 300)));

    JsonWriter w;
    w.beginObject();
    w.field("total", app.history.size());
    w.key("items").beginArray();
    for (const HistoryEntry& e : items) {
        w.beginObject();
        w.field("id", e.id);
        w.field("time", e.timestampMs);
        w.field("type", e.type);
        w.field("text", e.text);
        w.field("words", e.words);
        w.field("errors", e.errors);
        w.field("accuracy", e.accuracy);
        w.field("from", e.from);
        w.field("to", e.to);
        w.field("rank", e.rank);
        w.endObject();
    }
    w.endArray();
    w.endObject();
    sendJson(res, w);
}

void handleHistoryCorrection(AppState& app, const httplib::Request& req, httplib::Response& res) {
    const std::string from = param(req, "from");
    const std::string to = param(req, "to");
    if (from.empty() || to.empty()) return sendError(res, 400, "Both 'from' and 'to' are required.");
    const int rank = static_cast<int>(intParam(req, "rank", -1, -1, 50));

    std::lock_guard<std::mutex> lock(g_mutex);
    app.history.addCorrection(from, to, rank);
    if (rank >= 0) app.metrics.recordCorrection(rank);
    JsonWriter w;
    w.beginObject().field("ok", true).endObject();
    sendJson(res, w);
}

}  // namespace

// ---------------------------------------------------------------------------
//  runServer: register every route, then listen.
// ---------------------------------------------------------------------------
int runServer(AppState& app, const std::string& host, int port) {
    httplib::Server server;
    server.set_payload_max_length(512 * 1024);   // 512 KB of text is plenty

    // Turns "(AppState&, Request, Response)" handlers into httplib handlers.
    using Handler = void (*)(AppState&, const httplib::Request&, httplib::Response&);
    auto get = [&](const char* path, Handler h) {
        server.Get(path, [&app, h](const httplib::Request& rq, httplib::Response& rs) { h(app, rq, rs); });
    };
    auto post = [&](const char* path, Handler h) {
        server.Post(path, [&app, h](const httplib::Request& rq, httplib::Response& rs) { h(app, rq, rs); });
    };

    post("/api/check", handleCheck);
    get("/api/suggest", handleSuggest);
    get("/api/edit", handleEdit);

    get("/api/hash/overview", handleHashOverview);
    get("/api/hash/lookup", handleHashLookup);
    get("/api/hash/bucket", handleHashBucket);
    post("/api/hash/rebuild", handleHashRebuild);

    get("/api/trie/stats", handleTrieStats);
    get("/api/trie/node", handleTrieNode);
    get("/api/trie/tree", handleTrieTree);
    get("/api/trie/path", handleTriePath);
    get("/api/trie/autocomplete", handleAutocomplete);

    get("/api/stats", handleStats);
    get("/api/benchmark", handleBenchmark);
    get("/api/selftest", handleSelfTest);
    get("/api/dictionary", handleDictionary);
    post("/api/dictionary/add", handleAddWord);
    get("/api/history", handleHistory);
    post("/api/history/correction", handleHistoryCorrection);

    post("/api/history/clear", [](AppState& a, const httplib::Request&, httplib::Response& rs) {
        std::lock_guard<std::mutex> lock(g_mutex);
        a.history.clear();
        JsonWriter w;
        w.beginObject().field("ok", true).endObject();
        sendJson(rs, w);
    });
    post("/api/metrics/reset", [](AppState& a, const httplib::Request&, httplib::Response& rs) {
        std::lock_guard<std::mutex> lock(g_mutex);
        a.metrics.reset();
        JsonWriter w;
        w.beginObject().field("ok", true).endObject();
        sendJson(rs, w);
    });
    get("/api/health", [](AppState& a, const httplib::Request&, httplib::Response& rs) {
        JsonWriter w;
        w.beginObject().field("status", "ok").field("words", a.checker.entries().size()).endObject();
        sendJson(rs, w);
    });

    // Unknown /api paths -> JSON error instead of an HTML page.
    server.set_error_handler([](const httplib::Request& rq, httplib::Response& rs) {
        // Only replace EMPTY error pages; handlers that already sent a JSON error keep theirs.
        if (rq.path.rfind("/api/", 0) == 0 && rs.body.empty()) {
            JsonWriter w;
            w.beginObject().field("error", "Not found: " + rq.path).endObject();
            rs.set_content(w.str(), "application/json; charset=utf-8");
        }
    });
    server.set_exception_handler([](const httplib::Request&, httplib::Response& rs, std::exception_ptr) {
        rs.status = 500;
        rs.set_content("{\"error\":\"Internal server error\"}", "application/json; charset=utf-8");
    });

    // Dashboard files (index.html, css/, js/ ...). Always revalidate so edits show up on reload.
    if (!server.set_mount_point("/", app.webDir)) {
        std::cerr << "ERROR: frontend folder not found: " << app.webDir << "\n";
        return 1;
    }
    server.set_default_headers({{"Cache-Control", "no-cache"}});

    std::cout << "\n  Dashboard ready  ->  http://" << (host == "0.0.0.0" ? "localhost" : host) << ":" << port
              << "/\n  Press Ctrl+C to stop.\n\n";

    if (!server.listen(host, port)) {
        std::cerr << "ERROR: could not listen on " << host << ":" << port
                  << " (is another program using that port? try --port 8081)\n";
        return 1;
    }
    return 0;
}
