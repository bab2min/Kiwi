#include "PatternMatcher.h"
#include "pattern.hpp"

using namespace std;

struct PatternMatcher::MatchData
{
	pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9._%+", 0)>::type emailAccount;
	pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9.", 0)>::type alphaNumDotDash;
	pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9@:%._+~#=", 0)>::type domain;
	pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9()@:%_+.~#?&/=", 0)>::type path;
	pattern::CutSZCharSetParser<PP_GET_64("^# \t\n\r\v\f.,", 0)>::type hashtags;
	pattern::CutSZCharSetParser<PP_GET_64(" \t\n\r\v\f", 0)>::type space;
};

size_t PatternMatcher::testUrl(const char16_t * first, const char16_t * last) const
{
	// Pattern: https?://[-a-zA-Z0-9@:%._+~#=]{1,256}\.[a-zA-Z]{2,6}(:[0-9]+)?\b(/[-a-zA-Z0-9()@:%_+.~#?&/=]*)?

	const char16_t* b = first;

	// https?://
	pattern::CutSZParser<PP_GET_16("http://", 0)>::type http;
	pattern::CutSZParser<PP_GET_16("https://", 0)>::type https;
	auto m = http.progress(first, last);
	if (m.second) b = m.first;
	else
	{
		m = https.progress(first, last);
		if (m.second) b = m.first;
	}

	// [-a-zA-Z0-9@:%._+~#=]{1,256}\.[a-zA-Z0-9()]{1,6}
	int state = 0;
	const char16_t* lastMatched = first;
	if (b == last || !md->domain.test(*b)) return 0;
	++b;
	for (; b != last && md->domain.test(*b); ++b)
	{
		if (*b == '.') state = 1;
		else if (isalpha(*b))
		{
			if (state > 0) ++state;
			if (state >= 3)
			{
				lastMatched = b + 1;
			}
		}
		else state = 0;
	}

	if (lastMatched == first) return 0;
	b = lastMatched;

	// (:[0-9]+)?
	if (b != last && *b == ':')
	{
		++b;
		if (b == last || !isdigit(*b)) return 0;
		++b;
		while (b != last && isdigit(*b)) ++b;
	}

	// \b(/[-a-zA-Z0-9()@:%_+.~#?&/=]*)?
	if (b != last && *b == '/')
	{
		++b;
		while (b != last && md->path.test(*b)) ++b;
	}
	else
	{
		if (b != last && !md->space.test(*b)) return 0;
	}
	
	return b - first;
}

size_t PatternMatcher::testEmail(const char16_t * first, const char16_t * last) const
{
	// Pattern: [A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,6}
	
	const char16_t* b = first;

	// [A-Za-z0-9._%+-]+
	if (b == last || !md->emailAccount.test(*b)) return 0;
	++b;
	while (b != last && md->emailAccount.test(*b)) ++b;
	
	// @
	if (b == last || *b != '@') return 0;
	++b;

	// [A-Za-z0-9.-]+\.[A-Za-z]{2,6}
	int state = 0;
	const char16_t* lastMatched = first;
	if (b == last || !md->alphaNumDotDash.test(*b)) return 0;
	++b;
	for (; b != last && md->alphaNumDotDash.test(*b); ++b)
	{
		if (*b == '.') state = 1;
		else if (isalpha(*b))
		{
			if (state > 0) ++state;
			if (state >= 3) lastMatched = b + 1;
		}
		else state = 0;
	}

	return lastMatched - first;
}

size_t PatternMatcher::testMention(const char16_t* first, const char16_t* last) const
{
	// Pattern: @[A-Za-z0-9._%+-]+
	const char16_t* b = first;

	// @
	if (b == last || *b != '@') return 0;
	++b;

	// [A-Za-z0-9._%+-]+
	if (b == last || !md->emailAccount.test(*b)) return 0;
	++b;
	while (b != last && md->emailAccount.test(*b)) ++b;

	return b - first;
}

size_t PatternMatcher::testHashtag(const char16_t * first, const char16_t * last) const
{
	// Pattern: #[^#\s]+
	const char16_t* b = first;

	if (b == last || *b != '#') return 0;
	++b;

	if (b == last || !md->hashtags.test(*b)) return 0;
	++b;
	while (b != last && md->hashtags.test(*b)) ++b;

	return b - first;
}

PatternMatcher::PatternMatcher()
{
	md = new MatchData;
}

PatternMatcher::~PatternMatcher()
{
	delete md;
	md = nullptr;
}

pair<size_t, kiwi::KPOSTag> PatternMatcher::match(const char16_t * first, const char16_t * last, size_t matchOptions) const
{
	if(!matchOptions) return make_pair(0, kiwi::KPOSTag::UNKNOWN);
	size_t size;
	if ((matchOptions & match_hashtag) && (size = testHashtag(first, last))) return make_pair(size, kiwi::KPOSTag::W_HASHTAG);
	if ((matchOptions & match_email) && (size = testEmail(first, last))) return make_pair(size, kiwi::KPOSTag::W_EMAIL);
	if ((matchOptions & match_mention) && (size = testMention(first, last))) return make_pair(size, kiwi::KPOSTag::W_MENTION);
	if ((matchOptions & match_url) && (size = testUrl(first, last))) return make_pair(size, kiwi::KPOSTag::W_URL);
	return make_pair(0, kiwi::KPOSTag::UNKNOWN);
}
