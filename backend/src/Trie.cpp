#include "Trie.h"

#include <algorithm>

Trie::Trie() : root_(new TrieNode()) {}

int Trie::charToIndex(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c == '\'') return 26;
    return -1;
}

char Trie::indexToChar(int index) {
    return index == 26 ? '\'' : static_cast<char>('a' + index);
}

// ---------------------------------------------------------------------------
//  insert: walk down the tree, creating missing nodes along the way.
// ---------------------------------------------------------------------------
bool Trie::insert(const std::string& word, int freq) {
    if (word.empty()) return false;
    for (char c : word) {
        if (charToIndex(c) < 0) return false;   // unsupported character
    }

    // First pass: is it already there?
    if (contains(word)) return false;

    // Second pass: create nodes and bump the subtree word counters on the way.
    TrieNode* node = root_.get();
    ++node->wordCount;
    for (char c : word) {
        const int idx = charToIndex(c);
        if (!node->children[idx]) {
            node->children[idx].reset(new TrieNode());
            ++nodeCount_;
        }
        node = node->children[idx].get();
        ++node->wordCount;
    }
    node->isWord = true;
    node->freq = freq;
    ++wordCount_;
    return true;
}

const TrieNode* Trie::find(const std::string& prefix) const {
    const TrieNode* node = root_.get();
    for (char c : prefix) {
        const int idx = charToIndex(c);
        if (idx < 0) return nullptr;
        node = node->children[idx].get();
        if (!node) return nullptr;
    }
    return node;
}

bool Trie::contains(const std::string& word) const {
    const TrieNode* node = find(word);
    return node != nullptr && node->isWord;
}

// ---------------------------------------------------------------------------
//  Fuzzy search
//
//  We walk the trie depth-first. For every node we compute one ROW of the
//  edit-distance table (the same table described in EditDistance.h):
//
//      row[j] = edit distance between the letters spelled so far (the path from
//               the root to this node) and the first j letters of the target.
//
//  The row of a node only depends on the row of its parent (and grandparent, for
//  the "swapped letters" rule), so each node costs just one short loop.
//
//  If a node spells a complete word and row[last] <= maxDistance -> it's a match.
//  If EVERY value in the row is already larger than maxDistance, no word below this
//  node can ever get close enough -> we skip the whole subtree (pruning).
// ---------------------------------------------------------------------------
namespace {

struct FuzzyContext {
    const std::string* target;
    int maxDistance;
    int m;                                   // target length
    std::vector<std::vector<int>> rows;      // rows[depth] = DP row for the node at that depth
    std::string prefix;                      // letters on the path from the root
    std::vector<FuzzyMatch>* out;
    FuzzyStats* stats;
};

int rowMinimum(const std::vector<int>& row) {
    return *std::min_element(row.begin(), row.end());
}

void fuzzyVisit(const TrieNode* node, int depth, FuzzyContext& ctx) {
    const std::string& target = *ctx.target;
    const int childDepth = depth + 1;

    for (int idx = 0; idx < TRIE_ALPHABET; ++idx) {
        const TrieNode* child = node->children[idx].get();
        if (!child) continue;

        const char ch = Trie::indexToChar(idx);
        ctx.prefix.push_back(ch);
        if (ctx.stats) ++ctx.stats->nodesVisited;

        // ---- compute this child's row from its parent's row -----------------
        const std::vector<int>& prev = ctx.rows[depth];
        std::vector<int>& row = ctx.rows[childDepth];
        row[0] = childDepth;
        for (int j = 1; j <= ctx.m; ++j) {
            const int cost = (target[j - 1] == ch) ? 0 : 1;
            int best = std::min({prev[j - 1] + cost,   // substitute / match
                                 prev[j] + 1,          // delete a letter from the path
                                 row[j - 1] + 1});     // insert a letter
            // swapped neighbouring letters: path ends "..xy", target has "yx"
            if (childDepth >= 2 && j >= 2 &&
                target[j - 1] == ctx.prefix[childDepth - 2] && target[j - 2] == ch) {
                best = std::min(best, ctx.rows[depth - 1][j - 2] + 1);
            }
            row[j] = best;
        }

        // ---- is this node a match? ------------------------------------------
        if (child->isWord && row[ctx.m] <= ctx.maxDistance) {
            ctx.out->push_back({ctx.prefix, row[ctx.m], child->freq});
        }

        // ---- go deeper, or prune the whole branch ---------------------------
        // A branch is hopeless when this row's best value is already too big AND the
        // previous row was at least at the limit (the previous row matters because a
        // swap of two letters can reach back two rows).
        const bool hopeless = rowMinimum(row) > ctx.maxDistance && rowMinimum(prev) >= ctx.maxDistance;
        const bool tooDeep = childDepth + 1 >= static_cast<int>(ctx.rows.size());
        if (hopeless || tooDeep) {
            if (ctx.stats && child->wordCount > (child->isWord ? 1u : 0u)) {
                ++ctx.stats->subtreesPruned;
                ctx.stats->wordsPruned += child->wordCount - (child->isWord ? 1 : 0);
            }
        } else {
            fuzzyVisit(child, childDepth, ctx);
        }

        ctx.prefix.pop_back();
    }
}

}  // namespace

std::vector<FuzzyMatch> Trie::fuzzySearch(const std::string& target, int maxDistance,
                                          FuzzyStats* stats) const {
    std::vector<FuzzyMatch> matches;
    if (stats) *stats = FuzzyStats();
    if (target.empty() || maxDistance < 0) return matches;

    FuzzyContext ctx;
    ctx.target = &target;
    ctx.maxDistance = maxDistance;
    ctx.m = static_cast<int>(target.size());
    // Words longer than target + maxDistance can never match, so that many rows are enough.
    ctx.rows.assign(ctx.m + maxDistance + 3, std::vector<int>(ctx.m + 1, 0));
    for (int j = 0; j <= ctx.m; ++j) ctx.rows[0][j] = j;   // root row: distance to "" is j
    ctx.out = &matches;
    ctx.stats = stats;

    fuzzyVisit(root_.get(), 0, ctx);
    return matches;
}

// ---------------------------------------------------------------------------
//  Autocomplete: collect every word below the prefix node, keep the most common.
// ---------------------------------------------------------------------------
namespace {
void collectWords(const TrieNode* node, std::string& prefix, std::vector<FuzzyMatch>& out) {
    if (node->isWord) out.push_back({prefix, 0, node->freq});
    for (int idx = 0; idx < TRIE_ALPHABET; ++idx) {
        if (const TrieNode* child = node->children[idx].get()) {
            prefix.push_back(Trie::indexToChar(idx));
            collectWords(child, prefix, out);
            prefix.pop_back();
        }
    }
}
}  // namespace

std::vector<FuzzyMatch> Trie::autocomplete(const std::string& prefix, size_t limit) const {
    std::vector<FuzzyMatch> words;
    const TrieNode* node = find(prefix);
    if (!node) return words;

    std::string path = prefix;
    collectWords(node, path, words);

    auto moreCommon = [](const FuzzyMatch& a, const FuzzyMatch& b) {
        if (a.freq != b.freq) return a.freq > b.freq;
        return a.word < b.word;
    };
    if (words.size() > limit) {
        std::partial_sort(words.begin(), words.begin() + static_cast<long>(limit), words.end(), moreCommon);
        words.resize(limit);
    } else {
        std::sort(words.begin(), words.end(), moreCommon);
    }
    return words;
}

// ---------------------------------------------------------------------------
//  describe: what does one node look like? (used by the Trie Explorer)
// ---------------------------------------------------------------------------
TrieNodeInfo Trie::describe(const std::string& prefix) const {
    TrieNodeInfo info;
    info.prefix = prefix;
    const TrieNode* node = find(prefix);
    if (!node) return info;

    info.exists = true;
    info.isWord = node->isWord;
    info.freq = node->freq;
    info.subtreeWords = node->wordCount;

    for (int idx = 0; idx < TRIE_ALPHABET; ++idx) {
        const TrieNode* child = node->children[idx].get();
        if (!child) continue;
        TrieChildInfo c;
        c.ch = indexToChar(idx);
        c.isWord = child->isWord;
        c.freq = child->freq;
        c.subtreeWords = child->wordCount;
        for (int k = 0; k < TRIE_ALPHABET; ++k) {
            if (child->children[k]) ++c.childCount;
        }
        info.children.push_back(c);
    }
    return info;
}

// ---------------------------------------------------------------------------
//  computeStats: walk the whole tree once (iteratively, with an explicit stack).
// ---------------------------------------------------------------------------
TrieStats Trie::computeStats() const {
    TrieStats s;
    s.wordCount = wordCount_;

    struct Item { const TrieNode* node; size_t depth; };
    std::vector<Item> stack;
    stack.push_back({root_.get(), 0});

    size_t internalNodes = 0;
    size_t totalChildren = 0;
    while (!stack.empty()) {
        Item it = stack.back();
        stack.pop_back();

        ++s.nodeCount;
        if (s.nodesPerDepth.size() <= it.depth) s.nodesPerDepth.resize(it.depth + 1, 0);
        ++s.nodesPerDepth[it.depth];
        s.maxDepth = std::max(s.maxDepth, it.depth);

        size_t kids = 0;
        for (int idx = 0; idx < TRIE_ALPHABET; ++idx) {
            if (const TrieNode* child = it.node->children[idx].get()) {
                ++kids;
                stack.push_back({child, it.depth + 1});
            }
        }
        if (kids == 0) {
            ++s.leafCount;
        } else {
            ++internalNodes;
            totalChildren += kids;
        }
    }
    s.avgBranching = internalNodes ? static_cast<double>(totalChildren) / static_cast<double>(internalNodes) : 0.0;
    s.memoryBytes = s.nodeCount * sizeof(TrieNode);
    return s;
}
