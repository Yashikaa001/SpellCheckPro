#include "EditDistance.h"

#include <algorithm>

namespace edit {

// ---------------------------------------------------------------------------
//  Fast distance - builds the table but throws it away.
// ---------------------------------------------------------------------------
int distance(const std::string& a, const std::string& b) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1, 0));

    for (int i = 0; i <= n; ++i) d[i][0] = i;   // turning A's first i letters into "" = i deletions
    for (int j = 0; j <= m; ++j) d[0][j] = j;   // turning "" into B's first j letters = j insertions

    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int best = std::min({d[i - 1][j] + 1,          // delete
                                 d[i][j - 1] + 1,          // insert
                                 d[i - 1][j - 1] + cost}); // substitute (or match)
            // transposition of two neighbouring letters
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                best = std::min(best, d[i - 2][j - 2] + 1);
            }
            d[i][j] = best;
        }
    }
    return d[n][m];
}

// ---------------------------------------------------------------------------
//  Full alignment - same table, plus a walk back from the bottom-right corner
//  to the top-left corner to discover WHICH edits were used.
// ---------------------------------------------------------------------------
Alignment align(const std::string& a, const std::string& b) {
    const int n = static_cast<int>(a.size());
    const int m = static_cast<int>(b.size());

    Alignment result;
    auto& d = result.matrix;
    d.assign(n + 1, std::vector<int>(m + 1, 0));
    for (int i = 0; i <= n; ++i) d[i][0] = i;
    for (int j = 0; j <= m; ++j) d[0][j] = j;

    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int best = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                best = std::min(best, d[i - 2][j - 2] + 1);
            }
            d[i][j] = best;
        }
    }
    result.distance = d[n][m];

    // ---- backtrace: walk from (n, m) back to (0, 0) -------------------------
    int i = n, j = m;
    result.path.emplace_back(i, j);
    while (i > 0 || j > 0) {
        const int here = d[i][j];
        if (i > 0 && j > 0 && a[i - 1] == b[j - 1] && here == d[i - 1][j - 1]) {
            result.ops.push_back({'M', i - 1, j - 1, a[i - 1], b[j - 1]});
            --i; --j;
        } else if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                   here == d[i - 2][j - 2] + 1) {
            result.ops.push_back({'T', i - 2, j - 2, a[i - 2], a[i - 1]});
            result.path.emplace_back(i - 1, j - 1);
            i -= 2; j -= 2;
        } else if (i > 0 && j > 0 && here == d[i - 1][j - 1] + 1) {
            result.ops.push_back({'S', i - 1, j - 1, a[i - 1], b[j - 1]});
            --i; --j;
        } else if (i > 0 && here == d[i - 1][j] + 1) {
            result.ops.push_back({'D', i - 1, j, a[i - 1], 0});
            --i;
        } else {
            result.ops.push_back({'I', i, j - 1, 0, b[j - 1]});
            --j;
        }
        result.path.emplace_back(i, j);
    }
    std::reverse(result.ops.begin(), result.ops.end());
    std::reverse(result.path.begin(), result.path.end());
    return result;
}

}  // namespace edit
