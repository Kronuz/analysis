// Indexes the "text" fields of a JSON-lines corpus (argv[1], first argv[2] documents) into
// a per-document map of terms to wdf and positions (what a Xapian::Document holds);
// reports MB/s and ns per term for STEM_NONE and STEM_SOME.
#include "analysis.hh"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

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
		double best = 1e300;
		std::size_t terms = 0;
		for (int trial = 0; trial < 3; ++trial) {
			terms = 0;
			auto t0 = std::chrono::steady_clock::now();
			for (const auto& d : docs) {
				analysis::term_generator g;
				g.stem = strategy;
				g.stemmer = &english;
				std::unordered_map<std::string, std::pair<std::uint32_t, std::vector<std::uint32_t>>> doc;
				g.index_text(d, [&](std::string_view term, std::uint32_t wdf, std::uint32_t pos) {
					auto& t = doc[std::string(term)];
					t.first += wdf;
					if (pos) { t.second.push_back(pos); }
					++terms;
				});
			}
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		std::printf("%s: %zu docs, %.1f MB, %zu terms, %.1f MB/s, %.1f ns/term\n", strategy == analysis::stem_strategy::none ? "stem_none" : "stem_some",
		            docs.size(), bytes / 1e6, terms, bytes / 1e6 / best, best * 1e9 / double(terms));
	}
}
