#pragma once

#include <vector>
#include <string>
#include "KiwiHeader.h"
#include "KForm.h"

class PatternMatcher
{
	struct MatchData;
	MatchData* md = nullptr;
	size_t testUrl(const char16_t* first, const char16_t* last) const;
	size_t testEmail(const char16_t* first, const char16_t* last) const;
	size_t testHashtag(const char16_t* first, const char16_t* last) const;
	size_t testMention(const char16_t* first, const char16_t* last) const;
public:

	enum { match_url = 1 << 0, 
		match_email = 1 << 1, 
		match_hashtag = 1 << 2,
		match_mention = 1 << 3,
		all = match_url | match_email | match_hashtag | match_mention };

	PatternMatcher();
	~PatternMatcher();
	std::pair<size_t, kiwi::KPOSTag> match(const char16_t* first, const char16_t* last, size_t matchOptions) const;
};
