# analysis API

Everything lives in `analysis.hh`, in namespace `analysis`.

## term_generator

`term_generator` turns text into terms. It holds:

| Member | Default | Meaning |
|---|---|---|
| `stem` | `stem_strategy::some` | `none`: unstemmed terms only. `some`: unstemmed terms plus positionless `Z`-prefixed stems of words that start with a letter. `all`: stemmed terms only. `all_z`: `Z`-prefixed stemmed terms only. |
| `stop` | `stop_strategy::stemmed` | `none`: index stop words. `all`: drop them. `stemmed`: drop only their `Z` stems. |
| `cjk_ngram` | `false` | Index runs of CJK and similar scripts as unigrams and bigrams. |
| `max_word_length` | 64 | In bytes, after lowercasing; longer words are dropped. |
| `stemmer` | `nullptr` | A non-owning `snowball::stemmer*`. `all` and `all_z` index nothing without one. |
| `stop_words` | `nullptr` | A non-owning `analysis::stopper*`. |

`index_text(text, sink, field)` calls `sink(std::string_view term, std::uint32_t wdf_inc,
std::uint32_t position)` once per term occurrence. Position 0 means the term has no
position. The view is valid only during the call.

`field` sets `wdf_inc` (1 by default), a term `prefix`, and whether terms get `positions`
(true by default):

```cpp
g.index_text(title, sink, {.wdf_inc = 5, .prefix = "S", .positions = true});
```

Positions continue from `termpos()` across calls. `set_termpos(pos)` sets it, and
`increase_termpos(delta)` (100 by default) leaves a gap, for example between fields.

## word_scanner

`word_scanner` is the word rule on its own. `scan(p, end)` collects the lowercased word or
acronym starting at `p` into `text()` and returns where it ends, so a query parser sees the
same words as the indexer. Set its `cjk_ngram` to the generator's.

Scanning is linear in the text, including a long `A.B.C.` chain that is not an acronym. The
scanner remembers the last such chain, so call `reset()` if the bytes at the same addresses
change between scans.

## stopper

`stopper` is a set of stop words, matched against lowercased words:

```cpp
analysis::stopper stop{"the", "a", "of"};
g.stop_words = &stop;
```

The tokenization rules themselves are in [RULES.md](RULES.md).
