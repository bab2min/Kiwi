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
#define KSTR(x) u##x

namespace kiwi
{
	template<typename T, typename... Args>
	std::unique_ptr<T> make_unique(Args&&... args)
	{
		return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	}

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


	using k_string = std::basic_string<k_char>;
	using k_stringstream = std::basic_stringstream<k_char>;
	using k_vchar = std::vector<k_char>;
	using k_vpcf = std::vector<std::pair<k_vchar, float>>;
}
