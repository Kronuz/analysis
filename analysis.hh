/*
 * Copyright (c) 2026 Germán Méndez Bravo (Kronuz)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Text to index terms with Xapian TermGenerator's word rules, case folding, stemming
// strategies, stop strategies, and CJK n-grams. The rules are in README.md.

#pragma once

#include "snowball.hh"
#include "unicode.hh"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>

namespace analysis {

enum class stem_strategy : std::uint8_t {
	none,   // unstemmed terms only
	some,   // unstemmed terms, plus positionless Z-prefixed stems of words starting with a letter
	all,    // stemmed terms only
	all_z,  // Z-prefixed stemmed terms only
};

enum class stop_strategy : std::uint8_t {
	none,     // index stop words
	all,      // drop stop words entirely
	stemmed,  // drop only the Z-prefixed stems of stop words (stem_strategy::some)
};

// A set of stop words, matched against lowercased words.
class stopper {
public:
	stopper() = default;
	stopper(std::initializer_list<std::string_view> words) {
		for (auto w : words) { add(w); }
	}
	void add(std::string_view word) { words_.emplace(word); }
	bool operator()(std::string_view word) const { return words_.find(word) != words_.end(); }

private:
	struct hash {
		using is_transparent = void;
		std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
	};
	std::unordered_set<std::string, hash, std::equal_to<>> words_;
};

// Letters, marks, numbers, and connector punctuation.
constexpr bool is_word_char(char32_t c) noexcept {
	using k = unicode::category;
	auto cat = unicode::category_of(c);
	return (cat >= k::uppercase_letter && cat <= k::other_number) || cat == k::connector_punctuation;
}

// Word characters split into n-grams under cjk_ngram: the Unicode 17.0 blocks of Chinese,
// Japanese, Korean, Thai, Lao, Myanmar, and Khmer, and the halfwidth forms.
constexpr bool is_ngram_char(char32_t c) noexcept {
	constexpr char32_t ranges[][2] = {
		{0x0E00, 0x0E7F},    // Thai
		{0x0E80, 0x0EFF},    // Lao
		{0x1000, 0x109F},    // Myanmar
		{0x1100, 0x11FF},    // Hangul Jamo
		{0x1780, 0x17FF},    // Khmer
		{0x3000, 0x303F},    // CJK Symbols and Punctuation
		{0x3040, 0x309F},    // Hiragana
		{0x30A0, 0x30FF},    // Katakana
		{0x3100, 0x312F},    // Bopomofo
		{0x3130, 0x318F},    // Hangul Compatibility Jamo
		{0x3190, 0x319F},    // Kanbun
		{0x31A0, 0x31BF},    // Bopomofo Extended
		{0x31F0, 0x31FF},    // Katakana Phonetic Extensions
		{0x3200, 0x32FF},    // Enclosed CJK Letters and Months
		{0x3400, 0x4DBF},    // CJK Unified Ideographs Extension A
		{0x4E00, 0x9FFF},    // CJK Unified Ideographs
		{0xA700, 0xA71F},    // Modifier Tone Letters
		{0xA960, 0xA97F},    // Hangul Jamo Extended-A
		{0xA9E0, 0xA9FF},    // Myanmar Extended-B
		{0xAA60, 0xAA7F},    // Myanmar Extended-A
		{0xAC00, 0xD7AF},    // Hangul Syllables
		{0xD7B0, 0xD7FF},    // Hangul Jamo Extended-B
		{0xF900, 0xFAFF},    // CJK Compatibility Ideographs
		{0xFE30, 0xFE4F},    // CJK Compatibility Forms
		{0xFF61, 0xFFDC},    // Halfwidth CJK punctuation, katakana, and hangul
		{0x1AFF0, 0x1AFFF},  // Kana Extended-B
		{0x1B000, 0x1B0FF},  // Kana Supplement
		{0x1B100, 0x1B12F},  // Kana Extended-A
		{0x1B130, 0x1B16F},  // Small Kana Extension
		{0x20000, 0x2A6DF},  // CJK Unified Ideographs Extension B
		{0x2A700, 0x2B73F},  // CJK Unified Ideographs Extension C
		{0x2B740, 0x2B81F},  // CJK Unified Ideographs Extension D
		{0x2B820, 0x2CEAF},  // CJK Unified Ideographs Extension E
		{0x2CEB0, 0x2EBEF},  // CJK Unified Ideographs Extension F
		{0x2EBF0, 0x2EE5F},  // CJK Unified Ideographs Extension I
		{0x2F800, 0x2FA1F},  // CJK Compatibility Ideographs Supplement
		{0x30000, 0x3134F},  // CJK Unified Ideographs Extension G
		{0x31350, 0x323AF},  // CJK Unified Ideographs Extension H
	};
	if (c < 0x0E00) { return false; }
	for (const auto& r : ranges) {
		if (c < r[0]) { return false; }
		if (c <= r[1]) { return true; }
	}
	return false;
}

namespace detail {

constexpr bool is_digit(char32_t c) noexcept { return unicode::category_of(c) == unicode::category::decimal_number; }

// Removed wherever they appear: zero-width non-joiner and joiner, word joiner, and
// zero-width no-break space.
constexpr bool is_ignorable(char32_t c) noexcept { return c == 0x200C || c == 0x200D || c == 0x2060 || c == 0xFEFF; }

// A character between two word characters that joins them: a letter infix joins unless
// both are digits, a numeric infix only when both are. append is what the word keeps
// (0 drops the character).
struct infix {
	enum kind_t : std::uint8_t { none, letter, numeric } kind = none;
	char32_t append = 0;
};

constexpr infix infix_of(char32_t c) noexcept {
	switch (c) {
		case U'&':
		case U'\'':
		case 0x00B7:  // middle dot
		case 0x05F4:  // Hebrew punctuation gershayim
		case 0x2027:  // hyphenation point
			return {infix::letter, c};
		case 0x2019:  // right single quotation mark
		case 0x201B:  // single high-reversed-9 quotation mark
			return {infix::letter, U'\''};
		case 0x00AD:  // soft hyphen
			return {infix::letter, 0};
		case U',':
		case U'.':
		case U';':
		case 0x037E:  // Greek question mark
		case 0x0589:  // Armenian full stop
		case 0x060D:  // Arabic date separator
		case 0x07F8:  // NKo comma
		case 0x2044:  // fraction slash
		case 0xFE10:  // presentation form for vertical comma
		case 0xFE13:  // presentation form for vertical colon
		case 0xFE14:  // presentation form for vertical semicolon
			return {infix::numeric, c};
		case 0x200B:  // zero width space
			return {infix::numeric, 0};
		default:
			return {};
	}
}

constexpr bool is_acronym_letter(char c) noexcept { return c >= 'A' && c <= 'Z'; }

}  // namespace detail

struct field {
	std::uint32_t wdf_inc = 1;
	std::string_view prefix;
	bool positions = true;
};

// The word rules of the term generator, usable on their own: collects the word or
// acronym at a position into text(), lowercased.
class word_scanner {
public:
	bool cjk_ngram = false;

	// The most recently collected word.
	const std::string& text() const noexcept { return text_; }

	// Collects the word at p, which must start with a word character, and returns where it
	// ends. An acronym ("U.S.A.") is taken whole when one starts at p.
	const char* scan(const char* p, const char* end) {
		if (const char* after = acronym(p, end)) { return after; }
		return word(p, end);
	}

	// Collects the word at p into text_ and returns where it ends.
	const char* word(const char* p, const char* end) {
		text_.clear();
		const char* q = p;
		char32_t c = unicode::next_utf8(q, end);
		for (;;) {
			unicode::append_utf8(text_, unicode::tolower(c));
			const char32_t prev = c;
			p = q;
			// Skip ignorables, then continue with a word character or a valid infix.
			for (;;) {
				if (p == end) { return p; }
				q = p;
				c = unicode::next_utf8(q, end);
				if (!detail::is_ignorable(c)) { break; }
				p = q;
			}
			if (is_word_char(c)) {
				if (cjk_ngram && is_ngram_char(c)) { return p; }
				continue;
			}
			if (const auto in = detail::infix_of(c); in.kind != detail::infix::none) {
				const char* r = q;
				const char32_t next = r < end ? unicode::next_utf8(r, end) : 0;
				if (r > q && is_word_char(next) && !(cjk_ngram && is_ngram_char(next))) {
					const bool both_digits = detail::is_digit(prev) && detail::is_digit(next);
					if (in.kind == detail::infix::letter ? !both_digits : both_digits) {
						if (in.append) { unicode::append_utf8(text_, in.append); }
						c = next;
						q = r;
						continue;
					}
				}
			}
			return suffix(p, end);
		}
	}

	// Appends a suffix of one to three '+' or '#' at p when no word character follows it.
	const char* suffix(const char* p, const char* end) {
		const char* q = p;
		while (q < end && q - p < 4 && (*q == '+' || *q == '#')) { ++q; }
		if (q == p || q - p > 3) { return p; }
		if (q < end) {
			const char* r = q;
			if (is_word_char(unicode::next_utf8(r, end))) { return p; }
		}
		text_.append(p, q);
		return q;
	}

	// Forgets the failed acronym chain a scan remembers. Call it when the text scanned
	// changes under the same pointers; scanning a new buffer needs no reset.
	void reset() noexcept { fail_from_ = fail_to_ = fail_end_ = nullptr; }

	// An acronym: single ASCII capitals separated by periods ("U.S.A.", "A.B"), not
	// followed by a word character. Collects it into text_ and returns where it ends.
	const char* acronym(const char* p, const char* end) {
		if (end - p < 3 || !detail::is_acronym_letter(p[0]) || p[1] != '.' || !detail::is_acronym_letter(p[2])) { return nullptr; }
		// Every start on a chain that failed fails the same way: scan the chain once.
		if (end == fail_end_ && p >= fail_from_ && p < fail_to_ && (p - fail_from_) % 2 == 0) { return nullptr; }
		text_.assign(1, char(p[0] - 'A' + 'a'));
		const char* q = p + 1;
		while (end - q >= 2 && q[0] == '.' && detail::is_acronym_letter(q[1])) {
			text_.push_back(char(q[1] - 'A' + 'a'));
			q += 2;
		}
		const char* const chain_end = q;
		if (q < end && *q == '.') { ++q; }
		if (q < end) {
			const char* r = q;
			if (is_word_char(unicode::next_utf8(r, end))) {
				fail_from_ = p;
				fail_to_ = chain_end;
				fail_end_ = end;
				return nullptr;
			}
		}
		return q;
	}

private:
	std::string text_;
	const char* fail_from_ = nullptr;  // the last failed acronym chain [from, to) in a text ending at end
	const char* fail_to_ = nullptr;
	const char* fail_end_ = nullptr;
};

class term_generator {
public:
	stem_strategy stem = stem_strategy::some;
	stop_strategy stop = stop_strategy::stemmed;
	bool cjk_ngram = false;
	std::size_t max_word_length = 64;  // in bytes, after lowercasing; longer words are dropped
	snowball::stemmer* stemmer = nullptr;
	const analysis::stopper* stop_words = nullptr;

	std::uint32_t termpos() const noexcept { return termpos_; }
	void set_termpos(std::uint32_t pos) noexcept { termpos_ = pos; }
	void increase_termpos(std::uint32_t delta = 100) noexcept { termpos_ += delta; }

	// Calls sink(std::string_view term, std::uint32_t wdf_inc, std::uint32_t position) for
	// each term; position 0 means the term has no position. Positions continue from
	// termpos().
	template <class Sink>
	void index_text(std::string_view text, Sink&& sink, const field& f = {}) {
		if ((stem == stem_strategy::all || stem == stem_strategy::all_z) && !stemmer) { return; }
		scanner_.cjk_ngram = cjk_ngram;
		scanner_.reset();
		const char* p = text.data();
		const char* const end = p + text.size();
		while (p < end) {
			const char* q = p;
			const char32_t c = unicode::next_utf8(q, end);
			if (!is_word_char(c)) {
				p = q;
			} else if (cjk_ngram && is_ngram_char(c)) {
				p = ngrams(p, end, sink, f);
			} else {
				p = scanner_.scan(p, end);
				emit(scanner_.text(), true, sink, f);
			}
		}
	}

private:
	// Emits a run of n-gram characters as positional unigrams and positionless bigrams.
	template <class Sink>
	const char* ngrams(const char* p, const char* end, Sink& sink, const field& f) {
		prev_.clear();
		while (p < end) {
			const char* q = p;
			const char32_t c = unicode::next_utf8(q, end);
			if (!is_word_char(c) || !is_ngram_char(c)) { break; }
			word_.clear();
			unicode::append_utf8(word_, unicode::tolower(c));
			emit(word_, true, sink, f);
			if (!prev_.empty()) {
				prev_ += word_;
				emit(prev_, false, sink, f);
			}
			prev_ = word_;
			p = q;
		}
		return p;
	}

	template <class Sink>
	void emit(const std::string& word, bool positional, Sink& sink, const field& f) {
		if (word.size() > max_word_length) { return; }
		const bool stopped = stop != stop_strategy::none && stop_words && (*stop_words)(word);
		if (stopped && stop == stop_strategy::all) { return; }
		const std::uint32_t pos = positional && f.positions ? ++termpos_ : 0;
		switch (stem) {
			case stem_strategy::none:
				sink(term(f.prefix, word), f.wdf_inc, pos);
				break;
			case stem_strategy::some: {
				sink(term(f.prefix, word), f.wdf_inc, pos);
				if (stemmer && !stopped) {
					const char* q = word.data();
					if (unicode::is_letter(unicode::next_utf8(q, word.data() + word.size()))) {
						sink(term("Z", f.prefix, (*stemmer)(word)), f.wdf_inc, 0);
					}
				}
				break;
			}
			case stem_strategy::all:
				sink(term(f.prefix, (*stemmer)(word)), f.wdf_inc, pos);
				break;
			case stem_strategy::all_z:
				sink(term("Z", f.prefix, (*stemmer)(word)), f.wdf_inc, pos);
				break;
		}
	}

	std::string_view term(std::string_view prefix, std::string_view word) {
		if (prefix.empty()) { return word; }
		term_.assign(prefix);
		term_ += word;
		return term_;
	}
	std::string_view term(std::string_view z, std::string_view prefix, std::string_view word) {
		term_.assign(z);
		term_ += prefix;
		term_ += word;
		return term_;
	}

	std::uint32_t termpos_ = 0;
	word_scanner scanner_;
	std::string word_, prev_, term_;
};

}  // namespace analysis
