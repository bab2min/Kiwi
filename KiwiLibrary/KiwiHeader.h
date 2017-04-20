#pragma once
#include <stdio.h>
#include <cstring>
#include <cassert>
#include <cmath>

#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <bitset>

#include <algorithm>
#include <functional>
#include <numeric>

#include <string>
#include <sstream>
#include <iostream>

#include <memory>
#include <locale>
#include <codecvt>

#include <chrono>

using namespace std;

#define TRIE_ALLOC_ARRAY
#define DIVIDE_BOUND 6
#define MIN_CANDIDATE 3
#define USE_DIST_MAP
#define LOAD_TXT

#ifdef _WIN32
typedef wchar_t k_wchar;
typedef wstring k_wstring;
#define KSTR(x) L##x
#else
typedef char16_t k_wchar;
typedef u16string k_wstring;
#define KSTR(x) u##x
inline int fopen_s(FILE** f, const char* p, const char* m)
{
	*f = fopen(p, m);
	return !*f;
}
#endif