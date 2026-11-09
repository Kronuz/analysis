// A three-stage lookup table over all code points, with identical blocks shared at each
// level and the block sizes that take the fewest bytes, printed as C++ arrays for the
// generated tables.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace staged {

template <class T>
struct table {
	unsigned leaf_shift = 0, mid_shift = 0;
	std::vector<std::uint16_t> top, mid;
	std::vector<T> leaves;

	bool wide_top() const { return mid.size() >> (mid_shift - leaf_shift) > 256; }
	bool wide_mid() const { return leaves.size() >> leaf_shift > 256; }
	std::size_t bytes() const { return top.size() * (wide_top() ? 2 : 1) + mid.size() * (wide_mid() ? 2 : 1) + leaves.size() * sizeof(T); }
};

template <class T>
table<T> build(const std::vector<T>& values) {
	table<T> best;
	for (unsigned ls = 3; ls <= 8; ++ls) {
		for (unsigned ms = ls + 1; ms <= 12; ++ms) {
			table<T> t;
			t.leaf_shift = ls;
			t.mid_shift = ms;
			std::map<std::vector<T>, std::uint16_t> leaf_ids;
			std::vector<std::uint16_t> leaf_of;
			for (std::size_t x = 0; x < values.size(); x += std::size_t(1) << ls) {
				std::vector<T> blk(values.begin() + x, values.begin() + x + (std::size_t(1) << ls));
				auto [it, added] = leaf_ids.emplace(blk, std::uint16_t(leaf_ids.size()));
				if (added) { t.leaves.insert(t.leaves.end(), blk.begin(), blk.end()); }
				leaf_of.push_back(it->second);
			}
			std::map<std::vector<std::uint16_t>, std::uint16_t> mid_ids;
			const std::size_t k = std::size_t(1) << (ms - ls);
			for (std::size_t x = 0; x < leaf_of.size(); x += k) {
				std::vector<std::uint16_t> blk(leaf_of.begin() + x, leaf_of.begin() + x + k);
				auto [it, added] = mid_ids.emplace(blk, std::uint16_t(mid_ids.size()));
				if (added) { t.mid.insert(t.mid.end(), blk.begin(), blk.end()); }
				t.top.push_back(it->second);
			}
			if (best.leaves.empty() || t.bytes() < best.bytes()) { best = std::move(t); }
		}
	}
	return best;
}

// The table as C++: `name_top`, `name_mid`, `name_leaves` and their shifts.
template <class T>
std::string print(const std::string& name, const table<T>& t, const char* leaf_type) {
	std::string out = "inline constexpr unsigned " + name + "_leaf_shift = " + std::to_string(t.leaf_shift) + ", " + name + "_mid_shift = " +
	                  std::to_string(t.mid_shift) + ";  // " + std::to_string(t.bytes()) + " bytes\n";
	auto emit = [&](const char* type, const char* part, const auto& v) {
		out += std::string("inline constexpr ") + type + " " + name + "_" + part + "[" + std::to_string(v.size()) + "] = {";
		for (std::size_t i = 0; i < v.size(); ++i) { out += (i % 32 ? "" : "\n\t") + std::to_string(unsigned(v[i])) + ","; }
		out += "\n};\n";
	};
	emit(t.wide_top() ? "std::uint16_t" : "std::uint8_t", "top", t.top);
	emit(t.wide_mid() ? "std::uint16_t" : "std::uint8_t", "mid", t.mid);
	emit(leaf_type, "leaves", t.leaves);
	return out;
}

}  // namespace staged
