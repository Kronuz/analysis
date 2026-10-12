# analysis

Text to index terms for C++20, reproducing Xapian's `TermGenerator`: the same words,
case folding, `Z`-prefixed stems, stop strategies, CJK n-grams, wdf, and positions.
Header-only; depends on [unicode](https://github.com/Kronuz/unicode) and
[snowball](https://github.com/Kronuz/snowball).

## Usage

```cpp
#include "analysis.hh"

snowball::stemmer english("english");
analysis::term_generator g;
g.stemmer = &english;                       // stem_strategy::some by default
g.index_text("The quick brown foxes", [](std::string_view term, std::uint32_t wdf, std::uint32_t pos) {
	// the@1, quick@2, brown@3, foxes@4, and positionless Zthe, Zquick, Zbrown, Zfox
}, {.wdf_inc = 1, .prefix = "", .positions = true});
```

`term_generator` holds the strategies (`stem`, `stop`, `cjk_ngram`, `max_word_length`),
a non-owning `stemmer` and `stop_words`, and the running position (`termpos`,
`set_termpos`, `increase_termpos`). The sink receives each term occurrence with its wdf
increment and position (0 for none); the view is valid during the call.

## Rules

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

## Performance

Indexing 10,000 English Wikipedia articles (9.6 MB) into per-document maps of terms to
wdf and positions, best of three, on an Intel i9-9980HK, Apple clang `-O3`
(`bench/bench.cc`), against Xapian 2.1.0's `TermGenerator` into `Xapian::Document`
on the same text:

| Strategy | analysis | Xapian | Ratio |
|---|---:|---:|---:|
| `none` | 29.4 MB/s | 16.1 MB/s | 1.8x |
| `some`, English stemmer | 12.0 MB/s | 6.7 MB/s | 1.8x |

## Build and test

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
build/analysis_bench CORPUS_JSONL N
```

## License

MIT. See [LICENSE](LICENSE).
