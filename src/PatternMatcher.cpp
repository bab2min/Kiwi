#include <kiwi/PatternMatcher.h>
#include <kiwi/Utils.h>
#include <kiwi/ScriptType.h>
#include "pattern.hpp"
#include "StrUtils.h"

using namespace std;
using namespace kiwi;

namespace kiwi
{
	class PatternMatcherImpl
	{
		struct
		{
			pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9._%+", 0)>::type emailAccount;
			pattern::CutSZCharSetParser<PP_GET_64("-A-Za-z0-9.", 0)>::type alphaNumDotDash;
			pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9@:%._+~#=", 0)>::type domain;
			pattern::CutSZCharSetParser<PP_GET_64("-a-zA-Z0-9()@:%_+.~#!?&/=", 0)>::type path;
			pattern::CutSZCharSetParser<PP_GET_64("^# \t\n\r\v\f.,()[]<>{}", 0)>::type hashtags;
			pattern::CutSZCharSetParser<PP_GET_64(" \t\n\r\v\f", 0)>::type space;
		} md;

		size_t testUrl(const char16_t* first, const char16_t* last) const;
		size_t testEmail(const char16_t* first, const char16_t* last) const;
		size_t testHashtag(const char16_t* first, const char16_t* last) const;
		size_t testMention(const char16_t* first, const char16_t* last) const;
		size_t testNumeric(const char16_t left, const char16_t* first, const char16_t* last) const;
		size_t testSerial(const char16_t* first, const char16_t* last) const;
		size_t testAbbr(const char16_t* first, const char16_t* last) const;
		size_t testEmoji(const char16_t* first, const char16_t* last) const;

	public:
		std::pair<size_t, POSTag> match(char16_t left, const char16_t* first, const char16_t* last, Match matchOptions) const;
	};

	inline bool isAlpha(char16_t c)
	{
		return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z');
	}

	inline bool isUpperAlpha(char16_t c)
	{
		return ('A' <= c && c <= 'Z');
	}

	inline bool isDigit(char16_t c)
	{
		return ('0' <= c && c <= '9') || (u'\uff10' <= c && c <= u'\uff19');
	}
}

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
		else if (isAlpha(*b))
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
		if (b == last || !isDigit(*b)) return 0;
		++b;
		while (b != last && isDigit(*b)) ++b;
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
		else if (isAlpha(*b))
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
	if (b == last || !isAlpha(*b)) return 0;
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

size_t PatternMatcherImpl::testNumeric(char16_t left, const char16_t* first, const char16_t* last) const
{
	// Pattern: [0-9]+(,[0-9]{3})*(\.[0-9]+)?
	const char16_t* b = first;
	bool hasComma = false;

	if (b == last || !isDigit(*b)) return 0;
	
	while (b != last && isDigit(*b)) ++b;

	while (b != last && *b == ',')
	{
		++b;
		if (b + 2 >= last || !isDigit(b[0]) || !isDigit(b[1]) || !isDigit(b[2])) return b - 1 - first;
		b += 3;
		hasComma = true;
	}
	
	if (b == last || isSpace(*b) || isHangulSyllable(*b)) return b - first;

	if (*b == '.')
	{
		++b;
		if (!hasComma && !md.alphaNumDotDash.test(left) && (b == last || !md.alphaNumDotDash.test(*b))) return b - first;
		if (b == last || !isDigit(*b)) return b - 1 - first;
		while (b != last && isDigit(*b)) ++b;
	}
	
	if(b == last || (*b != '.'))
	{
		return b - first;
	}
	return 0;
}

size_t PatternMatcherImpl::testSerial(const char16_t* first, const char16_t* last) const
{
	// Pattern: [0-9]+([:.-/] ?)[0-9]+(\1[0-9]+)*\1?
	const char16_t* b = first;

	if (b == last || !isDigit(*b)) return 0;

	while (b != last && isDigit(*b)) ++b;

	if (b == last) return 0;

	char16_t sep = 0;
	if (*b == ':' || *b == '.' || *b == '-' || *b == '/') sep = *b;
	else return 0;
	++b;
	if (b != last && *b == ' ') ++b;
	if (b == last || !isDigit(*b)) return 0;
	++b;
	while (b != last && isDigit(*b)) ++b;

	if (sep == '.' && (b == last || *b != sep)) return 0; // reject [0-9]+\.[0-9]+ pattern

	while (b != last && *b == sep)
	{
		++b;
		if (b != last && *b == ' ') ++b;
		if (b == last || !isDigit(*b))
		{
			if (b[-1] == ' ') --b;
			return b - first;
		}
		++b;
		while (b != last && isDigit(*b)) ++b;
	}
	if (b[-1] == ' ') --b;
	return b - first;
}

size_t PatternMatcherImpl::testAbbr(const char16_t* first, const char16_t* last) const
{
	const char16_t* b = first;

	if (b == last || !isAlpha(*b)) return 0;

	size_t l = 0;
	while (b != last && isAlpha(*b)) ++b, ++l;

	if (b == last) return 0;

	if (*b == '.') ++b;
	else return 0;
	
	if (b != last && *b == ' ')
	{
		if (l > (isUpperAlpha(*first) ? 5 : 3)) return 0; // reject too long patterns for abbreviation
		return b - first;
	}
	else
	{
		if (l > 5) return 0; // reject too long patterns for abbreviation
	}
	
	while (b != last && isAlpha(*b))
	{
		l = 0;
		while (b != last && isAlpha(*b)) ++b, ++l;
		if (l > 5) return 0;
		
		if (b != last && *b == '.') ++b;
		else return b - first;
	}
	if (b[-1] == ' ') --b;
	return b - first;
}

size_t PatternMatcherImpl::testEmoji(const char16_t* first, const char16_t* last) const
{
	const char16_t* b = first;
	while (b + 1 < last)
	{
		char32_t c0 = 0, c1 = 0;
		const char16_t* b1 = b;
		if (isHighSurrogate(*b1))
		{
			c0 = mergeSurrogate(b1[0], b1[1]);
			b1 += 2;
		}
		else
		{
			c0 = *b1++;
		}

		const char16_t* b2 = b1;
		if (b2 < last)
		{
			if (isHighSurrogate(*b2) && b2 + 1 < last)
			{
				c1 = mergeSurrogate(b2[0], b2[1]);
				b2 += 2;
			}
			else
			{
				c1 = *b2++;
			}
		}

		auto r = isEmoji(c0, c1);
		if (r == 1)
		{
			b = b1;
		}
		else if (r == 2)
		{
			b = b2;
		}
		else
		{
			break;
		}

		if (b == last) return b - first;
		if (0xfe00 <= *b && *b <= 0xfe0f) // variation selectors
		{
			++b;
			if (b == last) return b - first;
		}
		else if (b + 1 < last && isHighSurrogate(b[0]))
		{
			c1 = mergeSurrogate(b[0], b[1]);
			if (0x1f3fb <= c1 && c1 <= 0x1f3ff) // skin color modifier
			{
				b += 2;
				if (b == last) return b - first;
			}
		}

		if (*b == 0x200d) // zero width joiner
		{
			++b;
			continue;
		}
		break;
	}
	return b - first;
}

pair<size_t, POSTag> PatternMatcherImpl::match(char16_t left, const char16_t * first, const char16_t * last, Match matchOptions) const
{
	size_t size;
	if (!!(matchOptions & Match::serial) && (size = testSerial(first, last))) return make_pair(size, POSTag::w_serial);
	if ((size = testNumeric(left, first, last))) return make_pair(size, POSTag::sn);
	if (!!(matchOptions & Match::hashtag) && (size = testHashtag(first, last))) return make_pair(size, POSTag::w_hashtag);
	if (!!(matchOptions & Match::email) && (size = testEmail(first, last))) return make_pair(size, POSTag::w_email);
	if (!!(matchOptions & Match::mention) && (size = testMention(first, last))) return make_pair(size, POSTag::w_mention);
	if (!!(matchOptions & Match::url) && (size = testUrl(first, last))) return make_pair(size, POSTag::w_url);
	if (!!(matchOptions & Match::emoji) && (size = testEmoji(first, last))) return make_pair(size, POSTag::w_emoji);
	if ((size = testAbbr(first, last))) return make_pair(size, POSTag::sl);
	return make_pair(0, POSTag::unknown);
}

pair<size_t, POSTag> kiwi::matchPattern(char16_t left, const char16_t* first, const char16_t* last, Match matchOptions)
{
	static PatternMatcherImpl matcher;
	return matcher.match(left, first, last, matchOptions);
}
