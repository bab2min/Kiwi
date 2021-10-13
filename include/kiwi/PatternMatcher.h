#pragma once

#include <vector>
#include <string>
#include <memory>
#include "Types.h"

namespace kiwi
{
	enum class Match : size_t
	{
		none = 0,
		url = 1 << 0,
		email = 1 << 1,
		hashtag = 1 << 2,
		mention = 1 << 3,
		normalizeCoda = 1 << 16,
		all = url | email | hashtag | mention,
		allWithNormalizing = all | normalizeCoda,
	};

	std::pair<size_t, kiwi::POSTag> matchPattern(const char16_t* first, const char16_t* last, Match matchOptions);
}

KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::Match);
