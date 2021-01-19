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

#ifdef USE_MIMALLOC
#include <mimalloc.h>
#endif

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

#ifdef USE_MIMALLOC
	template<typename _Ty>
	using mvector = std::vector<_Ty, mi_stl_allocator<_Ty>>;

	template<typename _K, typename _V>
	using munordered_map = std::unordered_map<_K, _V, std::hash<_K>, std::equal_to<_K>, mi_stl_allocator<std::pair<const _K, _V>>>;

	using k_string = std::basic_string<k_char, std::char_traits<k_char>, mi_stl_allocator<k_char>>;
	using k_stringstream = std::basic_stringstream<k_char, std::char_traits<k_char>, mi_stl_allocator<k_char>>;
	using k_vchar = mvector<k_char>;
	using k_vpcf = mvector<std::pair<k_vchar, float>>;
#else
	template<typename _Ty>
	using mvector = std::vector<_Ty>;

	template<typename _K, typename _V>
	using munordered_map = std::unordered_map<_K, _V>;

	using k_string = std::basic_string<k_char>;
	using k_stringstream = std::basic_stringstream<k_char>;
	using k_vchar = mvector<k_char>;
	using k_vpcf = mvector<std::pair<k_vchar, float>>;
#endif
}

#ifdef USE_MIMALLOC
namespace std
{
	template<>
	struct hash<kiwi::k_string>
	{
		size_t operator()(const kiwi::k_string& s) const
		{
			return hash<basic_string<kiwi::k_char>>{}({ s.begin(), s.end() });
		}
	};

}
#endif