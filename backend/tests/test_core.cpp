// ============================================================================
//  test_core.cpp - checks that the data structures give correct answers.
//
//  Build & run:   make test
//
//  The most important test compares the fast Trie search with a slow "brute force"
//  search (edit distance against EVERY word). Both must find exactly the same words.
// ============================================================================
#include <iostream>
#include <map>
#include <random>
#include <string>

#include "EditDistance.h"
#include "HashTable.h"
#include "SpellChecker.h"
#include "TextUtils.h"
#include "Trie.h"

static int failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cout << "  FAIL  " << #cond << "   (line " << __LINE__ << ")\n";  \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static void testEditDistance() {
    std::cout << "edit distance\n";
    CHECK(edit::distance("kitten", "sitting") == 3);
    CHECK(edit::distance("teh", "the") == 1);        // swapped neighbours count as ONE edit
    CHECK(edit::distance("", "abc") == 3);
    CHECK(edit::distance("same", "same") == 0);
    CHECK(edit::align("kitten", "sitting").distance == 3);
}

static void testHashTable() {
    std::cout << "hash table\n";
    HashTable t(7, HashFunction::DJB2);   // tiny table -> lots of collisions
    for (const char* w : {"apple", "banana", "cherry", "date", "elder", "fig", "grape", "honey"}) {
        DictEntry e;
        e.word = w;
        CHECK(t.insert(e));
    }
    DictEntry dup;
    dup.word = "apple";
    CHECK(!t.insert(dup));
    CHECK(t.size() == 8);
    CHECK(t.contains("grape"));
    CHECK(!t.contains("grapes"));
    CHECK(t.stats().collisions == 8 - t.stats().usedBuckets);

    t.rebuild(101, HashFunction::FNV1A);   // re-hash everything
    CHECK(t.size() == 8 && t.contains("honey") && !t.contains("zebra"));
    CHECK(computeHash("abc", HashFunction::SUM) == 'a' + 'b' + 'c');
}

static void testTokenizer() {
    std::cout << "tokenizer\n";
    auto tokens = text::tokenize("Don't panic, it's 42 degrees at me@x.com!");
    CHECK(tokens.size() >= 6);
    CHECK(tokens[0].text == "Don't" && tokens[0].start == 0 && tokens[0].end == 5);
    bool sawNumber = false, sawEmail = false;
    for (const auto& t : tokens) {
        if (t.text.empty() && !t.checkable) sawNumber = true;    // "42" has no letters
        if (t.text == "me" && !t.checkable) sawEmail = true;
    }
    CHECK(sawNumber && sawEmail);

    // An emoji takes TWO UTF-16 units, so the next word starts 3 units later, not 2.
    tokens = text::tokenize("\xF0\x9F\x98\x80 hi");
    CHECK(tokens.size() == 1 && tokens[0].start == 3 && tokens[0].end == 5);
    CHECK(text::matchCase("the", "Teh") == "The");
    CHECK(text::matchCase("the", "TEH") == "THE");
}

static void testTrieAgainstBruteForce(const std::string& dictPath) {
    std::cout << "trie fuzzy search vs brute force (this takes a few seconds)\n";
    SpellChecker sc;
    const size_t loaded = sc.loadDictionary(dictPath);
    CHECK(loaded > 1000);
    if (loaded == 0) return;

    std::mt19937 rng(7);
    const auto& words = sc.entries();
    int mismatches = 0;
    const int rounds = 150;
    for (int round = 0; round < rounds; ++round) {
        // Take a real word and damage it with 1-3 random edits.
        std::string w = words[rng() % words.size()].word;
        for (unsigned e = rng() % 3 + 1; e > 0 && !w.empty(); --e) {
            const size_t pos = rng() % w.size();
            switch (rng() % 4) {
                case 0: w[pos] = static_cast<char>('a' + rng() % 26); break;
                case 1: w.erase(pos, 1); break;
                case 2: w.insert(pos, 1, static_cast<char>('a' + rng() % 26)); break;
                default: if (pos + 1 < w.size()) std::swap(w[pos], w[pos + 1]);
            }
        }
        if (w.empty()) continue;

        const int maxDist = 1 + static_cast<int>(rng() % 2);
        std::map<std::string, int> fast, slow;
        for (const auto& m : sc.trie().fuzzySearch(w, maxDist)) fast[m.word] = m.distance;
        for (const auto& e : words) {
            const int d = edit::distance(w, e.word);
            if (d <= maxDist) slow[e.word] = d;
        }
        if (fast != slow) {
            ++mismatches;
            std::cout << "  mismatch for \"" << w << "\" (max " << maxDist << "): trie found " << fast.size()
                      << ", brute force found " << slow.size() << "\n";
        }
    }
    CHECK(mismatches == 0);
}

int main(int argc, char** argv) {
    const std::string dictPath = argc > 1 ? argv[1] : "data/dictionary.txt";
    testEditDistance();
    testHashTable();
    testTokenizer();
    testTrieAgainstBruteForce(dictPath);
    std::cout << (failures == 0 ? "\nAll tests passed.\n" : "\nSome tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
