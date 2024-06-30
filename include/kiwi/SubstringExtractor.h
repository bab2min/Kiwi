#pragma once

#include <vector>
#include <string>

namespace kiwi
{
	std::vector<std::pair<std::u16string, size_t>> extractSubstrings(
		const char16_t* first, 
		const char16_t* last, 
		size_t minCnt, 
		size_t minLength = 2, 
		size_t maxLength = 32,
		bool longestOnly = true,
		char16_t stopChr = 0);
}
