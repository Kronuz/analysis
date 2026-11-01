// Indexes the "text" fields of a JSON-lines corpus (argv[1], first argv[2] documents) into
// a per-document map of terms to wdf and positions (what a Xapian::Document holds);
// reports MB/s and ns per term for STEM_NONE and STEM_SOME.
//
// With ANALYSIS_BENCH_XAPIAN (CMake finds Xapian; off by default) it also indexes the same texts
// with Xapian::TermGenerator into a Xapian::Document, and before timing checks that, for every
// document, the Document holds exactly the terms, wdfs and positions the map holds.
#include "analysis.hh"

#ifdef ANALYSIS_BENCH_XAPIAN
#include <xapian.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using Terms = std::unordered_map<std::string, std::pair<std::uint32_t, std::vector<std::uint32_t>>>;

static double best_seconds(const std::function<void()>& pass) {
	double best = 1e300;
	for (int trial = 0; trial < 3; ++trial) {
		auto t0 = std::chrono::steady_clock::now();
		pass();
		best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	}
	return best;
}

#ifdef ANALYSIS_BENCH_XAPIAN
static volatile std::size_t sink;

static Xapian::TermGenerator::stem_strategy xapian_strategy(analysis::stem_strategy s) {
	return s == analysis::stem_strategy::none ? Xapian::TermGenerator::STEM_NONE : Xapian::TermGenerator::STEM_SOME;
}

// True when the Document holds the same terms, wdfs and positions as the map.
static bool same_terms(const Xapian::Document& d, const Terms& t) {
	std::size_t seen = 0;
	for (auto it = d.termlist_begin(); it != d.termlist_end(); ++it, ++seen) {
		auto found = t.find(*it);
		if (found == t.end() || found->second.first != it.get_wdf()) { return false; }
		std::vector<std::uint32_t> positions(it.positionlist_begin(), it.positionlist_end());
		if (positions != found->second.second) { return false; }
	}
	return seen == t.size();
}
#endif

int main(int argc, char** argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: analysis_bench CORPUS_JSONL N\n");
		return 2;
	}
	std::vector<std::string> docs;
	std::size_t bytes = 0;
	std::ifstream in(argv[1]);
	for (std::string line; docs.size() < std::stoul(argv[2]) && std::getline(in, line);) {
		auto at = line.find("\"text\": \"");
		if (at == std::string::npos) { continue; }
		at += 9;
		docs.push_back(line.substr(at, line.find('"', at) - at));
		bytes += docs.back().size();
	}
	snowball::stemmer english("english");
	for (auto strategy : {analysis::stem_strategy::none, analysis::stem_strategy::some}) {
		std::size_t terms = 0;
		const double best = best_seconds([&] {
			terms = 0;
			for (const auto& d : docs) {
				analysis::term_generator g;
				g.stem = strategy;
				g.stemmer = &english;
				Terms doc;
				g.index_text(d, [&](std::string_view term, std::uint32_t wdf, std::uint32_t pos) {
					auto& t = doc[std::string(term)];
					t.first += wdf;
					if (pos) { t.second.push_back(pos); }
					++terms;
				});
			}
		});
		const char* name = strategy == analysis::stem_strategy::none ? "stem_none" : "stem_some";
		std::printf("%s: %zu docs, %.1f MB, %zu terms, %.1f MB/s, %.1f ns/term\n", name,
		            docs.size(), bytes / 1e6, terms, bytes / 1e6 / best, best * 1e9 / double(terms));
#ifdef ANALYSIS_BENCH_XAPIAN
		Xapian::Stem stem("en");
		Xapian::TermGenerator xg;
		xg.set_stemmer(stem);
		xg.set_stemming_strategy(xapian_strategy(strategy));
		for (const auto& d : docs) {
			analysis::term_generator g;
			g.stem = strategy;
			g.stemmer = &english;
			Terms ours;
			g.index_text(d, [&](std::string_view term, std::uint32_t wdf, std::uint32_t pos) {
				auto& t = ours[std::string(term)];
				t.first += wdf;
				if (pos) { t.second.push_back(pos); }
			});
			Xapian::Document doc;
			xg.set_document(doc);
			xg.index_text(d);
			if (!same_terms(doc, ours)) {
				std::fprintf(stderr, "%s: Xapian and analysis disagree on a document; the row is void\n", name);
				return 1;
			}
		}
		const double xbest = best_seconds([&] {
			for (const auto& d : docs) {
				Xapian::Document doc;
				xg.set_document(doc);
				xg.index_text(d);
				sink = sink + doc.termlist_count();
			}
		});
		std::printf("%s: Xapian %s, %.1f MB/s (same terms, wdfs and positions on all %zu documents)\n", name, XAPIAN_VERSION,
		            bytes / 1e6 / xbest, docs.size());
#endif
	}
}
