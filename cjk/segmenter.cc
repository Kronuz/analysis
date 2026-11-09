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

#include "cjk/segmenter.hh"

#include "cjk/tables.hh"
#include "fst.hh"
#include "unicode.hh"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "cjdict.inc"  // generated from data/cjdict.txt by tools/cjdict_gen.cc

namespace analysis::cjk {

namespace {

// A three-stage table lookup (tools/staged.hh builds them).
template <unsigned LeafShift, unsigned MidShift, class Top, class Mid, class Leaf>
constexpr Leaf staged_get(const Top* top, const Mid* mid, const Leaf* leaves, char32_t c) noexcept {
	constexpr unsigned per_mid = MidShift - LeafShift;
	const unsigned m = top[c >> MidShift];
	const unsigned l = mid[(m << per_mid) | ((c >> LeafShift) & ((1u << per_mid) - 1))];
	return leaves[(l << LeafShift) | (c & ((1u << LeafShift) - 1))];
}

unsigned class_of(char32_t c) noexcept {
	using namespace tables;
	return staged_get<class_leaf_shift, class_mid_shift>(class_top, class_mid, class_leaves, c);
}

std::uint16_t id_of(char32_t c) noexcept {
	using namespace dictionary;
	return staged_get<id_leaf_shift, id_mid_shift>(id_top, id_mid, id_leaves, c);
}

word_kind kind_of(int status) noexcept {
	return status < 100 ? word_kind::none : status < 200 ? word_kind::number : status < 300 ? word_kind::letter : status < 400 ? word_kind::kana : word_kind::ideo;
}

// NFKC, for the dictionary path only: the decompositions and compositions of the
// characters the Chinese/Japanese engine handles.
std::u32string_view decomposition(const char32_t& c) noexcept {
	using tables::decompositions;
	const auto* it = std::lower_bound(std::begin(decompositions), std::end(decompositions), c, [](const auto& d, char32_t x) { return d.c < x; });
	if (it == std::end(decompositions) || it->c != c) { return {&c, 1}; }
	return {tables::decomposition_pool + it->offset, it->length};
}

unsigned combining_class(char32_t c) noexcept {
	for (const auto& x : tables::combining_classes) {
		if (x.c == c) { return x.ccc; }
	}
	return 0;
}

bool has_boundary_before(char32_t c) noexcept {
	return std::find(std::begin(tables::no_boundary_before), std::end(tables::no_boundary_before), c) == std::end(tables::no_boundary_before);
}

char32_t compose(char32_t first, char32_t second) noexcept {
	for (const auto& x : tables::compositions) {
		if (x.first == first && x.second == second) { return x.composed; }
	}
	return 0;
}

// Appends NFKC of one normalization fragment (a character and the ones with no boundary
// before them) to out: decompose, order the marks canonically, compose.
void nfkc_fragment(std::u32string_view fragment, std::u32string& work, std::u32string& out) {
	work.clear();
	for (const char32_t& c : fragment) { work += decomposition(c); }
	for (std::size_t i = 1; i < work.size(); ++i) {
		for (std::size_t j = i; j > 0; --j) {
			const unsigned a = combining_class(work[j - 1]), b = combining_class(work[j]);
			if (b == 0 || a <= b) { break; }
			std::swap(work[j - 1], work[j]);
		}
	}
	std::size_t starter = out.size();
	unsigned last = combining_class(work[0]) ? 256 : 0;
	out.push_back(work[0]);
	for (std::size_t i = 1; i < work.size(); ++i) {
		const char32_t c = work[i];
		const unsigned cc = combining_class(c);
		if (const char32_t composed = compose(out[starter], c); composed && (last < cc || last == 0)) {
			out[starter] = composed;
			continue;
		}
		if (cc == 0) { starter = out.size(); }
		last = cc;
		out.push_back(c);
	}
}

const fst::map& dictionary_fst() {
	static const fst::map m(std::string_view(dictionary::fst_bytes, dictionary::fst_size), fst::trusted);
	return m;
}

// Follows the byte label from n; false when there is no such transition.
bool step(const fst::map& dict, fst::map::node& n, std::uint64_t& out, std::uint8_t label) noexcept {
	const std::size_t i = n.find(label);
	if (i == n.count) { return false; }
	const fst::transition t = n.at(i);
	out += t.output;
	n = dict.load(t.target);
	return true;
}

bool step_id(const fst::map& dict, fst::map::node& n, std::uint64_t& out, std::uint16_t id) noexcept {
	constexpr unsigned single = dictionary::single_byte_ids;
	if (id < single) { return step(dict, n, out, std::uint8_t(id)); }
	return step(dict, n, out, std::uint8_t(single + ((id - single) >> 8))) && step(dict, n, out, std::uint8_t((id - single) & 0xFF));
}

// ICU's CjkBreakEngine constants: the longest word looked up, in UTF-16 code units; the
// cost of a character no word starts with; and the katakana run heuristic.
constexpr unsigned max_word_units = 20;
constexpr std::uint32_t max_cost = 255;
constexpr int max_katakana_group = 20;

constexpr std::uint32_t katakana_cost(int length) noexcept {
	constexpr std::uint32_t cost[] = {8192, 984, 408, 240, 204, 252, 300, 372, 480};
	return length > 8 ? 8192 : cost[length];
}

constexpr bool is_katakana(char32_t c) noexcept { return (c >= 0x30A1 && c <= 0x30FE && c != 0x30FB) || (c >= 0xFF66 && c <= 0xFF9F); }

constexpr bool is_hangul_syllable(char32_t c) noexcept { return c >= 0xAC00 && c <= 0xD7A3; }

}  // namespace

bool segmenter::next(segment& s) {
	if (next_break_ < breaks_.size()) {
		s = {pos_, breaks_[next_break_++], segment_kind_};
		pos_ = s.end;
		return true;
	}
	if (pos_ >= text_.size()) { return false; }
	word_kind kind;
	bool dictionary;
	const std::size_t end = rule_boundary(pos_, kind, dictionary);
	if (dictionary) {
		breaks_.clear();
		next_break_ = 0;
		divide_segment(pos_, end);
		if (!breaks_.empty()) {
			breaks_.push_back(end);
			segment_kind_ = kind;
			s = {pos_, breaks_[next_break_++], kind};
			pos_ = s.end;
			return true;
		}
	}
	s = {pos_, end, kind};
	pos_ = end;
	return true;
}

// ICU's handleNext: run the state machine from `from` and return the end of the longest
// match, with the status of the state that accepted it.
std::size_t segmenter::rule_boundary(std::size_t from, word_kind& kind, bool& dictionary) const noexcept {
	const char* const base = text_.data();
	const char* const end = base + text_.size();
	const char* p = base + from;
	unsigned state = 1;  // start; 0 is stop
	std::size_t result = from;
	int status = 0;
	dictionary = false;
	for (;;) {
		const char* q = p;
		unsigned category = 1;  // end of text
		if (p < end) {
			category = class_of(unicode::next_utf8(q, end)) & tables::category_mask;
			dictionary |= category >= tables::dict_start;
		}
		state = tables::next[state][category];
		if (tables::accepting[state]) {
			result = std::size_t(q - base);
			status = tables::status[state];
		}
		if (state == 0 || p == end) { break; }
		p = q;
	}
	if (result == from) {  // no rule matched: one character, status 0
		const char* q = base + from;
		unicode::next_utf8(q, end);
		result = std::size_t(q - base);
		status = 0;
	}
	kind = kind_of(status);
	return result;
}

// ICU's DictionaryCache::populateDictionary: each run of characters the Chinese/Japanese
// engine handles is divided by the dictionary; a run of other dictionary characters (an
// engine ICU lacks here, as for Hangul) is skipped whole, as ICU's UnhandledEngine skips
// every character of the script it met.
void segmenter::divide_segment(std::size_t begin, std::size_t end) {
	const char* const base = text_.data();
	const char* const limit = base + end;
	std::size_t pos = begin;
	while (pos < end) {
		const char* q = base + pos;
		const unsigned cls = class_of(unicode::next_utf8(q, limit));
		if ((cls & tables::category_mask) < tables::dict_start) {
			pos = std::size_t(q - base);
			continue;
		}
		if (cls & tables::cj_bit) {
			const char* r = base + pos;
			while (r < limit) {
				const char* t = r;
				if (!(class_of(unicode::next_utf8(t, limit)) & tables::cj_bit)) { break; }
				r = t;
			}
			divide_range(pos, std::size_t(r - base));
			pos = std::size_t(r - base);
			continue;
		}
		const bool common = cls & tables::common_bit;
		pos = std::size_t(q - base);
		while (pos < end) {
			const char* t = base + pos;
			const unsigned x = class_of(unicode::next_utf8(t, limit));
			const bool skipped = common ? (x & tables::common_bit) : (x & tables::category_mask) >= tables::dict_start && !(x & (tables::cj_bit | tables::common_bit));
			if (!skipped) { break; }
			pos = std::size_t(t - base);
		}
	}
}

// ICU's CjkBreakEngine::divideUpDictionaryRange: NFKC the run, find the segmentation of
// least total cost (dictionary words, unknown characters at max_cost, katakana runs), and
// add its boundaries inside the run, mapped back to the original text.
void segmenter::divide_range(std::size_t begin, std::size_t end) {
	const char* const base = text_.data();
	chars_.clear();
	offsets_.clear();
	bool changes = false;  // a character NFKC may change
	for (const char* p = base + begin; p < base + end;) {
		offsets_.push_back(std::size_t(p - base));
		chars_.push_back(unicode::next_utf8(p, base + end));
		changes |= class_of(chars_.back()) & tables::nfkc_bit;
	}
	offsets_.push_back(end);

	const std::u32string* text = &chars_;
	const std::vector<std::size_t>* map = &offsets_;
	if (changes) {
		normalized_.clear();
		map_.clear();
		for (std::size_t i = 0; i < chars_.size();) {
			std::size_t j = i + 1;
			while (j < chars_.size() && !has_boundary_before(chars_[j])) { ++j; }
			nfkc_fragment(std::u32string_view(chars_).substr(i, j - i), fragment_, normalized_);
			map_.resize(normalized_.size(), offsets_[i]);  // the fragment's characters map to its start
			i = j;
		}
		map_.push_back(end);
		if (normalized_ != chars_) {
			text = &normalized_;
			map = &map_;
		}
	}
	const std::u32string& normalized = *text;
	const std::vector<std::size_t>& positions = *map;

	const std::size_t n = normalized.size();
	ids_.resize(n);
	for (std::size_t i = 0; i < n; ++i) { ids_[i] = id_of(normalized[i]); }

	constexpr std::uint32_t unreached = std::numeric_limits<std::uint32_t>::max();
	cost_.assign(n + 1, unreached);
	cost_[0] = 0;
	prev_.assign(n + 1, -1);
	const fst::map& dict = dictionary_fst();
	bool prev_katakana = false;
	for (std::size_t i = 0; i < n; ++i) {
		if (cost_[i] == unreached) { continue; }

		// Every dictionary word that starts at i, shortest first.
		std::uint32_t lengths[max_word_units + 1], values[max_word_units + 1];
		unsigned count = 0, units = 0;
		fst::map::node node = dict.root();
		std::uint64_t out = 0;
		for (std::size_t k = i; k < n; ++k) {
			if (ids_[k] == dictionary::no_id || !step_id(dict, node, out, ids_[k])) { break; }
			units += normalized[k] > 0xFFFF ? 2 : 1;
			if (node.is_final) {
				lengths[count] = std::uint32_t(k - i + 1);
				values[count] = std::uint32_t(out + node.final_output);
				++count;
			}
			if (units >= max_word_units) { break; }
		}
		if ((count == 0 || lengths[0] != 1) && !is_hangul_syllable(normalized[i])) {
			values[count] = max_cost;
			lengths[count++] = 1;
		}
		for (unsigned j = 0; j < count; ++j) {
			const std::uint32_t c = cost_[i] + values[j];
			if (c < cost_[i + lengths[j]]) {
				cost_[i + lengths[j]] = c;
				prev_[i + lengths[j]] = std::int32_t(i);
			}
		}

		// A run of katakana is a candidate word whatever the dictionary says.
		const bool katakana = is_katakana(normalized[i]);
		if (!prev_katakana && katakana) {
			int run = 1;
			for (std::size_t j = i + 1; j < n && run < max_katakana_group && is_katakana(normalized[j]); ++j) { ++run; }
			if (run < max_katakana_group) {
				const std::uint32_t c = cost_[i] + katakana_cost(run);
				if (c < cost_[i + std::size_t(run)]) {
					cost_[i + std::size_t(run)] = c;
					prev_[i + std::size_t(run)] = std::int32_t(i);
				}
			}
		}
		prev_katakana = katakana;
	}

	path_.clear();
	if (cost_[n] == unreached) {
		path_.push_back(std::uint32_t(n));
	} else {
		for (std::int32_t i = std::int32_t(n); i > 0; i = prev_[std::size_t(i)]) { path_.push_back(std::uint32_t(i)); }
	}
	path_.push_back(0);

	// Positions inside one normalization fragment map to its start: keep the first.
	std::size_t last = 0;
	bool any = false;
	for (auto it = path_.rbegin(); it != path_.rend(); ++it) {
		const std::size_t pos = positions[*it];
		if (!any || pos > last) {
			if (pos != begin) { breaks_.push_back(pos); }
		}
		any = true;
		last = pos;
	}
	if (!breaks_.empty() && breaks_.back() == end) { breaks_.pop_back(); }
}

namespace detail {

int dictionary_cost(std::u32string_view word) {
	const fst::map& dict = dictionary_fst();
	fst::map::node node = dict.root();
	std::uint64_t out = 0;
	for (char32_t c : word) {
		const std::uint16_t id = c <= 0x10FFFF ? id_of(c) : dictionary::no_id;
		if (id == dictionary::no_id || !step_id(dict, node, out, id)) { return -1; }
	}
	return node.is_final && !word.empty() ? int(out + node.final_output) : -1;
}

std::size_t dictionary_bytes() noexcept {
	using namespace dictionary;
	return fst_size + sizeof id_top + sizeof id_mid + sizeof id_leaves;
}

}  // namespace detail

}  // namespace analysis::cjk
