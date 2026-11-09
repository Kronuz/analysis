// ICU as the oracle for cjk::segmenter. Links ICU; the library never does.
//
//   icu_compare vectors CASES RANDOM > test/vectors/cjk.jsonl
//       ICU's segments of each line of CASES (escapes \n \r \t \\ \uXXXX \UXXXXXXXX) and of
//       RANDOM generated strings, for test/cjk_test.cc to replay.
//   icu_compare corpus FILE...    every boundary and kind against ICU's, differences listed
//   icu_compare sweep             every code point in a set of contexts
//   icu_compare dict CJDICT_TXT   ICU's compiled cjdict against the text and our dictionary
//   icu_compare bench ROUNDS FILE...  MB/s of both on each file
//
// Exits 1 when anything differs.
#include "cjk/segmenter.hh"

#include <unicode/brkiter.h>
#include <unicode/uchar.h>
#include <unicode/rbbi.h>
#include <unicode/ucharstrie.h>
#include <unicode/udata.h>
#include <unicode/unistr.h>
#include <unicode/utext.h>
#include <unicode/uversion.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using analysis::cjk::segment;
using analysis::cjk::word_kind;

struct boundary {
	std::size_t end;
	int kind;
	bool operator==(const boundary&) const = default;
};

int kind_of(int status) { return status < 100 ? 0 : status < 200 ? 1 : status < 300 ? 2 : status < 400 ? 3 : 4; }

class icu_words {
public:
	icu_words() {
		UErrorCode st = U_ZERO_ERROR;
		bi_.reset(icu::BreakIterator::createWordInstance(icu::Locale::getRoot(), st));
		if (U_FAILURE(st)) {
			std::fprintf(stderr, "icu: %s\n", u_errorName(st));
			std::exit(2);
		}
		// Loads the Chinese/Japanese engine before any text that starts with a Common-script
		// kana mark (U+30FC) could be met without it; the segmenter models ICU after that.
		(*this)("日本語");
	}

	std::vector<boundary> operator()(std::string_view text) {
		UErrorCode st = U_ZERO_ERROR;
		UText* ut = utext_openUTF8(nullptr, text.data(), int64_t(text.size()), &st);
		bi_->setText(ut, st);
		std::vector<boundary> out;
		bi_->first();
		for (int32_t p = bi_->next(); p != icu::BreakIterator::DONE; p = bi_->next()) { out.push_back({std::size_t(p), kind_of(bi_->getRuleStatus())}); }
		utext_close(ut);
		return out;
	}

	icu::BreakIterator& iterator() { return *bi_; }

private:
	std::unique_ptr<icu::BreakIterator> bi_;
};

std::vector<boundary> ours(analysis::cjk::segmenter& s, std::string_view text) {
	std::vector<boundary> out;
	s.reset(text);
	std::size_t at = 0;
	for (segment g; s.next(g);) {
		if (g.begin != at || g.end <= g.begin) {
			std::fprintf(stderr, "segments do not tile the text at %zu\n", at);
			std::exit(1);
		}
		at = g.end;
		out.push_back({g.end, int(g.kind)});
	}
	if (at != text.size()) {
		std::fprintf(stderr, "segments end at %zu of %zu\n", at, text.size());
		std::exit(1);
	}
	return out;
}

void append_utf8(std::string& s, char32_t c) {
	if (c < 0x80) {
		s += char(c);
	} else if (c < 0x800) {
		s += char(0xC0 | (c >> 6));
		s += char(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		s += char(0xE0 | (c >> 12));
		s += char(0x80 | ((c >> 6) & 0x3F));
		s += char(0x80 | (c & 0x3F));
	} else {
		s += char(0xF0 | (c >> 18));
		s += char(0x80 | ((c >> 12) & 0x3F));
		s += char(0x80 | ((c >> 6) & 0x3F));
		s += char(0x80 | (c & 0x3F));
	}
}

std::string unescape(const std::string& line) {
	std::string out;
	for (std::size_t i = 0; i < line.size(); ++i) {
		if (line[i] != '\\' || i + 1 == line.size()) {
			out += line[i];
			continue;
		}
		const char e = line[++i];
		if (e == 'n') {
			out += '\n';
		} else if (e == 'r') {
			out += '\r';
		} else if (e == 't') {
			out += '\t';
		} else if (e == 'u' || e == 'U') {
			const std::size_t n = e == 'u' ? 4 : 8;
			append_utf8(out, char32_t(std::stoul(line.substr(i + 1, n), nullptr, 16)));
			i += n;
		} else {
			out += e;
		}
	}
	return out;
}

std::string json_string(std::string_view s) {
	std::string out = "\"";
	for (char c : s) {
		const auto u = static_cast<unsigned char>(c);
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (u < 0x20) {
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\u%04x", u);
			out += buf;
		} else {
			out += c;
		}
	}
	return out + "\"";
}

std::string read_file(const char* path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		std::fprintf(stderr, "cannot read %s\n", path);
		std::exit(2);
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// Characters of every kind the rules and the dictionary path distinguish.
const char32_t pool[] = {
	U'a', U'Z', U'é', U'ß', U'1', U'9', 0x0663, U'.', U',', U':', U';', U'\'', U'"', U'_', 0x203F, U'-', U'!', U'@', U'$',
	U' ', U' ', U'\t', U'\r', U'\n', 0x0085, 0x2028, 0x000B, 0x3000, 0x00AD, 0x0301, 0x200D, 0x200B, 0xFEFF, 0x2060, 0xFE0F,
	0x1F1E6, 0x1F1E7, 0x1F44D, 0x2764, 0x1F3FB, 0x05D0, 0x05D1, 0x0627, 0x0E01, 0x0E31, 0x1100, 0x1161, 0xAC00, 0xB098, 0xD7A3,
	0x30AB, 0x30BF, 0x30CA, 0x30FC, 0x30FB, 0xFF76, 0xFF9E, 0xFF9F, 0xFF70, 0x304B, 0x306E, 0x3099, 0x309B, 0x3031, 0x30A0,
	0x6F22, 0x5B57, 0x65E5, 0x672C, 0x8A9E, 0x4E2D, 0x56FD, 0x4EBA, 0x3005, 0x3007, 0x20000, 0x2A6D6, 0xF900, 0x2F00, 0x3300,
	0x309F, 0x16FF0, 0xFF21, 0xFF11, 0xFFE5, 0x3001, 0x3002, 0x300C, 0x300D, 0xE000,
};

// Words that put the dictionary to work.
const char* const words[] = {"日本語", "東京", "中华人民共和国", "カタカナ", "コンピューター", "ひらがな", "学生", "大学生", "研究", "生命", "起源", "ｶﾞｲﾄﾞ", "私"};

std::string random_text(std::mt19937& rng) {
	std::string s;
	const int n = 1 + int(rng() % 12);
	for (int i = 0; i < n; ++i) {
		if (rng() % 4 == 0) {
			s += words[rng() % std::size(words)];
		} else {
			append_utf8(s, pool[rng() % std::size(pool)]);
		}
	}
	return s;
}

int vectors(const char* cases, int random) {
	icu_words icu;
	std::vector<std::string> texts;
	std::ifstream in(cases);
	if (!in) {
		std::fprintf(stderr, "cannot read %s\n", cases);
		return 2;
	}
	for (std::string line; std::getline(in, line);) {
		if (line.empty() || line[0] == '#') { continue; }
		texts.push_back(unescape(line));
	}
	std::mt19937 rng(29);
	for (int i = 0; i < random; ++i) { texts.push_back(random_text(rng)); }
	for (const auto& t : texts) {
		std::string out = "{\"text\":" + json_string(t) + ",\"segments\":[";
		bool first = true;
		for (auto& b : icu(t)) {
			out += (first ? "[" : ",[") + std::to_string(b.end) + "," + std::to_string(b.kind) + "]";
			first = false;
		}
		std::printf("%s]}\n", out.c_str());
	}
	return 0;
}

// Segments of text[from, to) as "a|b|c", kinds after a colon when not none.
std::string show(std::string_view text, const std::vector<boundary>& bs, std::size_t from, std::size_t to) {
	std::string out;
	std::size_t at = from;
	for (auto& b : bs) {
		if (b.end <= from) { continue; }
		if (at >= to) { break; }
		const std::size_t e = std::min(b.end, to);
		if (!out.empty()) { out += '|'; }
		std::string piece(text.substr(at, e - at));
		for (char& c : piece) {
			if (c == '\n' || c == '\r' || c == '\t') { c = ' '; }
		}
		out += piece;
		if (b.kind && e == b.end) { out += ":" + std::to_string(b.kind); }
		at = e;
	}
	return out;
}

// Moves p back or forward by n code points, staying in text.
std::size_t back(std::string_view t, std::size_t p, int n) {
	while (n-- > 0 && p > 0) {
		--p;
		while (p > 0 && (static_cast<unsigned char>(t[p]) & 0xC0) == 0x80) { --p; }
	}
	return p;
}
std::size_t forward(std::string_view t, std::size_t p, int n) {
	while (n-- > 0 && p < t.size()) {
		++p;
		while (p < t.size() && (static_cast<unsigned char>(t[p]) & 0xC0) == 0x80) { ++p; }
	}
	return p;
}

// Lists each difference between two boundary lists by its context; returns how many.
std::size_t diff(std::string_view text, const std::vector<boundary>& a, const std::vector<boundary>& b, std::map<std::string, std::size_t>& out) {
	std::size_t i = 0, j = 0, n = 0;
	while (i < a.size() || j < b.size()) {
		if (i < a.size() && j < b.size() && a[i] == b[j]) {
			++i;
			++j;
			continue;
		}
		const std::size_t at = std::min(i < a.size() ? a[i].end : SIZE_MAX, j < b.size() ? b[j].end : SIZE_MAX);
		const std::size_t from = back(text, at, 4), to = forward(text, at, 4);
		out["icu  " + show(text, a, from, to) + "\nours " + show(text, b, from, to)]++;
		++n;
		if (i < a.size() && a[i].end == at) { ++i; }
		if (j < b.size() && b[j].end == at) { ++j; }
	}
	return n;
}

void report(const std::map<std::string, std::size_t>& diffs, std::size_t limit) {
	std::vector<std::pair<std::size_t, std::string>> v;
	for (auto& [k, n] : diffs) { v.emplace_back(n, k); }
	std::sort(v.rbegin(), v.rend());
	for (std::size_t i = 0; i < v.size() && i < limit; ++i) { std::printf("%zu×\n%s\n", v[i].first, v[i].second.c_str()); }
}

std::size_t code_points(std::string_view s) {
	return std::size_t(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

int corpus(int argc, char** argv) {
	icu_words icu;
	analysis::cjk::segmenter seg;
	std::size_t total = 0;
	for (int f = 0; f < argc; ++f) {
		const std::string text = read_file(argv[f]);
		const auto a = icu(text), b = ours(seg, text);
		std::map<std::string, std::size_t> diffs;
		const std::size_t n = diff(text, a, b, diffs);
		std::size_t words = 0;
		for (auto& x : a) { words += x.kind != 0; }
		std::printf("%s: %zu bytes, %zu characters, %zu ICU boundaries (%zu words), %zu ours, %zu differences\n", argv[f], text.size(), code_points(text),
		            a.size(), words, b.size(), n);
		report(diffs, SIZE_MAX);
		total += n;
	}
	std::printf("ICU %s, %zu differences\n", U_ICU_VERSION, total);
	return total ? 1 : 0;
}

int sweep() {
	icu_words icu;
	analysis::cjk::segmenter seg;
	const std::pair<const char*, const char*> contexts[] = {
		{"", ""}, {"a", "b"}, {"1", "2"}, {"日本", "語"}, {"カ", "ナ"}, {"가", "나"}, {"", "ﾞ"}, {"ｶ", "ﾟ"},
		{"", "\u0301"}, {"\u200D", ""}, {"🇦", "🇧"}, {"学", "生"}, {"コンピ", "ュータ"}, {" ", " "},
	};
	// Thai, Lao, Khmer, Myanmar and the other Line_Break=Complex_Context scripts are
	// divided by ICU's per-script engines, which the segmenter does not have: their
	// differences are counted apart and do not fail the sweep.
	std::map<std::string, std::size_t> diffs;
	std::map<char32_t, char32_t> ranges;  // code points with differences, as [first, last]
	std::size_t texts = 0, n = 0, complex = 0;
	for (char32_t c = 0; c <= 0x10FFFF; ++c) {
		if (c >= 0xD800 && c <= 0xDFFF) { continue; }
		std::string ch;
		append_utf8(ch, c);
		for (auto& [pre, post] : contexts) {
			for (int twice = 0; twice < 2; ++twice) {
				const std::string t = pre + ch + (twice ? ch : "") + post;
				std::map<std::string, std::size_t> d;
				const std::size_t k = diff(t, icu(t), ours(seg, t), d);
				if (k) {
					char key[64];
					std::snprintf(key, sizeof key, "U+%04X in [%s _%s %s]", unsigned(c), pre, twice ? " _" : "", post);
					if (diffs.size() < 200) { diffs[key + std::string("\n") + d.begin()->first] = k; }
					if (!ranges.empty() && std::prev(ranges.end())->second + 1 >= c) {
						std::prev(ranges.end())->second = c;
					} else {
						ranges[c] = c;
					}
					const bool sa = u_getIntPropertyValue(UChar32(c), UCHAR_LINE_BREAK) == U_LB_COMPLEX_CONTEXT;
					(sa ? complex : n) += k;
				}
				++texts;
			}
		}
	}
	report(diffs, 200);
	std::printf("code points with differences:");
	for (auto& [a, b] : ranges) { std::printf(" U+%04X..U+%04X", unsigned(a), unsigned(b)); }
	std::printf("\nICU %s, %zu texts, %zu differences, and %zu in Complex_Context scripts\n", U_ICU_VERSION, texts, n, complex);
	return n ? 1 : 0;
}

int dict(const char* cjdict) {
	UErrorCode st = U_ZERO_ERROR;
	UDataMemory* m = udata_open(U_ICUDATA_NAME U_TREE_SEPARATOR_STRING "brkitr", "dict", "cjdict", &st);
	if (U_FAILURE(st)) {
		std::fprintf(stderr, "udata_open: %s\n", u_errorName(st));
		return 2;
	}
	const auto* ix = static_cast<const int32_t*>(udata_getMemory(m));
	if (ix[4] != 9) {  // a UChars trie with values
		std::fprintf(stderr, "unexpected trie type %d\n", ix[4]);
		return 2;
	}
	const auto* trie = reinterpret_cast<const char16_t*>(reinterpret_cast<const char*>(ix) + ix[0]);
	std::map<std::u32string, int> icu_words;
	icu::UCharsTrie::Iterator it(trie, 0, st);
	while (it.next(st)) {
		const icu::UnicodeString& w = it.getString();
		std::u32string u;
		for (int32_t i = 0; i < w.length(); i = w.moveIndex32(i, 1)) { u.push_back(char32_t(w.char32At(i))); }
		icu_words[u] = it.getValue();
	}
	std::map<std::u32string, int> text_words;
	{
		std::ifstream in(cjdict, std::ios::binary);
		std::string line;
		for (std::size_t n = 1; std::getline(in, line); ++n) {
			if (n == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) { line.erase(0, 3); }
			if (auto h = line.find('#'); h != std::string::npos) { line.erase(h); }
			while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) { line.pop_back(); }
			if (line.empty()) { continue; }
			const auto tab = line.find('\t');
			const icu::UnicodeString w = icu::UnicodeString::fromUTF8(line.substr(0, tab));
			std::u32string u;
			for (int32_t i = 0; i < w.length(); i = w.moveIndex32(i, 1)) { u.push_back(char32_t(w.char32At(i))); }
			text_words[u] = std::stoi(line.substr(tab + 1), nullptr, 0);
		}
	}
	std::size_t bad = 0;
	for (auto& [w, v] : icu_words) {
		const int c = analysis::cjk::detail::dictionary_cost(w);
		if (c != v) { ++bad; }
	}
	std::printf("ICU %s cjdict: %zu words; data/cjdict.txt: %zu words, %s; ours disagree on %zu\n", U_ICU_VERSION, icu_words.size(), text_words.size(),
	            icu_words == text_words ? "identical" : "DIFFERENT", bad);
	udata_close(m);
	return icu_words == text_words && bad == 0 ? 0 : 1;
}

int bench(int rounds, int argc, char** argv) {
	icu_words icu;
	analysis::cjk::segmenter seg;
	using clock = std::chrono::steady_clock;
	std::printf("dictionary: %zu bytes built in\n", analysis::cjk::detail::dictionary_bytes());
	for (int f = 0; f < argc; ++f) {
		const std::string text = read_file(argv[f]);
		const icu::UnicodeString utf16 = icu::UnicodeString::fromUTF8(text);
		std::vector<double> t_ours, t_icu8, t_icu16;
		std::size_t n_ours = 0, n_icu8 = 0, n_icu16 = 0;
		for (int r = 0; r < rounds; ++r) {
			auto t0 = clock::now();
			seg.reset(text);
			n_ours = 0;
			for (segment g; seg.next(g);) { n_ours += g.kind != word_kind::none; }
			auto t1 = clock::now();
			UErrorCode st = U_ZERO_ERROR;
			UText* ut = utext_openUTF8(nullptr, text.data(), int64_t(text.size()), &st);
			icu.iterator().setText(ut, st);
			n_icu8 = 0;
			for (int32_t p = icu.iterator().first(); (p = icu.iterator().next()) != icu::BreakIterator::DONE;) { n_icu8 += icu.iterator().getRuleStatus() >= 100; }
			utext_close(ut);
			auto t2 = clock::now();
			icu.iterator().setText(utf16);
			n_icu16 = 0;
			for (int32_t p = icu.iterator().first(); (p = icu.iterator().next()) != icu::BreakIterator::DONE;) { n_icu16 += icu.iterator().getRuleStatus() >= 100; }
			auto t3 = clock::now();
			t_ours.push_back(std::chrono::duration<double>(t1 - t0).count());
			t_icu8.push_back(std::chrono::duration<double>(t2 - t1).count());
			t_icu16.push_back(std::chrono::duration<double>(t3 - t2).count());
		}
		auto mbs = [&](std::vector<double> v) {
			std::sort(v.begin(), v.end());
			return std::pair{text.size() / v.front() / 1e6, text.size() / v[v.size() / 2] / 1e6};
		};
		const auto o = mbs(t_ours), a = mbs(t_icu8), b = mbs(t_icu16);
		std::printf("%s: %zu bytes, %d rounds, words %zu/%zu/%zu; MB/s best (median): ours %.2f (%.2f), ICU UTF-8 %.2f (%.2f), ICU UTF-16 %.2f (%.2f); ours/ICU UTF-8 %.2fx\n",
		            argv[f], text.size(), rounds, n_ours, n_icu8, n_icu16, o.first, o.second, a.first, a.second, b.first, b.second, o.first / a.first);
	}
	return 0;
}

}  // namespace

int main(int argc, char** argv) {
	const std::string mode = argc > 1 ? argv[1] : "";
	if (mode == "vectors" && argc == 4) { return vectors(argv[2], std::atoi(argv[3])); }
	if (mode == "corpus" && argc > 2) { return corpus(argc - 2, argv + 2); }
	if (mode == "sweep" && argc == 2) { return sweep(); }
	if (mode == "dict" && argc == 3) { return dict(argv[2]); }
	if (mode == "bench" && argc > 3) { return bench(std::atoi(argv[2]), argc - 3, argv + 3); }
	std::fprintf(stderr, "usage: icu_compare vectors CASES RANDOM | corpus FILE... | sweep | dict CJDICT_TXT | bench ROUNDS FILE...\n");
	return 2;
}
