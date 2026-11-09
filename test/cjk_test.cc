// cjk::segmenter: replays ICU's segments recorded by tools/icu_compare.cc (cjk.jsonl in
// argv[1]), checks every word of data/cjdict.txt (argv[2]) in the built dictionary, and
// pins the API's behavior on its own cases.
#include "cjk/segmenter.hh"
#include "json.hh"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); if (++failures > 30) std::exit(1); } \
	} while (0)

namespace {

using analysis::cjk::segment;
using analysis::cjk::segmenter;
using analysis::cjk::word_kind;

// The segments as "text:kind" pieces, kind as a number, checking they tile the text.
std::string segments_of(segmenter& s, std::string_view text) {
	std::string out;
	s.reset(text);
	std::size_t at = 0;
	for (segment g; s.next(g);) {
		CHECK(g.begin == at);
		CHECK(g.end > g.begin);
		CHECK(g.end <= text.size());
		at = g.end;
		out += "[" + std::to_string(g.end) + "," + std::to_string(int(g.kind)) + "]";
	}
	CHECK(at == text.size());
	return out;
}

std::string words_of(std::string_view text) {
	segmenter s(text);
	std::string out;
	for (segment g; s.next(g);) {
		if (g.kind == word_kind::none) { continue; }
		out += (out.empty() ? "" : " ") + std::string(text.substr(g.begin, g.end - g.begin)) + ":" + "-nlki"[int(g.kind)];
	}
	return out;
}

std::u32string decode(const std::string& s) {
	std::u32string out;
	for (std::size_t i = 0; i < s.size();) {
		const auto b = static_cast<unsigned char>(s[i]);
		const std::size_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
		char32_t c = n == 1 ? b : b & (0x7F >> n);
		for (std::size_t k = 1; k < n; ++k) { c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F); }
		out.push_back(c);
		i += n;
	}
	return out;
}

void vectors(const std::string& path) {
	std::ifstream in(path);
	CHECK(in.good());
	segmenter s;
	std::size_t n = 0, boundaries = 0;
	for (std::string line; std::getline(in, line);) {
		const char* p = line.c_str();
		const auto r = test_json::parse(p);
		const std::string& text = r["text"].str();
		std::string want;
		for (const auto& b : r["segments"].array()) {
			want += "[" + std::to_string(int(b.array()[0].num())) + "," + std::to_string(int(b.array()[1].num())) + "]";
			++boundaries;
		}
		const std::string got = segments_of(s, text);
		if (got != want) { std::fprintf(stderr, "text: %s\n  ICU:  %s\n  ours: %s\n", text.c_str(), want.c_str(), got.c_str()); }
		CHECK(got == want);
		++n;
	}
	CHECK(n > 1000);
	std::printf("cjk.jsonl: %zu texts, %zu boundaries\n", n, boundaries);
}

void dictionary(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	CHECK(in.good());
	std::set<std::u32string> all;
	std::vector<std::pair<std::u32string, int>> words;
	for (std::string line; std::getline(in, line);) {
		if (line.rfind("\xEF\xBB\xBF", 0) == 0) { line.erase(0, 3); }
		if (auto h = line.find('#'); h != std::string::npos) { line.erase(h); }
		while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) { line.pop_back(); }
		if (line.empty()) { continue; }
		const auto tab = line.find('\t');
		words.emplace_back(decode(line.substr(0, tab)), std::stoi(line.substr(tab + 1)));
		all.insert(words.back().first);
	}
	CHECK(words.size() == 315964);
	std::size_t absent = 0;
	for (const auto& [w, cost] : words) {
		CHECK(analysis::cjk::detail::dictionary_cost(w) == cost);
		// A prefix that is not a word, and the word extended by a character no word has.
		const std::u32string prefix = w.substr(0, w.size() - 1);
		if (!prefix.empty() && !all.count(prefix)) {
			CHECK(analysis::cjk::detail::dictionary_cost(prefix) == -1);
			++absent;
		}
		CHECK(analysis::cjk::detail::dictionary_cost(w + U'a') == -1);
	}
	CHECK(absent > 1000);
	CHECK(analysis::cjk::detail::dictionary_cost(U"") == -1);
	CHECK(analysis::cjk::detail::dictionary_cost(std::u32string(1, char32_t(0x110000))) == -1);
	std::printf("cjdict.txt: %zu words, %zu non-word prefixes absent\n", words.size(), absent);
}

void cases() {
	segmenter s;
	// Empty text, reuse across texts, and positions in bytes.
	CHECK(segments_of(s, "").empty());
	CHECK(segments_of(s, "a b") == "[1,2][2,0][3,2]");
	CHECK(segments_of(s, "日本") == "[6,4]");

	// The UAX #29 rules: infixes, numbers, apostrophes, ExtendNumLet, Katakana.
	CHECK(words_of("can't 3.14 a.b 1,000 a1 foo_bar") == "can't:l 3.14:n a.b:l 1,000:n a1:n foo_bar:l");
	CHECK(words_of("a:b") == "a:b:l");
	CHECK(words_of("カタカナ_テスト") == "カタカナ_テスト:i");
	// Hangul syllables stay together, with no dictionary.
	CHECK(words_of("대한민국의 수도는 서울") == "대한민국의:l 수도는:l 서울:l");
	// Chinese and Japanese through the dictionary.
	CHECK(words_of("中华人民共和国成立") == "中华:i 人民:i 共和国:i 成立:i");
	CHECK(words_of("私は東京大学の学生です") == "私:i は:i 東京:i 大学:i の:i 学生:i です:i");
	// Halfwidth katakana normalizes for the lookup; the boundaries stay in the original.
	CHECK(words_of("ｺﾝﾋﾟｭｰﾀｰ") == "ｺﾝﾋﾟｭｰﾀｰ:i");

	// Invalid UTF-8 reads one byte at a time, and the segments still tile the text.
	const std::string bad = "\xff\xfe\xe6\x97\xa5\x80\xe6\x9c\xac\xe8\xaa";
	segments_of(s, bad);
	const std::string nul("a\0b", 3);
	CHECK(segments_of(s, nul) == "[1,2][2,0][3,2]");
}

}  // namespace

int main(int argc, char** argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: analysis_cjk_test VECTORS_DIR CJDICT_TXT\n");
		return 2;
	}
	cases();
	vectors(std::string(argv[1]) + "/cjk.jsonl");
	dictionary(argv[2]);
	if (failures) { std::fprintf(stderr, "%d failures\n", failures); }
	return failures ? 1 : 0;
}
