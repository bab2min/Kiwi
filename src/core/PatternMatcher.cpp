#include <kiwi/PatternMatcher.h>
#include "pattern.hpp"

using namespace std;
using namespace kiwi;

class PatternMatcherImpl : public PatternMatcher
{
	struct
	{
		pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9._%+", 0)>::type emailAccount;
		pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9.", 0)>::type alphaNumDotDash;
		pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9@:%._+~#=", 0)>::type domain;
		pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9()@:%_+.~#?&/=", 0)>::type path;
		pattern::CutSZCharSetParser<PP_GET_64("^# \t\n\r\v\f.,", 0)>::type hashtags;
		pattern::CutSZCharSetParser<PP_GET_64(" \t\n\r\v\f", 0)>::type space;
	} md;
	size_t testUrl(const char16_t* first, const char16_t* last) const;
	size_t testEmail(const char16_t* first, const char16_t* last) const;
	size_t testHashtag(const char16_t* first, const char16_t* last) const;
	size_t testMention(const char16_t* first, const char16_t* last) const;
public:
	std::pair<size_t, kiwi::POSTag> match(const char16_t* first, const char16_t* last, Match matchOptions) const override;
};

size_t PatternMatcherImpl::testUrl(const char16_t * first, const char16_t * last) const
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
	// Pattern: @[A-Za-z0-9._%+-]+
	const char16_t* b = first;

	// @
	if (b == last || *b != '@') return 0;
	++b;

	// [A-Za-z0-9._%+-]+
	if (b == last || !md.emailAccount.test(*b)) return 0;
	++b;
	while (b != last && md.emailAccount.test(*b)) ++b;

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

pair<size_t, kiwi::POSTag> PatternMatcherImpl::match(const char16_t * first, const char16_t * last, Match matchOptions) const
{
	if (!matchOptions) return make_pair(0, kiwi::POSTag::unknown);
	size_t size;
	if (!!(matchOptions & Match::hashtag) && (size = testHashtag(first, last))) return make_pair(size, kiwi::POSTag::w_hashtag);
	if (!!(matchOptions & Match::email) && (size = testEmail(first, last))) return make_pair(size, kiwi::POSTag::w_email);
	if (!!(matchOptions & Match::mention) && (size = testMention(first, last))) return make_pair(size, kiwi::POSTag::w_mention);
	if (!!(matchOptions & Match::url) && (size = testUrl(first, last))) return make_pair(size, kiwi::POSTag::w_url);
	return make_pair(0, kiwi::POSTag::unknown);
}

unique_ptr<PatternMatcher> PatternMatcher::create()
{
	return make_unique<PatternMatcherImpl>();
}