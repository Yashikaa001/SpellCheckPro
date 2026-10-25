# SpellCheck Pro

A spell checker with a **real C++ backend** and a dark, glass-style dashboard.

- **Hash table** (written from scratch, separate chaining) answers "is this word spelled correctly?"
- **Trie** (prefix tree) with **edit distance** finds the best corrections, and skips whole branches that cannot match.
- The C++ program is also the web server. The browser dashboard talks to it over a small JSON API. Nothing is simulated: every number on screen is measured by the C++ code.

## What you can do

| Page | What it does |
|---|---|
| **Dashboard** | Live stats (checks, accuracy, lookup speed), charts, a speed test (hash vs trie vs list scan), and an accuracy test on 112 known misspellings |
| **Spell checker** | Underlines mistakes as you type, click a word for suggestions, Fix all, ignore / add to dictionary, **Copy corrected text** |
| **Hash table** | Bucket heat map, collisions, rebuild with another hash function or size, step-by-step lookup |
| **Trie** | Draw the tree, trace a word through it, autocomplete, see how much work fuzzy search skips |
| **Edit distance** | The dynamic-programming table and the edits between two words |
| **Dictionary** | Search, filter, sort and page through 44,000+ words, add your own |
| **History** | Every check and every applied correction, saved to disk |

## Folder structure

```
SpellCheckPro/
├── backend/
│   ├── include/            C++ headers (read these first: each starts with a plain-English explanation)
│   │   ├── HashTable.h       hash table + 4 hash functions
│   │   ├── Trie.h            prefix tree + fuzzy search
│   │   ├── EditDistance.h    Damerau-Levenshtein distance
│   │   ├── SpellChecker.h    combines the above: check, suggest, benchmark
│   │   ├── TextUtils.h       tokenizer (splits text into words)
│   │   ├── Metrics.h         running statistics for the dashboard
│   │   ├── History.h         correction history (saved to data/history.tsv)
│   │   ├── Json.h            tiny JSON writer
│   │   └── Api.h             web server entry point
│   ├── src/                C++ implementations (+ main.cpp, Api.cpp = all endpoints)
│   ├── tests/test_core.cpp unit tests
│   └── third_party/httplib.h   cpp-httplib (single-file HTTP server, MIT license)
├── frontend/
│   ├── index.html
│   ├── css/style.css       dark theme, gradients, glass cards
│   └── js/                 one file per page + core.js (helpers) + charts.js (own chart library)
├── data/
│   ├── dictionary.txt      word <TAB> frequency score
│   ├── selftest.tsv        misspelling <TAB> correct word (used by the accuracy test)
│   ├── custom_words.txt    words you add (created automatically)
│   └── history.tsv         correction history (created automatically)
├── tools/build_dictionary.py   optional: how dictionary.txt was generated
├── .vscode/                tasks, debugger and IntelliSense settings
├── build.bat               one-click build + run on Windows
└── Makefile                build on Linux / macOS
```

## Windows + VS Code setup (step by step)

### 1. Install VS Code
Download it from <https://code.visualstudio.com> and install.
Open VS Code, press `Ctrl+Shift+X`, search for **C/C++** (by Microsoft) and click **Install**.

### 2. Install a C++ compiler (MSYS2 + MinGW-w64)
1. Download the installer from <https://www.msys2.org> and install it to the default folder `C:\msys64`.
2. When it finishes, open **"MSYS2 UCRT64"** from the Start menu and run:
   ```
   pacman -Syu
   ```
   (if the window closes, open it again and run the same command once more), then:
   ```
   pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain
   ```
   Press Enter to accept all, then `Y`.
3. Add the compiler to your **PATH**:
   - Press the Windows key, type **"environment variables"**, open *Edit the system environment variables*.
   - Click **Environment Variables**, select **Path** under *User variables*, click **Edit** → **New**, and add:
     ```
     C:\msys64\ucrt64\bin
     ```
   - Click OK on every window.
4. Open a **new** PowerShell window and check:
   ```
   g++ --version
   ```
   You should see a version number (13.x or newer is fine; anything that supports C++17 works).

### 3. Open the project
In VS Code: **File → Open Folder…** and choose the `SpellCheckPro` folder.

### 4. Build and run
Pick **one** of these:

- **Easiest:** double-click `build.bat`. It compiles, starts the server and opens your browser.
- **In VS Code:** press `Ctrl+Shift+B` (builds `build\spellcheck.exe`), then in the terminal (`Ctrl+``):
  ```
  .\build\spellcheck.exe
  ```
  and open <http://localhost:8080>.
- **Debugging:** press `F5`. VS Code builds a debug version and starts it under GDB. Click in the left margin of any `.cpp` file to set a breakpoint, then use the dashboard to trigger it.

You should see something like:

```
  SpellCheck Pro
  ==============
  Dictionary     : 44438 words (+0 custom) loaded in 70 ms
  Trie nodes     : 107905
  Dashboard ready  ->  http://127.0.0.1:8080/
```

Leave that window open while you use the dashboard. Press `Ctrl+C` to stop.

### Run the unit tests
`Terminal → Run Task… → Run unit tests`, then run `.\build\test_core.exe`. It checks the edit distance, the hash table, the tokenizer, and compares the trie's fuzzy search with a slow brute-force search (they must find exactly the same words).

### Linux / macOS
```
make          # builds build/spellcheck
make run      # build and start
make test     # unit tests
```
Then open <http://localhost:8080>.

### Command-line options
```
spellcheck --port 9000        use another port
spellcheck --host 0.0.0.0     allow other devices on your network to open the dashboard
spellcheck --root <folder>    project folder (only needed if you start it from somewhere unusual)
```

## How it works

### 1. Is the word correct? (hash table)
`HashTable.cpp` turns each word into a number (for example djb2: start at 5381, then `h = h*33 + letter` for each letter). `number % bucketCount` picks a bucket. Looking up a word means opening **one** bucket and comparing the few words inside. Two words in the same bucket are a **collision**; they are kept in a chain. The *Hash table* page lets you switch the hash function and bucket count, then rebuilds the real table in C++ so you can watch collisions change.

### 2. What did they mean? (trie + edit distance)
`Trie.cpp` stores every word letter by letter, so words with the same beginning share nodes. To find suggestions it walks the tree once and keeps one row of the edit-distance table per node. When every value in a row is above the limit (default 2 edits), no word below that node can match, so the whole branch is skipped. Typically only 5–8% of the 108,000 nodes are visited. A unit test proves the result is identical to checking all 44,000 words.

**Edit distance** (`EditDistance.cpp`) counts the fewest edits to turn one word into another: replace a letter, insert, delete, or swap two neighbours (so `teh → the` is 1 edit, not 2).

### 3. Which suggestion first?
Suggestions are sorted by edit distance. Ties are broken by how common the word is, with small adjustments (a different first letter is less likely; swapped letters are more likely). On the built-in test list the right word is first about 92% of the time and in the top 3 about 98% of the time.

### API (for curiosity)
The dashboard uses plain HTTP + JSON. You can try them in a browser:
- `http://localhost:8080/api/suggest?word=speling`
- `http://localhost:8080/api/hash/lookup?word=hello`
- `http://localhost:8080/api/trie/autocomplete?prefix=spel`
- `http://localhost:8080/api/edit?a=kitten&b=sitting`

The full endpoint list is at the top of `backend/src/Api.cpp`.

## Customising

- **Add words:** use the *Dictionary* page, or append `word<TAB>400` lines to `data/custom_words.txt`.
- **Change the dictionary:** edit `data/dictionary.txt` (format: `word<TAB>score`, higher score = more common). `tools/build_dictionary.py` shows how the included one was made.
- **Tune suggestion ranking:** see the `score` calculation in `SpellChecker::suggest` (`backend/src/SpellChecker.cpp`).
- **Change colours:** the CSS variables at the top of `frontend/css/style.css`.

## Troubleshooting

| Problem | Fix |
|---|---|
| `g++` is not recognised | Add `C:\msys64\ucrt64\bin` to PATH and open a **new** terminal |
| "could not listen on 127.0.0.1:8080" | Something else uses port 8080. Run with `--port 8081` |
| "could not find data/dictionary.txt" | Start the program from the `SpellCheckPro` folder, or pass `--root` |
| Windows Firewall prompt | Click *Cancel* / allow private networks only. The server listens on localhost by default |
| Dashboard says "Server offline" | The C++ window was closed. Start it again |
| Fonts look different offline | The page falls back to system fonts. Everything else works without internet |

## Credits
- HTTP server: [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT).
- Word list: public English word lists, filtered and ranked by real-world frequency using the `wordfreq` package.
