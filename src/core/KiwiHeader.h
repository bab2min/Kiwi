#pragma once
#include <cstdio>
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

#include <chrono>
#include <mutex>
#include <thread>

#define TRIE_ALLOC_ARRAY
#define CUSTOM_ALLOC

#define KSTR(x) u##x

#include "KMemory.h"

namespace kiwi
{
	typedef char16_t k_char;

	class KiwiException : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class KiwiUnicodeException : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};


#ifdef CUSTOM_ALLOC
	typedef std::basic_string<k_char, std::char_traits<k_char>, spool_allocator<k_char>> k_string;
	typedef std::basic_stringstream<k_char, std::char_traits<k_char>, spool_allocator<k_char>> k_stringstream;
	typedef std::vector<k_char, pool_allocator<k_char>> k_vchar;
	typedef std::vector<std::pair<k_vchar, float>, pool_allocator<std::pair<k_vchar, float>>> k_vpcf;
#else
	typedef std::basic_string<k_char> k_string;
	typedef std::basic_stringstream<k_char> k_stringstream;
	typedef std::vector<k_char> k_vchar;
	typedef std::vector<std::pair<k_vchar, float>> k_vpcf;
#endif
}

#ifdef CUSTOM_ALLOC
#include "KMemoryChar.h"

namespace std
{
	template<> struct hash<kiwi::k_string>
	{
		size_t operator()(const kiwi::k_string& s) const noexcept
		{
			return hash<basic_string <kiwi::k_char, char_traits<kiwi::k_char>>>{}(
				basic_string <kiwi::k_char, char_traits<kiwi::k_char>>{ s.begin(), s.end() }
			);
		}
	};
}
#endif
