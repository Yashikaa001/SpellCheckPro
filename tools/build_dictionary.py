#!/usr/bin/env python3
"""
build_dictionary.py - OPTIONAL helper that was used to generate data/dictionary.txt

You do NOT need to run this to use SpellCheck Pro: the finished dictionary is
already included in data/dictionary.txt.

What it does
------------
1. Reads one or more plain word lists (one word per line).
2. Keeps only lowercase a-z words.
3. Looks up how common each word is in everyday English using the `wordfreq`
   package ("Zipf frequency", roughly 1.0 = very rare ... 7.5 = "the").
4. Keeps words that are common enough, adds a few contractions, and writes
   "word<TAB>score" lines, where score = Zipf frequency * 100 (an integer).

The backend uses that score to rank suggestions (more common words first).

Usage
-----
    pip install wordfreq
    python tools/build_dictionary.py wordlist1.txt wordlist2.txt -o data/dictionary.txt
"""

import argparse
import re
import sys

from wordfreq import zipf_frequency

# Words rarer than this are dropped (keeps the dictionary small and "everyday").
MIN_ZIPF = 2.3
# Three-letter strings in big word lists are mostly junk (abbreviations, Scrabble
# oddities), so they must be clearly common - or be on the RARE_THREE whitelist.
MIN_ZIPF_THREE = 3.0
RARE_THREE = set(
    "ebb eel elk emu ewe gnu hag hem imp ire irk ilk jig jot keg lug lye mow nab nag nit "
    "nub oar oat ohm orb ova pox ply pry pug rut sax sob sod tot tux urn vex wad wag wok "
    "wry yak yam yew zap".split()
)

# Two-letter words that really are English words (word lists contain many junk
# two-letter strings, so we whitelist instead).
TWO_LETTER = set(
    "am an as at be by do go he hi if in is it me my no of oh ok on or so to up us we "
    "ah ax ex ha hm la lo ma mr ms pa pi um uh yo".split()
)

# Abbreviations and very common *misspellings* that sneak into word lists
# (frequency data is built from real internet text, which contains typos!).
BLACKLIST = set(
    "www com org gov edu inc llc ltd http https html php css xml pdf "
    "wich alot thats dont cant ive youre teh adn thier freind recieve definately "
    "seperate occured untill becuase wierd".split()
)

# Everyday words that word lists often lack: months, days, places, tech words.
EXTRAS = """
january february march april may june july august september october november december
monday tuesday wednesday thursday friday saturday sunday
america american europe european asia asian africa african australia australian
canada canadian china chinese india indian japan japanese france french germany german
italy italian spain spanish russia russian brazil mexico england english britain british
london paris tokyo berlin washington california texas york
email online internet website website webpage blog app apps wifi smartphone username
password login download upload software hardware
christmas easter halloween
""".split()

CONTRACTIONS = """
don't doesn't didn't can't won't isn't aren't wasn't weren't hasn't haven't hadn't
wouldn't couldn't shouldn't mustn't needn't i'm i've i'll i'd you're you've you'll you'd
he's she's it's we're we've we'll we'd they're they've they'll they'd that's there's
here's what's let's who's how's where's when's why's ain't y'all o'clock ma'am
""".split()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("wordlists", nargs="+", help="plain text word lists")
    parser.add_argument("-o", "--output", default="data/dictionary.txt")
    args = parser.parse_args()

    candidates = set()
    for path in args.wordlists:
        with open(path, encoding="utf-8") as f:
            for line in f:
                w = line.strip().lower()
                if re.fullmatch(r"[a-z]+", w):
                    candidates.add(w)

    words = {}

    def consider(w, minimum):
        if w in BLACKLIST:
            return
        if len(w) == 1 and w not in ("a", "i"):
            return
        if len(w) == 2 and w not in TWO_LETTER:
            return
        if len(w) == 3 and w not in RARE_THREE:
            minimum = max(minimum, MIN_ZIPF_THREE)
        z = zipf_frequency(w, "en")
        if z >= minimum:
            words[w] = int(round(z * 100))

    for w in candidates:
        consider(w, MIN_ZIPF)

    # Extra everyday words (months, countries, ...) that the word lists lack.
    for w in EXTRAS:
        if w not in words:
            words[w] = max(int(round(zipf_frequency(w, "en") * 100)), 300)

    for c in CONTRACTIONS:
        words[c] = 520

    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        out.write("# SpellCheck Pro dictionary: word<TAB>frequency score (Zipf * 100)\n")
        for w in sorted(words):
            out.write(f"{w}\t{words[w]}\n")

    print(f"Wrote {len(words)} words to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
