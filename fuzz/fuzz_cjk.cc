// libFuzzer target for cjk::segmenter: on any bytes, the segments are non-empty, in
// order, and tile the text exactly; a second pass over the same text in a reused
// segmenter gives the same segments.
#include "cjk/segmenter.hh"

#include <cstdlib>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	static analysis::cjk::segmenter reused;
	const std::string_view text(reinterpret_cast<const char*>(data), size);
	analysis::cjk::segmenter fresh(text);
	std::vector<analysis::cjk::segment> first;
	std::size_t at = 0;
	for (analysis::cjk::segment s; fresh.next(s);) {
		if (s.begin != at || s.end <= s.begin || s.end > size || int(s.kind) > 4) { std::abort(); }
		at = s.end;
		first.push_back(s);
	}
	if (at != size) { std::abort(); }
	reused.reset(text);
	std::size_t i = 0;
	for (analysis::cjk::segment s; reused.next(s); ++i) {
		if (i >= first.size() || s.begin != first[i].begin || s.end != first[i].end || s.kind != first[i].kind) { std::abort(); }
	}
	if (i != first.size()) { std::abort(); }
	return 0;
}
