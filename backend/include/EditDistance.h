// ============================================================================
//  EditDistance.h - how many single-letter edits turn one word into another?
//
//  The allowed edits (each costs 1):
//      substitute  - replace one letter           cat -> cut
//      insert      - add one letter               cat -> cart
//      delete      - remove one letter            cart -> cat
//      transpose   - swap two NEIGHBOURING letters   teh -> the
//
//  This is the "Damerau-Levenshtein" distance (optimal string alignment form).
//  It is calculated with DYNAMIC PROGRAMMING: a table where cell [i][j] holds the
//  distance between the first i letters of word A and the first j letters of word B.
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace edit {

// Plain distance (fast, no table returned).
int distance(const std::string& a, const std::string& b);

// One step of the cheapest transformation from A to B.
struct EditOp {
    char type;   // 'M' match, 'S' substitute, 'I' insert, 'D' delete, 'T' transpose
    int i;       // index in A (for I: where the letter is inserted)
    int j;       // index in B (for D: position in B where the letter would have been)
    char from;   // letter in A (0 if none)
    char to;     // letter in B (0 if none)
};

// Full result with the DP table and the path through it (used by the visualiser).
struct Alignment {
    int distance = 0;
    std::vector<std::vector<int>> matrix;        // (len(A)+1) x (len(B)+1)
    std::vector<EditOp> ops;                     // from the first letter to the last
    std::vector<std::pair<int, int>> path;       // matrix cells on the cheapest path
};

Alignment align(const std::string& a, const std::string& b);

}  // namespace edit
