// Generates cjk/tables.hh from ICU: the word break state machine ICU compiles from its
// word.txt rules, each code point's category in it, and the NFKC data the dictionary
// path needs. Links ICU; the library never does.
//
//   gen_tables > cjk/tables.hh
#include <unicode/brkiter.h>
#include <unicode/normalizer2.h>
#include <unicode/rbbi.h>
#include <unicode/ucptrie.h>
#include <unicode/uniset.h>
#include <unicode/uscript.h>
#include <unicode/uversion.h>

#include "staged.hh"

#include <algorithm>
#include <array>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const char* what) {
	std::fprintf(stderr, "gen_tables: %s\n", what);
	std::exit(1);
}

void check(UErrorCode status, const char* what) {
	if (U_FAILURE(status)) {
		std::fprintf(stderr, "gen_tables: %s: %s\n", what, u_errorName(status));
		std::exit(1);
	}
}

std::u32string code_points(const icu::UnicodeString& s) {
	std::u32string out;
	for (int32_t i = 0; i < s.length(); i = s.moveIndex32(i, 1)) { out.push_back(char32_t(s.char32At(i))); }
	return out;
}

// RBBIDataHeader and RBBIStateTable (ICU's rbbidata.h), read as 32-bit words.
struct rules_t {
	std::uint32_t categories;
	std::uint32_t states, row_len, dict_start, lookahead, flags;
	const std::uint8_t* rows;
	const std::int32_t* status;
	const UCPTrie* trie;
};

rules_t load_rules(icu::RuleBasedBreakIterator& bi) {
	std::uint32_t length = 0;
	const std::uint8_t* data = bi.getBinaryRules(length);
	if (!data || length < 80) { fail("no binary rules"); }
	auto word = [&](std::size_t i) {
		std::uint32_t v;
		std::memcpy(&v, data + 4 * i, 4);
		return v;
	};
	if (word(0) != 0xB1A0) { fail("bad RBBI magic"); }
	if (data[4] != 6) { fail("unknown RBBI format version"); }
	rules_t r{};
	r.categories = word(3);
	const std::uint32_t ftable = word(4), trie = word(8), trie_len = word(9), status = word(12);
	auto table = [&](std::size_t i) {
		std::uint32_t v;
		std::memcpy(&v, data + ftable + 4 * i, 4);
		return v;
	};
	r.states = table(0);
	r.row_len = table(1);
	r.dict_start = table(2);
	r.lookahead = table(3);
	r.flags = table(4);
	r.rows = data + ftable + 20;
	r.status = reinterpret_cast<const std::int32_t*>(data + status);
	UErrorCode st = U_ZERO_ERROR;
	r.trie = ucptrie_openFromBinary(UCPTRIE_TYPE_ANY, UCPTRIE_VALUE_BITS_ANY, data + trie, int32_t(trie_len), nullptr, &st);
	check(st, "ucptrie_openFromBinary");
	return r;
}

}  // namespace

int main() {
	UErrorCode st = U_ZERO_ERROR;
	std::unique_ptr<icu::BreakIterator> bi(icu::BreakIterator::createWordInstance(icu::Locale::getRoot(), st));
	check(st, "createWordInstance");
	const rules_t r = load_rules(*static_cast<icu::RuleBasedBreakIterator*>(bi.get()));

	// The runtime handles 8-bit rows with no look-ahead rules and no {bof}: fail loudly if
	// ICU's word rules ever need more.
	constexpr std::uint32_t lookahead_hard_break = 1, bof_required = 2, rows_8bit = 4;
	if (r.flags != rows_8bit || (r.flags & (lookahead_hard_break | bof_required))) { fail("unexpected state table flags"); }
	if (r.lookahead != 0) { fail("look-ahead rules are not supported"); }
	if (r.row_len != 3 + r.categories) { fail("unexpected row length"); }
	if (r.categories > 32 || r.states > 256) { fail("too many categories or states"); }

	icu::UnicodeSet cj(icu::UnicodeString(u"[[:Han:][:Hiragana:][:Katakana:]\\u30fc\\uff70\\uff9e\\uff9f]"), st);
	check(st, "cj set");

	// NFKC for the dictionary path: full compatibility decompositions of the engine's
	// characters, the combining classes of the non-starters they contain, the characters
	// with no normalization boundary before them, and the canonical pairs that compose
	// with those non-starters. Fails if a starter could compose with another starter.
	const icu::Normalizer2* nfkd = icu::Normalizer2::getNFKDInstance(st);
	const icu::Normalizer2* nfkc = icu::Normalizer2::getNFKCInstance(st);
	const icu::Normalizer2* nfc = icu::Normalizer2::getNFCInstance(st);
	check(st, "normalizers");
	std::vector<std::pair<char32_t, std::u32string>> decomp;
	std::map<char32_t, unsigned> nonstarters;
	std::vector<char32_t> no_boundary;
	std::u32string pool;
	std::vector<bool> produced(0x110000);
	for (UChar32 c = 0; c < 0x110000; ++c) {
		if (!cj.contains(c)) { continue; }
		if (!nfkc->hasBoundaryBefore(c)) { no_boundary.push_back(char32_t(c)); }
		icu::UnicodeString d;
		nfkd->normalize(icu::UnicodeString(c), d, st);
		check(st, "nfkd");
		const std::u32string cps = code_points(d);
		for (char32_t x : cps) {
			produced[x] = true;
			if (const auto ccc = u_getCombiningClass(UChar32(x))) { nonstarters[x] = ccc; }
		}
		if (cps != std::u32string(1, char32_t(c))) { decomp.emplace_back(char32_t(c), cps); }
	}
	for (char32_t c : no_boundary) {
		if (!nonstarters.count(c) && !(c == 0xFF9E || c == 0xFF9F)) { fail("a character without a boundary before it is neither a non-starter nor a voicing mark"); }
	}
	std::vector<std::array<char32_t, 3>> pairs;
	for (UChar32 c = 0; c < 0x110000; ++c) {
		icu::UnicodeString raw;
		if (!nfc->getRawDecomposition(c, raw)) { continue; }
		const std::u32string cps = code_points(raw);
		if (cps.size() != 2 || nfc->composePair(UChar32(cps[0]), UChar32(cps[1])) != c) { continue; }
		if (!produced[cps[0]] || !produced[cps[1]]) { continue; }
		if (!nonstarters.count(cps[1])) { fail("a starter composes with another starter"); }
		pairs.push_back({cps[0], cps[1], char32_t(c)});
	}
	std::sort(pairs.begin(), pairs.end());
	std::vector<bool> changes(0x110000);
	for (auto& [c, d] : decomp) { changes[c] = true; }
	for (char32_t c : no_boundary) { changes[c] = true; }

	// Character classes: the category in the low 5 bits, then three flags the dictionary
	// path reads: the Chinese/Japanese engine's set, Script=Common, and engine characters
	// NFKC can change (a decomposition, or no normalization boundary before them).
	constexpr unsigned cj_bit = 0x20, common_bit = 0x40, nfkc_bit = 0x80;
	std::vector<std::uint8_t> cls(0x110000);
	for (UChar32 c = 0; c < 0x110000; ++c) {
		const std::uint32_t cat = ucptrie_get(r.trie, c);
		if (cat >= r.categories) { fail("category out of range"); }
		UErrorCode e = U_ZERO_ERROR;
		const bool common = uscript_getScript(c, &e) == USCRIPT_COMMON;
		cls[c] = std::uint8_t(cat | (cj.contains(c) ? cj_bit : 0) | (common ? common_bit : 0) | (changes[c] ? nfkc_bit : 0));
	}

	const staged::table<std::uint8_t> t = staged::build(cls);


	std::printf("// Generated by tools/gen_tables.cc from ICU %s (Unicode %s). Do not edit.\n", U_ICU_VERSION, U_UNICODE_VERSION);
	std::printf("// ICU's word break rules compiled to a state machine, and Unicode character data:\n");
	std::printf("// Unicode-3.0, see LICENSE-ICU.\n");
	std::printf("#pragma once\n\n#include <cstdint>\n\nnamespace analysis::cjk::tables {\n\n");
	std::printf("inline constexpr unsigned categories = %u;\n", r.categories);
	std::printf("inline constexpr unsigned dict_start = %u;  // categories at or above it are dictionary characters\n", r.dict_start);
	std::printf("inline constexpr unsigned category_mask = 0x1f, cj_bit = 0x%x, common_bit = 0x%x, nfkc_bit = 0x%x;\n", cj_bit, common_bit, nfkc_bit);
	std::printf("// Each code point's class: its category, cj_bit, common_bit, nfkc_bit.\n");
	std::fputs(staged::print("class", t, "std::uint8_t").c_str(), stdout);
	std::printf("\n");

	// Rows: next state per category, then whether the state accepts and the status (the
	// largest value of its rule status group) a boundary there carries.
	std::printf("inline constexpr unsigned states = %u;\n", r.states);
	std::printf("inline constexpr std::uint8_t next[%u][%u] = {\n", r.states, r.categories);
	std::vector<int> accepting(r.states), status(r.states);
	for (std::uint32_t s = 0; s < r.states; ++s) {
		const std::uint8_t* row = r.rows + std::size_t(s) * r.row_len;
		if (row[0] > 1 || row[1] != 0) { fail("look-ahead state"); }
		accepting[s] = row[0];
		status[s] = r.status[row[2] + r.status[row[2]]];
		std::printf("\t{");
		for (std::uint32_t c = 0; c < r.categories; ++c) { std::printf("%u,", row[3 + c]); }
		std::printf("},\n");
	}
	std::printf("};\n");
	std::printf("inline constexpr bool accepting[%u] = {", r.states);
	for (auto a : accepting) { std::printf("%d,", a); }
	std::printf("};\n");
	std::printf("inline constexpr std::int16_t status[%u] = {", r.states);
	for (auto s : status) { std::printf("%d,", s); }
	std::printf("};\n\n");

	std::printf("// Full compatibility decompositions of the engine's characters that change.\n");
	std::printf("struct decomposition { char32_t c; std::uint16_t offset; std::uint8_t length; };\n");
	std::printf("inline constexpr decomposition decompositions[%zu] = {", decomp.size());
	std::size_t i = 0;
	for (auto& [c, d] : decomp) {
		if (pool.size() > 0xFFFF || d.size() > 0xFF) { fail("decomposition pool overflow"); }
		std::printf("%s{0x%X,%zu,%zu},", i++ % 8 ? "" : "\n\t", unsigned(c), pool.size(), d.size());
		pool += d;
	}
	std::printf("\n};\n");
	std::printf("inline constexpr char32_t decomposition_pool[%zu] = {", pool.size());
	for (i = 0; i < pool.size(); ++i) { std::printf("%s0x%X,", i % 12 ? "" : "\n\t", unsigned(pool[i])); }
	std::printf("\n};\n\n");
	std::printf("// Combining classes of the non-starters in those decompositions.\n");
	std::printf("struct combining { char32_t c; std::uint8_t ccc; };\n");
	std::printf("inline constexpr combining combining_classes[%zu] = {", nonstarters.size());
	for (auto& [c, ccc] : nonstarters) { std::printf("{0x%X,%u},", unsigned(c), ccc); }
	std::printf("};\n");
	std::printf("// Engine characters with no normalization boundary before them.\n");
	std::printf("inline constexpr char32_t no_boundary_before[%zu] = {", no_boundary.size());
	for (char32_t c : no_boundary) { std::printf("0x%X,", unsigned(c)); }
	std::printf("};\n\n");
	std::printf("// Canonical compositions of a starter with one of the non-starters above.\n");
	std::printf("struct composition { char32_t first, second, composed; };\n");
	std::printf("inline constexpr composition compositions[%zu] = {", pairs.size());
	i = 0;
	for (auto& p : pairs) { std::printf("%s{0x%X,0x%X,0x%X},", i++ % 4 ? "" : "\n\t", unsigned(p[0]), unsigned(p[1]), unsigned(p[2])); }
	std::printf("\n};\n\n}  // namespace analysis::cjk::tables\n");
	return 0;
}
