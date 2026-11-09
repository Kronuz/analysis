#!/usr/bin/env python3
"""Fetches and cleans the public-domain corpora in test/corpora/ (sources in its README).

  fetch_corpora.py OUT_DIR

Each corpus keeps the first lines of its work that reach MIN_CHARS characters.
"""

from __future__ import annotations

import io
import re
import sys
import urllib.parse
import urllib.request
import zipfile

MIN_CHARS = 110_000

ZH = [
    "https://www.gutenberg.org/cache/epub/27166/pg27166.txt",  # 吶喊, Lu Xun
    "https://www.gutenberg.org/cache/epub/24264/pg24264.txt",  # 紅樓夢, Cao Xueqin
]
JA = "https://www.aozora.gr.jp/cards/000148/files/773_ruby_5968.zip"
KO = "https://ko.wikisource.org/w/index.php?title={}&action=raw"
KO_PAGES = ["무정/1장~20장", "무정/21장~40장", "무정/41장~60장", "무정/61장~80장"]


def fetch(url: str) -> bytes:
    request = urllib.request.Request(
        url, headers={"User-Agent": "Kronuz-analysis-corpora/1"}
    )
    with urllib.request.urlopen(request, timeout=60) as r:
        return r.read()


def chinese() -> str:
    parts = []
    for url in ZH:
        text = fetch(url).decode("utf-8-sig")
        start = text.index("\n", text.index("*** START OF")) + 1
        parts.append(text[start : text.index("*** END OF")])
    return "\n".join(parts)


def japanese() -> str:
    with zipfile.ZipFile(io.BytesIO(fetch(JA))) as z:
        raw = z.read(next(n for n in z.namelist() if n.endswith(".txt")))
    text = raw.decode("cp932").replace("\r\n", "\n")
    body = text.split("-------------------------------------------------------\n")[2]
    body = body[: body.index("底本：")]
    body = re.sub(r"《[^》]*》", "", body)  # ruby readings
    body = re.sub(r"［＃[^］]*］", "", body)  # transcriber notes
    return body.replace("｜", "")


def korean() -> str:
    parts = []
    for page in KO_PAGES:
        text = fetch(KO.format(urllib.parse.quote(page))).decode("utf-8")
        while re.search(r"\{\{[^{}]*\}\}", text):
            text = re.sub(r"\{\{[^{}]*\}\}", "", text)  # templates, innermost first
        # Links keep their text.
        text = re.sub(r"\[\[(?:[^|\]]*\|)?([^\]]*)\]\]", r"\1", text)
        text = re.sub(r"<[^>]*>", "", text)
        text = re.sub(r"^=+.*=+\s*$", "", text, flags=re.MULTILINE)
        parts.append(text)
    return "\n".join(parts)


def trim(text: str) -> str:
    lines = [line.rstrip() for line in text.strip().split("\n")]
    out: list[str] = []
    count = 0
    for line in lines:
        if count >= MIN_CHARS:
            break
        out.append(line)
        count += len(line) + 1
    return re.sub(r"\n{3,}", "\n\n", "\n".join(out)) + "\n"


def main() -> None:
    out_dir = sys.argv[1]
    for name, make in (("zh", chinese), ("ja", japanese), ("ko", korean)):
        text = trim(make())
        with open(f"{out_dir}/{name}.txt", "w", encoding="utf-8") as f:
            f.write(text)
        print(f"{name}: {len(text)} characters, {len(text.encode())} bytes")


if __name__ == "__main__":
    main()
