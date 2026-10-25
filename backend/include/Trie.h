// ============================================================================
//  Trie.h - a prefix tree ("Trie") of dictionary words.
//
//  WHAT IS A TRIE?
//  ---------------
//  Every node stands for ONE letter. Walking from the root down to a node spells
//  a prefix. Words that share a beginning share the same nodes:
//
//            (root)
//              |
//              c
//              |
//              a
//           /   |
//          t    r        "cat", "car"  ->  c-a-t  and  c-a-r  share "c-a"
//
//  A node with isWord = true marks the end of a real word.
//
//  WHY USE IT FOR SPELL CHECKING?
//  ------------------------------
//  Suggesting corrections means finding dictionary words within a small EDIT
//  DISTANCE of the misspelled word. Instead of measuring the distance to all
//  44,000 words, we walk the trie once and keep a row of the edit-distance table
//  per node. As soon as a branch can no longer get close enough, we skip the
//  WHOLE branch (thousands of words) in one step. That pruning is the speed-up.
// ============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

constexpr int TRIE_ALPHABET = 27;   // 'a'..'z' = 0..25, apostrophe (') = 26

struct TrieNode {
    std::unique_ptr<TrieNode> children[TRIE_ALPHABET];  // nullptr = no such letter here
    bool isWord = false;     // does a dictionary word end at this node?
    int freq = 0;            // frequency score of that word (only if isWord)
    size_t wordCount = 0;    // number of words in this subtree (including this node)
};

// A dictionary word found by a search.
struct FuzzyMatch {
    std::string word;
    int distance = 0;
    int freq = 0;
};

// How much work a fuzzy search did (shown in the UI to prove the pruning works).
struct FuzzyStats {
    size_t nodesVisited = 0;    // nodes whose edit-distance row we computed
    size_t subtreesPruned = 0;  // branches we skipped entirely
    size_t wordsPruned = 0;     // words that were inside those skipped branches
};

// ---- structures used by the "Trie Explorer" page ---------------------------
struct TrieChildInfo {
    char ch = 0;
    bool isWord = false;
    int freq = 0;
    size_t subtreeWords = 0;
    size_t childCount = 0;
};

struct TrieNodeInfo {
    bool exists = false;
    std::string prefix;
    bool isWord = false;
    int freq = 0;
    size_t subtreeWords = 0;
    std::vector<TrieChildInfo> children;
};

struct TrieStats {
    size_t nodeCount = 0;
    size_t wordCount = 0;
    size_t maxDepth = 0;
    size_t leafCount = 0;
    double avgBranching = 0.0;               // children per internal node
    size_t memoryBytes = 0;                  // rough estimate
    std::vector<size_t> nodesPerDepth;       // index = depth
};

class Trie {
public:
    Trie();

    // Letter <-> slot number. charToIndex returns -1 for characters we do not store.
    static int charToIndex(char c);
    static char indexToChar(int index);

    // Adds a word (lower-case a-z and '). Returns true if it was new.
    bool insert(const std::string& word, int freq);

    // Node reached by following `prefix`, or nullptr if no word starts with it.
    const TrieNode* find(const std::string& prefix) const;

    bool contains(const std::string& word) const;

    // All dictionary words within `maxDistance` edits of `target` (unsorted).
    std::vector<FuzzyMatch> fuzzySearch(const std::string& target, int maxDistance,
                                        FuzzyStats* stats = nullptr) const;

    // Most common words that start with `prefix`.
    std::vector<FuzzyMatch> autocomplete(const std::string& prefix, size_t limit) const;

    TrieNodeInfo describe(const std::string& prefix) const;
    TrieStats computeStats() const;

    size_t nodeCount() const { return nodeCount_; }
    size_t wordCount() const { return wordCount_; }

private:
    std::unique_ptr<TrieNode> root_;
    size_t nodeCount_ = 1;   // the root counts
    size_t wordCount_ = 0;
};
