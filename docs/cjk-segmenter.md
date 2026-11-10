# cjk::segmenter: ICU's word breaks without ICU

`analysis::cjk::segmenter` (`cjk/segmenter.hh`, library `analysis::cjk`) splits UTF-8
text into the segments between word boundaries exactly as ICU 78.3's word break
iterator does in the root locale, iterating forward: ICU's UAX #29 word rules, with
runs of Han and kana divided by ICU's Chinese/Japanese dictionary. Each segment carries
the kind ICU's rule status gives its end (none, number, letter, ideo). ICU is not linked;
it is the oracle in `tools/`.

## How it works

```
 text --> rule state machine --> segment [begin, end), kind, read a dictionary char?
                                      | yes
                                      v
            runs of Han/kana --> NFKC --> least-cost path over cjdict words --> boundaries inside
            other dictionary runs (Hangul, ...) --> left whole
```

Rules decide every boundary first; the dictionary only adds boundaries inside a rule
segment, and every piece of a divided segment has the segment's kind.

### Rules: ICU's compiled state machine

ICU compiles `word.txt` (UAX #29 tailored: Han, kana and Hangul are left out of
`ALetter`, and runs of them are chained for the dictionary) into a deterministic state
machine over character categories. `tools/gen_tables.cc` asks ICU for it
(`RuleBasedBreakIterator::getBinaryRules`) and writes `cjk/tables.hh`:

- each code point's class in a three-stage table (18,320 bytes): its category (31 of
  them; 24 and up are dictionary characters) and three flags the dictionary path reads:
  membership in the Chinese/Japanese engine's set, Script=Common, and "NFKC may change
  it";
- the transitions, 58 states by 31 categories in bytes, whether each state accepts, and
  the status of its rule group.

`rule_boundary` is ICU's `handleNext`: from the last boundary, feed categories until
the stop state (end of text is category 1), and return the end of the longest match
with the status of the state that accepted it, or one character with status 0 when none
did. The generator fails when the rules need what the runtime lacks (look-ahead rules,
`{bof}`, 16-bit rows), so a new ICU cannot change behavior silently. Writing the rules
by hand instead would mean reproducing ICU's rule chaining and status sets; taking the
machine ICU runs makes the rule half exact by construction.

### Dictionary runs

When the machine read a dictionary character, `divide_segment` walks the segment as
ICU's `DictionaryCache::populateDictionary` does: a run of the Chinese/Japanese engine's
characters (`[[:Han:][:Hiragana:][:Katakana:]ーｰﾞﾟ]`) goes to the
dictionary; any other dictionary character starts a run left whole, as ICU's
`UnhandledEngine` skips its script (all of Script=Common when the run starts on one).
ICU has no Korean dictionary in its data, so Hangul is never divided.

`divide_range` is `CjkBreakEngine::divideUpDictionaryRange`:

1. **NFKC.** A run with no flagged character is already NFKC and is used as it is.
   Otherwise it is normalized fragment by fragment (a character and the ones with
   no normalization boundary before them: U+FF9E, U+FF9F, U+16FF0, U+16FF1), from tables
   of the run characters' decompositions (1,475), the combining classes of the four
   non-starters they produce, and their 58 compositions. Every normalized character maps
   to its fragment's start in the original text; an unchanged run maps one to one.
2. **Least cost.** `cost[i]` is the cheapest segmentation of the first `i` characters.
   From each reached `i`, every dictionary word starting there (up to 20 UTF-16 code
   units) offers `cost[i] + its cost`; a character no one-character word starts offers
   255; a run of up to 19 katakana starting there offers ICU's length cost (984, 408,
   240, 204, 252, 300, 372, 480, then 8,192). Ties keep the first offer, in ICU's
   order, and costs add in 32 bits as ICU's do.
3. **Boundaries.** Walk the best path back, map each position to the original text,
   drop one that maps where the previous did (inside an expanded fragment), and keep
   only those strictly inside the run.

## The dictionary: an FST over dense ids

`data/cjdict.txt` is ICU 78.3's dictionary unchanged: 315,964 words with costs 27 to 251
(sha256 `e73fd72048981d0cc13e9dc436a7eaba07ffb6eff58c8a59dc75c1df746663a0`). At build
time `tools/cjdict_gen.cc` parses it as ICU's `gendict` does and builds a
[Kronuz/fst](https://github.com/Kronuz/fst) that `cjk/segmenter.cc` includes as
`cjdict.inc`:

- Each of the 16,037 characters the words use gets a dense id, most frequent first; the
  first 194 ids are one byte, the rest a lead byte and a trail byte. A per-code-point id
  table (three stages, 45,664 bytes) maps text to ids.
- The FST maps id-encoded words to costs, sharing prefixes and suffixes (2,042,117
  bytes). A lookup at position `i` walks it one id at a time and reports each final
  state passed: all words starting at `i`, shortest first, in one walk.

Built in: 2,087,781 bytes, 4.0% more than ICU's own `cjdict.dict` (a UCharsTrie,
2,007,142 bytes). Measured alternatives on the same words, with the fst builder:

| Keys | FST bytes |
|---|---:|
| UTF-8 words | 2,531,287 |
| UTF-16BE words | 2,247,354 |
| dense ids, cost as two trailing labels instead of outputs | 2,201,764 |
| dense ids, costs as outputs (chosen) | 2,042,117 |

Most of the remaining bytes are the costs: the same FST without them is 1,576,423
bytes, since 208 distinct costs at the leaves keep suffixes apart. A level-order trie
with costs in a byte array indexed by rank would be about 1.4 MB by estimate; it is a
second static-dictionary mechanism, so it belongs in Kronuz/fst if it is ever built.

## Speed

`analysis_icu_compare bench 30` on the three corpora, x86_64 macOS, Release, in an
exclusive hold (job 261110-021524-d131bd at analysis:8ce3f0c): best of 30 rounds in MB/s
of UTF-8 input, median in parentheses. ICU is measured on UTF-8 and on UTF-16 input; the
word counts of all three agree.

| Corpus | Ours | ICU (UTF-8) | ICU (UTF-16) | Ours / ICU UTF-8 |
|---|---:|---:|---:|---:|
| zh | 23.48 (19.44) | 8.38 (6.90) | 10.75 (8.91) | 2.80x |
| ja | 19.38 (15.52) | 9.25 (7.49) | 11.69 (9.47) | 2.10x |
| ko | 111.05 (92.39) | 26.66 (22.23) | 38.53 (31.88) | 4.17x |

The segmenter's object code is 2,146,096 bytes, 2,127,876 of them the dictionary.

## Verified against ICU

All with ICU 78.3 (`tools/icu_compare.cc`, built with `-DANALYSIS_ICU_TOOLS=ON`):

- `vectors`: ICU's segments of 35 curated texts (`test/vectors/cjk-cases.txt`: every
  UAX #29 rule, emoji, regional indicators, Hangul, halfwidth katakana with voicing
  marks, compatibility ideographs, supplementary Han, katakana runs around 20, a cost tie
  ICU breaks by order) and 3,000 random ones, recorded in `test/vectors/cjk.jsonl` and
  replayed by ctest.
- `corpus`: the public-domain Chinese, Japanese and Korean corpora in `test/corpora`
  (110,000 characters each), every boundary and kind. `lines` records ICU's segments of
  each corpus line in `test/vectors/cjk-corpora.txt` (203,702 segments), which ctest
  replays.
- `sweep`: every code point, alone and doubled, in 14 contexts (31 million texts).
- `dict`: ICU's compiled `cjdict.dict` has exactly the words and costs of
  `data/cjdict.txt`, and the built FST answers every one of them; ctest also checks every
  word and 104,954 non-word prefixes against the text.
- `tools/cjk_mutants.sh BUILD_DIR` breaks the segmenter twelve ways (cost ties, the
  unknown-character and katakana costs, NFKC composition, position mapping, the
  Common-script skip, rule status, longest match) and checks ctest fails on each.

## Where it differs from ICU

- **Thai, Lao, Khmer, Myanmar** and the other Line_Break=Complex_Context scripts: ICU
  divides them with per-script dictionaries or LSTM models; the segmenter leaves their
  runs whole (`test/cjk_test.cc` pins that). The sweep counts these differences apart, but
  its 868 texts are the isolated code points that differ, not a measure of the divergence:
  real Thai or Lao text differs at nearly every word boundary.
- **Invalid UTF-8**, other than encoded surrogates (read as their code points), reads as
  ISO-8859-1 one byte at a time, as in `term_generator`; ICU reads U+FFFD. Boundaries fall on the same bytes only for valid text.
- **ICU's engine loading.** The model is ICU after its Chinese/Japanese engine has
  loaded. A process whose first dictionary character is U+30FC, U+FF70, U+FF9E or U+FF9F
  (Script=Common) has ICU skip it as unhandled instead; the oracle loads the engine first.
- **Forward only**, and no phrase breaking (`ja@lw=phrase`), no locale tailoring.

## Regenerating

- `cjk/tables.hh`: `analysis_gen_tables > cjk/tables.hh` with the ICU to match; then
  record new vectors (`analysis_icu_compare vectors test/vectors/cjk-cases.txt 3000 >
  test/vectors/cjk.jsonl`, `analysis_icu_compare lines test/corpora/{zh,ja,ko}.txt >
  test/vectors/cjk-corpora.txt`) and rerun `corpus`, `sweep` and `dict`.
- `data/cjdict.txt`: `icu4c/source/data/brkitr/dictionaries/cjdict.txt` of the same ICU
  release.
