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
		all = url | email | hashtag | mention,
	};

	class PatternMatcher
	{
	public:
		static const PatternMatcher* getInst();
		virtual ~PatternMatcher() {}
		virtual std::pair<size_t, kiwi::POSTag> match(const char16_t* first, const char16_t* last, Match matchOptions) const = 0;
	};
}

KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::Match);
