// A minimal JSON reader for the test vector files; strings keep raw bytes.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace test_json {

struct json {
	std::variant<std::nullptr_t, bool, double, std::string, std::vector<json>, std::map<std::string, json>> v;
	const json& operator[](const std::string& k) const { return std::get<std::map<std::string, json>>(v).at(k); }
	const std::vector<json>& array() const { return std::get<std::vector<json>>(v); }
	const std::string& str() const { return std::get<std::string>(v); }
	double num() const { return std::get<double>(v); }
	bool boolean() const { return std::get<bool>(v); }
};

inline json parse(const char*& p) {
	while (*p == ' ') { ++p; }
	if (*p == '{') {
		std::map<std::string, json> m;
		++p;
		while (*p != '}') {
			std::string k = parse(p).str();
			++p;  // ':'
			m[k] = parse(p);
			if (*p == ',') { ++p; }
		}
		++p;
		return {std::move(m)};
	}
	if (*p == '[') {
		std::vector<json> a;
		++p;
		while (*p != ']') {
			a.push_back(parse(p));
			if (*p == ',') { ++p; }
		}
		++p;
		return {std::move(a)};
	}
	if (*p == '"') {
		std::string s;
		for (++p; *p != '"'; ++p) {
			if (*p != '\\') {
				s.push_back(*p);
				continue;
			}
			switch (*++p) {
				case 'n': s.push_back('\n'); break;
				case 't': s.push_back('\t'); break;
				case 'u': s.push_back(char(std::stoi(std::string(p + 1, 4), nullptr, 16))); p += 4; break;
				default: s.push_back(*p);
			}
		}
		++p;
		return {std::move(s)};
	}
	if (*p == 't') { p += 4; return {true}; }
	if (*p == 'f') { p += 5; return {false}; }
	char* e;
	double d = std::strtod(p, &e);
	p = e;
	return {d};
}

}  // namespace test_json
