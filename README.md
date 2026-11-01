# analysis

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/analysis/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/analysis/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only text analyzer for C++20: turns text into index terms exactly as Xapian's
TermGenerator does. Words, case folding, `Z`-prefixed stems, stop words, CJK n-grams, wdf
and positions all match Xapian 2.1.0, at 1.8 times its speed. Reach for it when the terms
you index have to match a Xapian index or a Xapian-style query parser.

## ✨ Highlights

- **Xapian-exact.** The same terms, wdf and positions as Xapian 2.1.0, checked against 966
  recorded oracle vectors and sweeps over all 1,114,112 code points.
- **1.8 times Xapian's speed** indexing English Wikipedia, with and without stemming
  ([Performance](#-performance)).
- **Unicode-aware.** UTF-8 with an ISO-8859-1 fallback, infixes such as `3.14` and
  `o'clock`, suffixes such as `c++`, acronyms such as `U.S.A.`, and CJK n-grams.
- **Stemming and stop words.** Four stem strategies (`none`, `some`, `all`, `all_z`) with
  any [snowball](https://github.com/Kronuz/snowball) stemmer, and three stop strategies.
- **One word rule for both sides.** `word_scanner` is the indexer's word rule on its own,
  so a query parser sees the same words the indexer saw.
- **Fuzzed.** A libFuzzer target checks that terms are valid UTF-8, bounded in length and
  in increasing positions; CI runs ASan and UBSan.

## 🚀 Quick start

```cpp
#include "analysis.hh"

#include <cstdio>

int main() {
	snowball::stemmer english("english");
	analysis::term_generator g;   // stem_strategy::some: terms plus Z-prefixed stems
	g.stemmer = &english;
	g.index_text("The quick brown foxes", [](std::string_view term, std::uint32_t wdf, std::uint32_t pos) {
		std::printf("%.*s@%u ", int(term.size()), term.data(), pos);
	});
	// the@1 Zthe@0 quick@2 Zquick@0 brown@3 Zbrown@0 foxes@4 Zfox@0
}
```

The sink receives each term occurrence with its wdf increment and its position (0 for
none). [docs/API.md](docs/API.md) covers the strategies, fields and `word_scanner`.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer. CMake fetches
[unicode](https://github.com/Kronuz/unicode) and [snowball](https://github.com/Kronuz/snowball)
at pinned commits. CI builds it on Linux (x86_64, arm64), macOS (arm64, x86_64), and
FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(analysis
  GIT_REPOSITORY https://github.com/Kronuz/analysis.git
  GIT_TAG        991ff3349d7e7d834b331c4b406139e76c76b24e)  # main tracks the tip
FetchContent_MakeAvailable(analysis)

target_link_libraries(my_app PRIVATE analysis::analysis)
```

Then `#include "analysis.hh"`.

## 🎯 When to use it

Use it to index text whose terms must equal a Xapian index's, or as a fast analyzer with
Xapian's rules.

Not for:

- Other analyzers' rules, such as Lucene's StandardTokenizer or ICU word breaks: it
  reproduces Xapian's, quirks included.
- Keeping terms past the callback: the sink's `std::string_view` is valid only during the
  call, so copy what you keep.

## ⚡ Performance

Indexing 10,000 English Wikipedia articles (9.6 MB) into per-document maps of terms to wdf
and positions, best of three, on an Intel Core i9-9980HK with Apple clang `-O3`. The Xapian
column is Xapian 2.1.0's `TermGenerator` indexing the same articles into a
`Xapian::Document`, from a separate harness outside this repository.

| Strategy | analysis | Xapian 2.1.0 | Ratio |
|---|---:|---:|---:|
| `none` | 29.4 MB/s | 16.1 MB/s | 1.8x |
| `some`, English stemmer | 12.0 MB/s | 6.7 MB/s | 1.8x |

`bench/bench.cc` reads the `"text"` field of each line of a JSON-lines corpus:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/analysis_bench enwiki.jsonl 10000
```

## 📚 Documentation

- [docs/API.md](docs/API.md): `term_generator`, its strategies and fields, the sink, and
  `word_scanner`.
- [docs/RULES.md](docs/RULES.md): every tokenization rule and how it is verified.
- [AGENTS.md](AGENTS.md): repository map and invariants.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#analysis):
  include line, target, dependencies and key API.

## 🛠️ Building and testing

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The build includes `analysis_bench`; `-DANALYSIS_FUZZ=ON` adds the libFuzzer target.

## 🤝 Contributing

Read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
before opening a pull request; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo. See [LICENSE](LICENSE).
