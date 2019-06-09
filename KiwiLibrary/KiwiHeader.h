#pragma once
#include <stdio.h>
#include <cstring>
#include <cassert>
#include <cmath>

#include <array>
#include <vector>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <deque>
#include <bitset>

#include <algorithm>
#include <functional>
#include <numeric>

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <regex>

#include <memory>
#include <locale>
#include <codecvt>

#include <chrono>
#include <mutex>
#include <thread>

#define TRIE_ALLOC_ARRAY
//#define LOAD_TXT
#define CUSTOM_ALLOC

#ifdef _WIN32
typedef char16_t k_char;
#define KSTR(x) u##x
#else
typedef char16_t k_char;
#define KSTR(x) u##x
#endif

#include "KMemory.h"


class KiwiException : public std::exception
{
	std::string msg;
public:
	KiwiException(const std::string& _msg) : msg(_msg) {}
	const char* what() const noexcept override { return msg.c_str(); }
};


#ifdef CUSTOM_ALLOC
typedef std::basic_string<k_char, std::char_traits<k_char>, spool_allocator<k_char>> k_string;
#ifdef _WIN32
#else
namespace std
{
	template <>
	struct hash<k_string> {
	public:
		size_t operator() (const k_string& o) const
		{
			return hash<string>{}(string{ o.begin(), o.end() });
		};
	};
}
#endif
typedef std::basic_stringstream<k_char, std::char_traits<k_char>, spool_allocator<k_char>> k_stringstream;
typedef std::vector<k_char, pool_allocator<k_char>> k_vchar;
typedef std::vector<std::pair<k_vchar, float>, pool_allocator<std::pair<k_vchar, float>>> k_vpcf;
#include "KMemoryChar.h"
#else
typedef std::basic_string<k_char> k_string;
typedef std::basic_stringstream<k_char> k_stringstream;
typedef std::vector<k_char> k_vchar;
typedef std::vector<std::pair<k_vchar, float>> k_vpcf;
#endif