#include <cassert>
#include <kiwi/Utils.h>

namespace kiwi
{
	std::u16string utf8To16(const std::string & str)
	{
		std::u16string ret;
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t code = 0;
			uint8_t byte = *it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (byte & 0x07) << 18;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (byte & 0x0F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (byte & 0x1F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0x80) == 0x00)
			{
				code = byte;
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}

			if (code < 0x10000)
			{
				ret.push_back(code);
			}
			else if(code < 0x10FFFF)
			{
				code -= 0x10000;
				ret.push_back(0xD800 | (code >> 10));
				ret.push_back(0xDC00 | (code & 0x3FF));
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}
		return ret;
	}

	std::u16string utf8To16(const std::string& str, std::vector<size_t>& bytePositions)
	{
		std::u16string ret;
		bytePositions.clear();
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t pos = (size_t)(it - str.begin());
			size_t code = 0;
			uint8_t byte = *it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (byte & 0x07) << 18;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (byte & 0x0F) << 12;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (byte & 0x1F) << 6;
				if (++it == str.end()) throw UnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw UnicodeException{ "unexpected trailing byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0x80) == 0x00)
			{
				code = byte;
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}

			if (code < 0x10000)
			{
				ret.push_back(code);
				bytePositions.emplace_back(pos);
			}
			else if (code < 0x10FFFF)
			{
				code -= 0x10000;
				ret.push_back(0xD800 | (code >> 10));
				ret.push_back(0xDC00 | (code & 0x3FF));
				bytePositions.emplace_back(pos);
				bytePositions.emplace_back(pos);
			}
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}
		return ret;
	}

	std::string utf16To8(const std::u16string & str)
	{
		std::string ret;
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t code = *it;
			if ((code & 0xFC00) == 0xD800)
			{
				if (++it == str.end()) throw UnicodeException{ "unpaired surrogate" };
				size_t code2 = *it;
				if ((code2 & 0xFC00) != 0xDC00) throw UnicodeException{ "unpaired surrogate" };
				code = ((code & 0x3FF) << 10) | (code2 & 0x3FF);
				code += 0x10000;
			}

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
			else
			{
				throw UnicodeException{ "unicode error" };
			}
		}

		return ret;
	}

	POSTag identifySpecialChr(kchar_t chr)
	{
		switch (chr)
		{
		case ' ':
		case '\t':
		case '\r':
		case '\n':
		case '\v':
		case '\f':
			return POSTag::unknown;
		}
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
		if (tagStr == u"NNG") return POSTag::nng;
		if (tagStr == u"NNP") return POSTag::nnp;
		if (tagStr == u"NNB") return POSTag::nnb;
		if (tagStr == u"NR") return POSTag::nr;
		if (tagStr == u"NP") return POSTag::np;
		if (tagStr == u"VV") return POSTag::vv;
		if (tagStr == u"VA") return POSTag::va;
		if (tagStr == u"VX") return POSTag::vx;
		if (tagStr == u"VCP") return POSTag::vcp;
		if (tagStr == u"VCN") return POSTag::vcn;
		if (tagStr == u"MM") return POSTag::mm;
		if (tagStr == u"MAG") return POSTag::mag;
		if (tagStr == u"MAJ") return POSTag::maj;
		if (tagStr == u"IC") return POSTag::ic;
		if (tagStr == u"JKS") return POSTag::jks;
		if (tagStr == u"JKC") return POSTag::jkc;
		if (tagStr == u"JKG") return POSTag::jkg;
		if (tagStr == u"JKO") return POSTag::jko;
		if (tagStr == u"JKB") return POSTag::jkb;
		if (tagStr == u"JKV") return POSTag::jkv;
		if (tagStr == u"JKQ") return POSTag::jkq;
		if (tagStr == u"JX") return POSTag::jx;
		if (tagStr == u"JC") return POSTag::jc;
		if (tagStr == u"EP") return POSTag::ep;
		if (tagStr == u"EF") return POSTag::ef;
		if (tagStr == u"EC") return POSTag::ec;
		if (tagStr == u"ETN") return POSTag::etn;
		if (tagStr == u"ETM") return POSTag::etm;
		if (tagStr == u"XPN") return POSTag::xpn;
		if (tagStr == u"XSN") return POSTag::xsn;
		if (tagStr == u"XSV") return POSTag::xsv;
		if (tagStr == u"XSA") return POSTag::xsa;
		if (tagStr == u"XR") return POSTag::xr;
		if (tagStr == u"SF") return POSTag::sf;
		if (tagStr == u"SP") return POSTag::sp;
		if (tagStr == u"SS") return POSTag::ss;
		if (tagStr == u"SE") return POSTag::se;
		if (tagStr == u"SO") return POSTag::so;
		if (tagStr == u"SW") return POSTag::sw;
		if (tagStr == u"NF") return POSTag::unknown;
		if (tagStr == u"NV") return POSTag::unknown;
		if (tagStr == u"NA") return POSTag::unknown;
		if (tagStr == u"SL") return POSTag::sl;
		if (tagStr == u"SH") return POSTag::sh;
		if (tagStr == u"SN") return POSTag::sn;
		if (tagStr == u"V") return POSTag::v;
		if (tagStr == u"A") return POSTag::v;
		if (tagStr == u"^") return POSTag::unknown;
		if (tagStr == u"W_URL") return POSTag::w_url;
		if (tagStr == u"W_EMAIL") return POSTag::w_email;
		if (tagStr == u"W_HASHTAG") return POSTag::w_hashtag;
		if (tagStr == u"W_MENTION") return POSTag::w_mention;
		//assert(0);
		return POSTag::max;
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
			"V",
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
			u"V",
			u"@"
		};
		assert(t < POSTag::max);
		return tags[(size_t)t];
	}
}
