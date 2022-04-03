#include <kiwi/PatternMatcher.h>
#include <kiwi/Utils.h>
#include "pattern.hpp"

using namespace std;
using namespace kiwi;

class PatternMatcherImpl
{
	struct
	{
		pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9._%+", 0)>::type emailAccount;
		pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9.", 0)>::type alphaNumDotDash;
		pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9@:%._+~#=", 0)>::type domain;
		pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9()@:%_+.~#!?&/=", 0)>::type path;
		pattern::CutSZCharSetParser<PP_GET_64("^# \t\n\r\v\f.,", 0)>::type hashtags;
		pattern::CutSZCharSetParser<PP_GET_64(" \t\n\r\v\f", 0)>::type space;
	} md;

	size_t testUrl(const char16_t* first, const char16_t* last) const;
	size_t testEmail(const char16_t* first, const char16_t* last) const;
	size_t testHashtag(const char16_t* first, const char16_t* last) const;
	size_t testMention(const char16_t* first, const char16_t* last) const;
	size_t testNumeric(const char16_t* first, const char16_t* last) const;

public:
	std::pair<size_t, POSTag> match(const char16_t* first, const char16_t* last, Match matchOptions) const;
};

size_t PatternMatcherImpl::testUrl(const char16_t * first, const char16_t * last) const
{
	// Pattern: https?://[-a-zA-Z0-9@:%._+~#=]{1,256}\.[a-zA-Z]{2,6}(:[0-9]+)?\b(/[-a-zA-Z0-9()@:%_+.~#!?&/=]*)?

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

	if (!m.second) return 0;

	// [-a-zA-Z0-9@:%._+~#=]{1,256}\.[a-zA-Z0-9()]{1,6}
	int state = 0;
	const char16_t* lastMatched = first;
	if (b == last || !md.domain.test(*b)) return 0;
	++b;
	for (; b != last && md.domain.test(*b); ++b)
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
		while (b != last && md.path.test(*b)) ++b;
	}
	else
	{
		if (b != last && !md.space.test(*b)) return 0;
	}
	
	if (b[-1] == u'.' || b[-1] == u':') --b;
	return b - first;
}

size_t PatternMatcherImpl::testEmail(const char16_t * first, const char16_t * last) const
{
	// Pattern: [A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,6}
	
	const char16_t* b = first;

	// [A-Za-z0-9._%+-]+
	if (b == last || !md.emailAccount.test(*b)) return 0;
	++b;
	while (b != last && md.emailAccount.test(*b)) ++b;
	
	// @
	if (b == last || *b != '@') return 0;
	++b;

	// [A-Za-z0-9.-]+\.[A-Za-z]{2,6}
	int state = 0;
	const char16_t* lastMatched = first;
	if (b == last || !md.alphaNumDotDash.test(*b)) return 0;
	++b;
	for (; b != last && md.alphaNumDotDash.test(*b); ++b)
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

size_t PatternMatcherImpl::testMention(const char16_t* first, const char16_t* last) const
{
	// Pattern: @[A-Za-z][A-Za-z0-9._%+-]+
	const char16_t* b = first;

	// @
	if (b == last || *b != '@') return 0;
	++b;

	// [A-Za-z][A-Za-z0-9._%+-]+
	if (b == last || !isalpha(*b)) return 0;
	++b;
	while (b != last && md.emailAccount.test(*b)) ++b;
	if (b[-1] == u'.' || b[-1] == u'%' || b[-1] == u'+' || b[-1] == u'-') --b;
	if (b - first <= 3) return 0;
	return b - first;
}

size_t PatternMatcherImpl::testHashtag(const char16_t * first, const char16_t * last) const
{
	// Pattern: #[^#\s]+
	const char16_t* b = first;

	if (b == last || *b != '#') return 0;
	++b;

	if (b == last || !md.hashtags.test(*b)) return 0;
	++b;
	while (b != last && md.hashtags.test(*b)) ++b;

	return b - first;
}

inline bool isDigit(char16_t c)
{
	return ('0' <= c && c <= '9') || (u'\uff10' <= c && c <= u'\uff19');
}

size_t PatternMatcherImpl::testNumeric(const char16_t* first, const char16_t* last) const
{
	// Pattern: [0-9]+(,[0-9]{3})*(\.[0-9]+)?
	const char16_t* b = first;

	if (b == last || !isDigit(*b)) return 0;
	
	while (b != last && isDigit(*b)) ++b;

	while (b != last && *b == ',')
	{
		++b;
		if (b + 2 >= last || !isDigit(b[0]) || !isDigit(b[1]) || !isDigit(b[2])) return b - 1 - first;
		b += 3;
	}
	
	if (b == last || isSpace(*b) || isHangulSyllable(*b)) return b - first;

	if (*b == '.')
	{
		++b;
		if (b == last || !isDigit(*b)) return b - 1 - first;
		while (b != last && isDigit(*b)) ++b;
	}
	
	if(b == last || isSpace(*b) || isHangulSyllable(*b))
	{
		return b - first;
	}
	return 0;
}


pair<size_t, POSTag> PatternMatcherImpl::match(const char16_t * first, const char16_t * last, Match matchOptions) const
{
	size_t size;
	if ((size = testNumeric(first, last))) return make_pair(size, POSTag::sn);
	if (!!(matchOptions & Match::hashtag) && (size = testHashtag(first, last))) return make_pair(size, POSTag::w_hashtag);
	if (!!(matchOptions & Match::email) && (size = testEmail(first, last))) return make_pair(size, POSTag::w_email);
	if (!!(matchOptions & Match::mention) && (size = testMention(first, last))) return make_pair(size, POSTag::w_mention);
	if (!!(matchOptions & Match::url) && (size = testUrl(first, last))) return make_pair(size, POSTag::w_url);
	return make_pair(0, POSTag::unknown);
}

pair<size_t, POSTag> kiwi::matchPattern(const char16_t* first, const char16_t* last, Match matchOptions)
{
	static PatternMatcherImpl matcher;
	return matcher.match(first, last, matchOptions);
}
