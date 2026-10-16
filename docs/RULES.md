# analysis tokenization rules

The exact behavior `term_generator` reproduces, and how it is verified.

## The rules

These are Xapian 2.1.0's, recorded as vectors by an oracle that links Xapian and
reproduced here without its source.

- **Text** is UTF-8. A byte that does not start a valid, shortest-form sequence reads
  as ISO-8859-1.
- **Word characters** are letters, marks, numbers, and connector punctuation (`_`).
  A word is a run of them, lowercased by simple case mapping.
- **Infixes** join two word characters into one word:
  - `&`, `'`, `·` (U+00B7), `״` (U+05F4), `‧` (U+2027), and the apostrophes `’`
    (U+2019) and `‛` (U+201B), which join as `'`, unless both neighbors are
    decimal digits. The soft hyphen joins the same way and is dropped.
  - `,` `.` `;` and U+037E, U+0589, U+060D, U+07F8, U+2044, U+FE10, U+FE13, U+FE14
    only between decimal digits (`3.14`, `1,000`). U+200B joins digits and is dropped.
  - U+200C, U+200D, U+2060, and U+FEFF are dropped wherever they appear.
- **Suffixes**: one to three `+` or `#` after a word stay on it when no word
  character follows (`c++`, `c#`, `c+++`); four or more stay off.
- **Acronyms**: single ASCII capitals separated by periods (`U.S.A.`, `A.B`) form one
  word (`usa`) unless a word character follows.
- **Length**: a word over 64 bytes after lowercasing produces no terms and no position.
- **Positions**: each word takes the next position; `positions = false` takes none.
- **Stemming**: `none` indexes the word; `some` adds a positionless `Z` + prefix +
  stem when the word starts with a letter; `all` indexes the stem; `all_z` indexes
  `Z` + prefix + stem. Without a stemmer, `some` adds nothing and `all` and `all_z`
  index nothing at all.
- **Stopping**: `all` drops a stop word with no position; `stemmed` drops only the
  `Z` stem under `some`; `none` drops nothing.
- **CJK n-grams** (`cjk_ngram`): word characters in the Chinese, Japanese, Korean,
  Thai, Lao, Myanmar, and Khmer blocks and the halfwidth forms index as positional
  unigrams plus positionless bigrams of adjacent pairs; other characters next to them
  form separate words.

## Correctness

The test replays 966 oracle vectors (46 texts under every stem strategy with and without
n-grams, and Xapiand's prefix, wdf, positionless, and stop configurations), checking
terms, wdf, positions, and the final position. It also checks two oracle sweeps over all
1,114,112 code points: each one doubled (not a word, word, or n-gram character), and
each non-word one between letters and digits, as a suffix, and in an acronym. It runs
clean under ASan and UBSan; `fuzz/fuzz_index.cc` is a libFuzzer target (6 million runs
clean).
