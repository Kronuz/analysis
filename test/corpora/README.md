# Corpora

Public-domain texts for comparing `cjk::segmenter` with ICU (`tools/icu_compare.cc
corpus`). `tools/fetch_corpora.py` downloads and cleans them; each keeps the first lines
of its source that reach 110,000 characters.

| File | Text | Source | Status |
|---|---|---|---|
| `zh.txt` | 魯迅《吶喊》(1923), then the opening of 曹雪芹《紅樓夢》 | Project Gutenberg eBooks [#27166](https://www.gutenberg.org/ebooks/27166) and [#24264](https://www.gutenberg.org/ebooks/24264), without the Gutenberg header and license | Public domain (Lu Xun died 1936; Cao Xueqin 1763) |
| `ja.txt` | 夏目漱石『こころ』(1914) | Aozora Bunko, [card 773](https://www.aozora.gr.jp/cards/000148/card773.html), without ruby readings and transcriber notes | Public domain (Natsume Sōseki died 1916) |
| `ko.txt` | 이광수 『무정』(1917), its opening chapters | Korean Wikisource, [무정](https://ko.wikisource.org/wiki/%EB%AC%B4%EC%A0%95), without wiki markup | Public domain (PD-old-50 on Wikisource; Yi Kwang-su died 1950) |
