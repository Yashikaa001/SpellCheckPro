// ============================================================================
//  SpellCheck Pro - program entry point.
//
//  What happens when you start the program:
//    1. Find the project folder (the one that contains data/ and frontend/).
//    2. Load the dictionary into the Hash Table and the Trie.
//    3. Start the web server and wait for requests from the dashboard.
//
//  Usage:   spellcheck [--port 8080] [--host 127.0.0.1] [--root <project folder>]
// ============================================================================
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Api.h"

namespace {

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// The program may be started from the project folder, from backend/, from build/
// or from VS Code's debugger. Look upwards for a folder that has data/dictionary.txt.
std::string findProjectRoot(const std::string& argv0) {
    // "./" means "the current folder". (An EMPTY string is reserved for "not found".)
    std::vector<std::string> prefixes = {"./", "../", "../../", "../../../"};
    for (const std::string& p : prefixes) {
        if (fileExists(p + "data/dictionary.txt")) return p;
    }
    // Also try relative to the executable's own location (e.g. double-clicking the .exe).
    std::string dir = argv0;
    const size_t slash = dir.find_last_of("/\\");
    dir = (slash == std::string::npos) ? std::string("./") : dir.substr(0, slash + 1);
    for (const std::string& p : prefixes) {
        if (fileExists(dir + p + "data/dictionary.txt")) return dir + p;
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string root;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--root" && i + 1 < argc) root = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: spellcheck [--port 8080] [--host 127.0.0.1] [--root <project folder>]\n";
            return 0;
        }
    }

    if (root.empty()) root = findProjectRoot(argv[0]);
    if (root.empty()) {
        std::cerr << "ERROR: could not find data/dictionary.txt.\n"
                  << "Run the program from the SpellCheckPro folder, or pass --root <folder>.\n";
        return 1;
    }
    if (!root.empty() && root.back() != '/' && root.back() != '\\') root += '/';

    AppState state;
    state.webDir = root + "frontend";
    state.dictionaryPath = root + "data/dictionary.txt";
    state.customPath = root + "data/custom_words.txt";
    state.historyPath = root + "data/history.tsv";
    state.selfTestPath = root + "data/selftest.tsv";

    std::cout << "\n  SpellCheck Pro\n  ==============\n";
    std::cout << "  Project folder : " << root << "\n";

    const auto t0 = std::chrono::steady_clock::now();
    state.baseWordCount = state.checker.loadDictionary(state.dictionaryPath);
    const size_t custom = state.checker.loadCustomWords(state.customPath);
    const auto t1 = std::chrono::steady_clock::now();
    state.history.load(state.historyPath);

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "  Dictionary     : " << state.baseWordCount << " words (+" << custom << " custom) loaded in "
              << static_cast<long>(ms) << " ms\n";
    std::cout << "  Trie nodes     : " << state.checker.trie().nodeCount() << "\n";
    std::cout << "  Hash buckets   : " << state.checker.hashTable().bucketCount() << "\n";

    if (state.baseWordCount == 0) {
        std::cerr << "ERROR: the dictionary is empty or unreadable: " << state.dictionaryPath << "\n";
        return 1;
    }

    return runServer(state, host, port);
}
