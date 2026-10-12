// libFuzzer target: the first byte picks the strategies, the rest is text. Terms are
// non-empty valid UTF-8 within the length cap, and positions strictly increase.
#include "analysis.hh"

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	static snowball::stemmer english("english");
	static const analysis::stopper stop_words{"the", "a", "and"};
	if (size == 0) { return 0; }
	analysis::term_generator g;
	g.stem = analysis::stem_strategy(data[0] & 3);
	g.stop = analysis::stop_strategy((data[0] >> 2) % 3);
	g.cjk_ngram = data[0] & 0x10;
	g.stemmer = (data[0] & 0x20) ? &english : nullptr;
	g.stop_words = &stop_words;
	const analysis::field f{1, (data[0] & 0x40) ? "XP" : "", !(data[0] & 0x80)};
	std::uint32_t last = 0;
	g.index_text(std::string_view(reinterpret_cast<const char*>(data + 1), size - 1), [&](std::string_view term, std::uint32_t, std::uint32_t pos) {
		if (term.empty() || term.size() > 64 + 3 + 64) { std::abort(); }
		for (const char *p = term.data(), *end = p + term.size(); p < end;) {
			const char* q = p;
			char32_t c = unicode::next_utf8(q, end);
			std::string back;
			unicode::append_utf8(back, c);
			if (back != std::string_view(p, std::size_t(q - p))) { std::abort(); }  // not valid UTF-8
			p = q;
		}
		if (pos) {
			if (pos <= last) { std::abort(); }
			last = pos;
		}
	}, f);
	return 0;
}
