# AGENTS.md — analysis

## Repo map

```
analysis.hh            term_generator, stopper, is_word_char, is_ngram_char.
test/test.cc           Replays test/vectors (oracle output) and the code-point sweeps.
test/vectors/          termgen.jsonl, termgen-xapiand.jsonl, chars.txt, infix.txt.
fuzz/fuzz_index.cc     libFuzzer: valid UTF-8 terms, bounded length, increasing positions.
bench/bench.cc         MB/s and ns per term over a JSON-lines corpus.
```

## Invariants

- Behavior equals Xapian's TermGenerator: every vector and sweep passes. The vectors
  come from strata-oracle, which links Xapian; never read Xapian's source to change a
  rule here. Record a new vector instead.
- The sink's term view is valid only during the call.

## Verify a change

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
