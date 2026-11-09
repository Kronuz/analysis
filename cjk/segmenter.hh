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

// Word segmentation of UTF-8 text the way ICU's word break iterator does it: ICU's
// UAX #29 word rules, with runs of Han and kana divided by ICU's dictionary (cjdict).
// How it works: docs/cjk-segmenter.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace analysis::cjk {

// The kind of a segment: ICU's rule status ranges (UBRK_WORD_NONE, _NUMBER, _LETTER,
// _KANA, _IDEO) for the boundary that ends it.
enum class word_kind : std::uint8_t {
	none,    // spaces, punctuation, symbols: not a word
	number,  // ends in a digit
	letter,  // ends in a letter, including Hangul
	kana,    // reserved: ICU's rules report kana as ideo
	ideo,    // Han, Hiragana, Katakana
};

// Bytes [begin, end) of the text.
struct segment {
	std::size_t begin = 0, end = 0;
	word_kind kind = word_kind::none;
};

// Splits text into the segments between consecutive word boundaries; together they
// cover the text. Bytes that are not valid UTF-8 read as ISO-8859-1, one at a time, as
// in term_generator. Reuse one segmenter across texts to keep its buffers.
class segmenter {
public:
	segmenter() = default;
	explicit segmenter(std::string_view text) { reset(text); }

	// Starts over on text, which must outlive the segments read from it.
	void reset(std::string_view text) noexcept {
		text_ = text;
		pos_ = 0;
		breaks_.clear();
		next_break_ = 0;
	}

	// The next segment; false at the end of the text.
	bool next(segment& s);

private:
	std::size_t rule_boundary(std::size_t from, word_kind& kind, bool& dictionary) const noexcept;
	void divide_segment(std::size_t begin, std::size_t end);
	void divide_range(std::size_t begin, std::size_t end);

	std::string_view text_;
	std::size_t pos_ = 0;
	std::vector<std::size_t> breaks_;  // dictionary boundaries inside the current rule segment
	std::size_t next_break_ = 0;
	word_kind segment_kind_ = word_kind::none;

	// Buffers of the dictionary path, kept between ranges.
	std::u32string chars_, normalized_, fragment_;
	std::vector<std::size_t> offsets_, map_;
	std::vector<std::uint16_t> ids_;
	std::vector<std::uint32_t> cost_;
	std::vector<std::int32_t> prev_;
	std::vector<std::uint32_t> path_;
};

namespace detail {

// The cost of a word in the dictionary, or -1 when it is not one (for tests).
int dictionary_cost(std::u32string_view word);

// Bytes of the built-in dictionary: the FST and the code point to id table.
std::size_t dictionary_bytes() noexcept;

}  // namespace detail

}  // namespace analysis::cjk
