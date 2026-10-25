// ============================================================================
//  Api.h - connects the C++ spell checker to the browser.
//
//  runServer() starts a small web server that
//     * serves the dashboard files (frontend/)          ->  http://localhost:8080/
//     * answers JSON requests under /api/...            ->  used by the dashboard's JavaScript
//
//  The list of endpoints is documented at the top of Api.cpp.
// ============================================================================
#pragma once

#include <string>

#include "History.h"
#include "Metrics.h"
#include "SpellChecker.h"

// Everything the server needs, bundled together.
struct AppState {
    SpellChecker checker;
    Metrics metrics;
    History history;

    std::string webDir;          // folder with index.html, css/, js/
    std::string dictionaryPath;  // data/dictionary.txt
    std::string customPath;      // data/custom_words.txt   (words the user added)
    std::string historyPath;     // data/history.tsv
    std::string selfTestPath;    // data/selftest.tsv
    size_t baseWordCount = 0;    // words in dictionary.txt (without custom words)
};

// Starts the server and blocks until it stops. Returns a process exit code.
int runServer(AppState& state, const std::string& host, int port);
