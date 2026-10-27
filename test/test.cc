// analysis: Xapian TermGenerator vectors (termgen.jsonl, termgen-xapiand.jsonl) and the
// per-code-point sweeps (chars.txt, infix.txt) recorded by strata-oracle, in argv[1].
#include "analysis.hh"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

static int failures = 0;
#define CHECK(cond) \
	do { \
		if (!(cond)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); if (++failures > 30) std::exit(1); } \
	} while (0)

namespace {

// A minimal JSON reader for the vector files; strings keep raw bytes.
struct json {
	std::variant<std::nullptr_t, bool, double, std::string, std::vector<json>, std::map<std::string, json>> v;
	const json& operator[](const std::string& k) const { return std::get<std::map<std::string, json>>(v).at(k); }
	const std::vector<json>& array() const { return std::get<std::vector<json>>(v); }
	const std::string& str() const { return std::get<std::string>(v); }
	double num() const { return std::get<double>(v); }
	bool boolean() const { return std::get<bool>(v); }
};

json parse(const char*& p) {
	while (*p == ' ') { ++p; }
	if (*p == '{') {
		std::map<std::string, json> m;
		++p;
		while (*p != '}') {
			std::string k = parse(p).str();
			++p;  // ':'
			m[k] = parse(p);
			if (*p == ',') { ++p; }
		}
		++p;
		return {std::move(m)};
	}
	if (*p == '[') {
		std::vector<json> a;
		++p;
		while (*p != ']') {
			a.push_back(parse(p));
			if (*p == ',') { ++p; }
		}
		++p;
		return {std::move(a)};
	}
	if (*p == '"') {
		std::string s;
		for (++p; *p != '"'; ++p) {
			if (*p != '\\') {
				s.push_back(*p);
				continue;
			}
			switch (*++p) {
				case 'n': s.push_back('\n'); break;
				case 't': s.push_back('\t'); break;
				case 'u': s.push_back(char(std::stoi(std::string(p + 1, 4), nullptr, 16))); p += 4; break;
				default: s.push_back(*p);
			}
		}
		++p;
		return {std::move(s)};
	}
	if (*p == 't') { p += 4; return {true}; }
	if (*p == 'f') { p += 5; return {false}; }
	char* e;
	double d = std::strtod(p, &e);
	p = e;
	return {d};
}

using termlist = std::map<std::string, std::pair<std::uint32_t, std::vector<std::uint32_t>>>;

termlist run(analysis::term_generator& g, std::string_view text, const analysis::field& f = {}) {
	termlist out;
	g.index_text(text, [&](std::string_view term, std::uint32_t wdf, std::uint32_t pos) {
		auto& t = out[std::string(term)];
		t.first += wdf;
		if (pos) { t.second.push_back(pos); }
	}, f);
	return out;
}

termlist expected(const json& terms) {
	termlist out;
	for (const auto& t : terms.array()) {
		auto& e = out[t.array()[0].str()];
		e.first = std::uint32_t(t.array()[1].num());
		for (const auto& p : t.array()[2].array()) { e.second.push_back(std::uint32_t(p.num())); }
	}
	return out;
}

std::string show(const termlist& t) {
	std::string s;
	for (const auto& [term, e] : t) {
		s += " " + term + ":" + std::to_string(e.first) + "@";
		for (auto p : e.second) { s += std::to_string(p) + ","; }
	}
	return s;
}

std::string terms_of(std::string_view text, bool ngram = false) {
	analysis::term_generator g;
	g.cjk_ngram = ngram;
	std::string out;
	for (const auto& [term, e] : run(g, text)) { out += (out.empty() ? "" : " ") + term; }
	return out;
}

std::string utf8(char32_t c) {
	std::string s;
	unicode::append_utf8(s, c);
	return s;
}

std::vector<std::string> lines(const std::string& path) {
	std::ifstream in(path);
	CHECK(in.good());
	std::vector<std::string> out;
	for (std::string l; std::getline(in, l);) { out.push_back(l); }
	return out;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "usage: analysis_test VECTORS_DIR\n");
		return 2;
	}
	const std::string dir = argv[1];
	snowball::stemmer english("english");
	const analysis::stopper stop_words{"the", "a", "an", "and", "of", "to", "in", "is", "it", "over"};
	std::size_t vectors = 0;

	// TermGenerator with an English stemmer, per stem strategy, with and without n-grams.
	for (const auto& line : lines(dir + "/termgen.jsonl")) {
		const char* p = line.c_str();
		json r = parse(p);
		analysis::term_generator g;
		g.stemmer = &english;
		const std::string& stem = r["stem"].str();
		g.stem = stem == "none" ? analysis::stem_strategy::none : stem == "some" ? analysis::stem_strategy::some
		       : stem == "all"  ? analysis::stem_strategy::all  : analysis::stem_strategy::all_z;
		g.cjk_ngram = r["cjk_ngram"].boolean();
		auto got = run(g, r["text"].str());
		auto want = expected(r["terms"]);
		if (got != want || g.termpos() != std::uint32_t(r["termpos"].num())) {
			std::fprintf(stderr, "termgen %s ngram=%d: %s\n  got: %s\n want: %s\n", stem.c_str(), g.cjk_ngram, r["text"].str().c_str(), show(got).c_str(), show(want).c_str());
			CHECK(false);
		}
		++vectors;
	}

	// Xapiand's configurations: prefixes, wdf increments, positionless, stop strategies.
	for (const auto& line : lines(dir + "/termgen-xapiand.jsonl")) {
		const char* p = line.c_str();
		json r = parse(p);
		analysis::term_generator g;
		g.stem = analysis::stem_strategy(int(r["stem"].num()));
		g.stop = analysis::stop_strategy(int(r["stop"].num()));
		if (r["stemmer"].boolean()) { g.stemmer = &english; }
		if (r["config"].str() != "defaults") { g.stop_words = &stop_words; }
		analysis::field f{std::uint32_t(r["wdf_inc"].num()), r["prefix"].str(), r["positions"].boolean()};
		auto got = run(g, r["text"].str(), f);
		auto want = expected(r["terms"]);
		if (got != want || g.termpos() != std::uint32_t(r["termpos"].num())) {
			std::fprintf(stderr, "termgen-xapiand %s: %s\n  got: %s\n want: %s\n", r["config"].str().c_str(), r["text"].str().c_str(), show(got).c_str(), show(want).c_str());
			CHECK(false);
		}
		++vectors;
	}

	// Every code point doubled: not a word character, a word character, or an n-gram one.
	std::size_t code_points = 0;
	for (const auto& line : lines(dir + "/chars.txt")) {
		std::istringstream in(line);
		std::uint32_t first, last;
		std::string cls;
		in >> first >> last >> cls;
		for (std::uint32_t c = first; c <= last; ++c, ++code_points) {
			if (c >= 0xD800 && c <= 0xDFFF) { continue; }
			std::string text = utf8(c) + utf8(c);
			analysis::term_generator g;
			g.cjk_ngram = true;
			const std::size_t n = run(g, text).size();
			const std::string got = n == 0 ? "none" : n == 1 ? "word" : n == 2 ? "cjk" : "other";
			if (got != cls) {
				std::fprintf(stderr, "U+%04X: %s, want %s\n", c, got.c_str(), cls.c_str());
				CHECK(false);
			}
		}
	}

	// Every non-word code point between letters and digits, as a suffix, and in acronyms.
	std::map<std::uint32_t, std::string> infix;
	std::set<std::uint32_t> acronym;
	for (const auto& line : lines(dir + "/infix.txt")) {
		std::istringstream in(line);
		std::string kind;
		std::uint32_t c;
		in >> kind >> c;
		if (kind == "acronym") {
			acronym.insert(c);
		} else {
			infix[c] = line.substr(line.find('['));
		}
	}
	const std::string plain = "[a b] [1 2] [1 a] [1 a] [a b] [c] [c] [c] [a b]";
	for (char32_t c = 0; c <= unicode::max_code_point; ++c) {
		if (c >= 0xD800 && c <= 0xDFFF) { continue; }
		const std::string s = utf8(c);
		if (analysis::is_word_char(c)) {
			const std::string low = utf8(unicode::tolower(c));
			const bool is_acronym = terms_of(s + "." + s + ".") == low + low;
			if (is_acronym != acronym.count(c)) {
				std::fprintf(stderr, "U+%04X: acronym %d\n", unsigned(c), is_acronym);
				CHECK(false);
			}
			continue;
		}
		std::string got;
		for (auto [l, r] : {std::pair{"a", "b"}, {"1", "2"}, {"a", "1"}, {"1", "a"}, {"A", "B"}}) { got += "[" + terms_of(l + s + r) + "] "; }
		for (int n : {1, 3, 4}) {
			std::string w = "C";
			for (int i = 0; i < n; ++i) { w += s; }
			got += "[" + terms_of(w + " ") + "] ";
		}
		got += "[" + terms_of("A" + s + "B" + s + " ") + "]";
		auto it = infix.find(c);
		const std::string& want = it == infix.end() ? plain : it->second;
		if (got != want) {
			std::fprintf(stderr, "U+%04X: %s, want %s\n", unsigned(c), got.c_str(), want.c_str());
			CHECK(false);
		}
	}

	// Positions continue across calls; increase_termpos leaves a gap.
	analysis::term_generator g;
	run(g, "one two");
	g.increase_termpos();
	auto second = run(g, "three");
	CHECK(second["three"].second == std::vector<std::uint32_t>{103});

	// The public word scanner collects one lowercased word or acronym and says where it ends.
	{
		analysis::word_scanner ws;
		const std::string_view in = "Obama's U.S.A. c++ x";
		const char* p = in.data();
		const char* const end = p + in.size();
		std::vector<std::string> got;
		while (p < end) {
			const char* q = p;
			if (!analysis::is_word_char(unicode::next_utf8(q, end))) { p = q; continue; }
			p = ws.scan(p, end);
			got.push_back(ws.text());
		}
		CHECK((got == std::vector<std::string>{"obama's", "usa", "c++", "x"}));
	}

	// Acronym chains: scanning is linear and yields the words of an unmemoized walk.
	{
		// The acronym rule without the failed-chain memory.
		auto reference = [](const char* p, const char* end, std::string& out) -> const char* {
			auto up = [](char c) { return c >= 'A' && c <= 'Z'; };
			if (end - p < 3 || !up(p[0]) || p[1] != '.' || !up(p[2])) { return nullptr; }
			out.assign(1, char(p[0] - 'A' + 'a'));
			const char* q = p + 1;
			while (end - q >= 2 && q[0] == '.' && up(q[1])) {
				out.push_back(char(q[1] - 'A' + 'a'));
				q += 2;
			}
			if (q < end && *q == '.') { ++q; }
			if (q < end) {
				const char* r = q;
				if (analysis::is_word_char(unicode::next_utf8(r, end))) { return nullptr; }
			}
			return q;
		};
		auto words = [&](const std::string& in, bool memo) {
			analysis::word_scanner ws, plain;
			std::string out, acr;
			const char* p = in.data();
			const char* const end = p + in.size();
			while (p < end) {
				const char* q = p;
				if (!analysis::is_word_char(unicode::next_utf8(q, end))) { p = q; continue; }
				if (memo) {
					p = ws.scan(p, end);
					out += ws.text();
				} else if (const char* r = reference(p, end, acr)) {
					p = r;
					out += acr;
				} else {
					p = plain.word(p, end);
					out += plain.text();
				}
				out += '|';
			}
			return out;
		};
		std::uint32_t seed = 12345;
		const char alphabet[] = "AAABBb.. x1";
		for (int i = 0; i < 20000; ++i) {
			std::string in(1 + seed % 40, ' ');
			for (char& c : in) {
				seed = seed * 1664525u + 1013904223u;
				c = alphabet[(seed >> 16) % (sizeof alphabet - 1)];
			}
			CHECK(words(in, true) == words(in, false));
			if (failures) { std::fprintf(stderr, "input: %s\n", in.c_str()); break; }
		}

		using clock = std::chrono::steady_clock;
		std::string chain;
		while (chain.size() < 65536) { chain += "A."; }
		chain += "x";
		const auto t0 = clock::now();
		std::size_t terms = 0;
		analysis::term_generator tg;
		tg.stem = analysis::stem_strategy::none;
		tg.index_text(chain, [&](std::string_view, std::uint32_t, std::uint32_t) { ++terms; });
		const double secs = std::chrono::duration<double>(clock::now() - t0).count();
		CHECK(terms == 32768 + 1);   // a, a, ... and x
		CHECK(secs < 0.5);  // the quadratic walk took 1.9 s here
		if (secs >= 0.5) { std::fprintf(stderr, "acronym chain of 64 KiB: %.3f s\n", secs); }
	}

	std::printf("analysis: %zu vectors, %zu code points: %s\n", vectors, code_points, failures ? "FAILED" : "ok");
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
