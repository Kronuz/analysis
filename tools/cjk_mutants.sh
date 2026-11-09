#!/bin/sh
# Breaks cjk/segmenter.cc one way at a time and checks that ctest's cjk test fails.
#   tools/cjk_mutants.sh BUILD_DIR   (a configured build; restores the source after each)
# Exits 1 when a mutant survives.
set -u
cd "$(dirname "$0")/.."
build=$1
survived=0
run() {
	cp cjk/segmenter.cc "$build/segmenter.cc.orig"
	sed -i.bak "$2" cjk/segmenter.cc && rm -f cjk/segmenter.cc.bak
	if cmp -s cjk/segmenter.cc "$build/segmenter.cc.orig"; then
		echo "$1: NOT APPLIED"
		survived=1
	elif ! cmake --build "$build" --target analysis_cjk_test -j4 >/dev/null 2>&1; then
		echo "$1: does not build"
		survived=1
	elif "$build/analysis_cjk_test" test/vectors data/cjdict.txt >/dev/null 2>&1; then
		echo "$1: SURVIVED"
		survived=1
	else
		echo "$1: killed"
	fi
	cp "$build/segmenter.cc.orig" cjk/segmenter.cc
}
run tie-break 's/if (c < cost_\[i + lengths\[j\]\]) {/if (c <= cost_[i + lengths[j]]) {/'
run unknown-cost 's/constexpr std::uint32_t max_cost = 255;/constexpr std::uint32_t max_cost = 254;/'
run katakana-cost 's/{8192, 984, 408, 240, 204, 252, 300, 372, 480}/{8192, 984, 408, 240, 204, 252, 300, 372, 400}/'
run katakana-start 's/if (!prev_katakana \&\& katakana) {/if (katakana) {/'
run no-compose 's/composed \&\& (last < cc || last == 0)/composed \&\& false/'
run no-dedupe 's/if (!any || pos > last) {/if (true) {/'
run keep-end 's/if (!breaks_.empty() \&\& breaks_.back() == end) { breaks_.pop_back(); }//'
run common-skip 's/const bool common = cls \& tables::common_bit;/const bool common = false;/'
run nfkc-fastpath 's/changes |= class_of(chars_.back()) \& tables::nfkc_bit;/changes = false;/'
run no-dictionary 's/divide_range(pos, std::size_t(r - base));//'
run status-ignored 's/status = tables::status\[state\];/status = 0;/'
run longest-match 's/if (state == 0 || p == end) { break; }/if (state == 0 || p == end || tables::accepting[state]) { break; }/'
# Equivalent for ICU 78.3's data, so not run: max_katakana_group 20 -> 21 (a 20-katakana run
# at 8,192 always loses to 20 unknown characters at 255), max_word_units 20 -> 19 (no word
# is longer than 16 characters).
cmake --build "$build" --target analysis_cjk_test -j4 >/dev/null 2>&1
exit $survived
