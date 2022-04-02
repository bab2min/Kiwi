#include <cassert>
#include <kiwi/Utils.h>
#include "StrUtils.h"

namespace kiwi
{
	std::u16string utf8To16(const std::string & str)
	{
		return utf8To16(nonstd::to_string_view(str));
	}

	std::u16string utf8To16(const std::string& str, std::vector<size_t>& bytePositions)
	{
		return utf8To16(nonstd::to_string_view(str), bytePositions);
	}

	std::string utf8FromCode(size_t code)
	{
		std::string ret;
		if (code <= 0x7F)
		{
			ret.push_back(code);
		}
		else if (code <= 0x7FF)
		{
			ret.push_back(0xC0 | (code >> 6));
			ret.push_back(0x80 | (code & 0x3F));
		}
		else if (code <= 0xFFFF)
		{
			ret.push_back(0xE0 | (code >> 12));
			ret.push_back(0x80 | ((code >> 6) & 0x3F));
			ret.push_back(0x80 | (code & 0x3F));
		}
		else if (code <= 0x10FFFF)
		{
			ret.push_back(0xF0 | (code >> 18));
			ret.push_back(0x80 | ((code >> 12) & 0x3F));
			ret.push_back(0x80 | ((code >> 6) & 0x3F));
			ret.push_back(0x80 | (code & 0x3F));
		}
		return ret;
	}

	std::string utf16To8(const std::u16string & str)
	{
		return utf16To8(nonstd::to_string_view(str));
	}

	POSTag identifySpecialChr(kchar_t chr)
	{
		if (isSpace(chr)) return POSTag::unknown;
		if (0x2000 <= chr && chr <= 0x200F) return POSTag::unknown;

		if (iswdigit(chr)) return POSTag::sn;
		if (('A' <= chr && chr <= 'Z') ||
			('a' <= chr && chr <= 'z'))  return POSTag::sl;
		if (0xAC00 <= chr && chr < 0xD7A4) return POSTag::max;
		if (isHangulCoda(chr)) return POSTag::max;
		switch (chr)
		{
		case '.':
		case '!':
		case '?':
		case 0x2047:
		case 0x2048:
		case 0x2049:
		case 0x3002:
		case 0xff01:
		case 0xff0e:
		case 0xff1f:
		case 0xff61:
			return POSTag::sf;
		case '-':
		case '~':
		case 0x223c:
		case 0x301c:
		case 0xff5e:
			return POSTag::so;
		case 0x2026:
		case 0x205d:
			return POSTag::se;
		case ',':
		case ';':
		case ':':
		case '/':
		case 0xb7:
		case 0x3001:
		case 0xff0c:
		case 0xff1a:
		case 0xff1b:
		case 0xff64:
			return POSTag::sp;
		case '"':
		case '\'':
		case '(':
		case ')':
		case '<':
		case '>':
		case '[':
		case ']':
		case '{':
		case '}':
		case 0xad:
		case 0x2015:
		case 0x2018:
		case 0x2019:
		case 0x201c:
		case 0x201d:
		case 0x226a:
		case 0x226b:
		case 0x2500:
		case 0x3008:
		case 0x3009:
		case 0x300a:
		case 0x300b:
		case 0x300c:
		case 0x300d:
		case 0x300e:
		case 0x300f:
		case 0x3010:
		case 0x3011:
		case 0x3014:
		case 0x3015:
		case 0x3016:
		case 0x3017:
		case 0x3018:
		case 0x3019:
		case 0x301a:
		case 0x301b:
		case 0xff08:
		case 0xff09:
		case 0xff0d:
		case 0xff1c:
		case 0xff1e:
		case 0xff3b:
		case 0xff3d:
		case 0xff5b:
		case 0xff5d:
		case 0xff5f:
		case 0xff60:
		case 0xff62:
		case 0xff63:
			return POSTag::ss;
		}
		if ((0x2e80 <= chr && chr <= 0x2e99) ||
			(0x2e9b <= chr && chr <= 0x2ef3) ||
			(0x2f00 <= chr && chr <= 0x2fd5) ||
			(0x3005 <= chr && chr <= 0x3007) ||
			(0x3021 <= chr && chr <= 0x3029) ||
			(0x3038 <= chr && chr <= 0x303b) ||
			(0x3400 <= chr && chr <= 0x4db5) ||
			(0x4e00 <= chr && chr <= 0x9fcc) ||
			(0xf900 <= chr && chr <= 0xfa6d) ||
			(0xfa70 <= chr && chr <= 0xfad9)) return POSTag::sh;
		if (0xd800 <= chr && chr <= 0xdfff) return POSTag::sh;
		return POSTag::sw;
	}

	bool isClosingPair(char16_t c)
	{
		switch (c)
		{
		case ')':
		case '>':
		case ']':
		case '}':
		case 0x2019:
		case 0x201d:
		case 0x3009:
		case 0x300b:
		case 0x300d:
		case 0x300f:
		case 0x3011:
		case 0x3015:
		case 0x3017:
		case 0x3019:
		case 0x301b:
		case 0xff09:
		case 0xff1e:
		case 0xff5d:
		case 0xff60:
		case 0xff63:
			return true;
		}
		return false;
	}

	POSTag toPOSTag(const std::u16string& tagStr)
	{
		return toPOSTag(nonstd::to_string_view(tagStr));
	}

	const char* tagToString(POSTag t)
	{
		static const char* tags[] =
		{
			"UN",
			"NNG", "NNP", "NNB",
			"VV", "VA",
			"MAG",
			"NR", "NP",
			"VX",
			"MM", "MAJ",
			"IC",
			"XPN", "XSN", "XSV", "XSA", "XR",
			"VCP", "VCN",
			"SF", "SP", "SS", "SE", "SO", "SW",
			"SL", "SH", "SN",
			"W_URL", "W_EMAIL", "W_MENTION", "W_HASHTAG",
			"JKS", "JKC", "JKG", "JKO", "JKB", "JKV", "JKQ", "JX", "JC",
			"EP", "EF", "EC", "ETN", "ETM",
			"P",
			"@"
		};
		assert(t < POSTag::max);
		return tags[(size_t)t];
	}

	const kchar_t* tagToKString(POSTag t)
	{
		static const kchar_t* tags[] =
		{
			u"UN",
			u"NNG", u"NNP", u"NNB",
			u"VV", u"VA",
			u"MAG",
			u"NR", u"NP",
			u"VX",
			u"MM", u"MAJ",
			u"IC",
			u"XPN", u"XSN", u"XSV", u"XSA", u"XR",
			u"VCP", u"VCN",
			u"SF", u"SP", u"SS", u"SE", u"SO", u"SW",
			u"SL", u"SH", u"SN",
			u"W_URL", u"W_EMAIL", u"W_MENTION", u"W_HASHTAG",
			u"JKS", u"JKC", u"JKG", u"JKO", u"JKB", u"JKV", u"JKQ", u"JX", u"JC",
			u"EP", u"EF", u"EC", u"ETN", u"ETM",
			u"P",
			u"@"
		};
		assert(t < POSTag::max);
		return tags[(size_t)t];
	}
}
