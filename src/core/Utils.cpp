#include "KiwiHeader.h"
#include "KTrie.h"
#include "KForm.h"
#include "Utils.h"

namespace kiwi
{

	bool isHangulCoda(char16_t chr)
	{
		return 0x11A8 <= chr && chr < (0x11A7 + 28);
	}

	k_string normalizeHangul(const std::u16string& hangul)
	{
		k_string ret;
		ret.reserve(hangul.size() * 1.5);
		for (auto c : hangul)
		{
			if (c == 0xB42C) c = 0xB410; // 됬 -> 됐 으로 강제 교정
			if (0xAC00 <= c && c < 0xD7A4)
			{
				int coda = (c - 0xAC00) % 28;
				ret.push_back(c - coda);
				if (coda) ret.push_back(coda + 0x11A7);
			}
			else
			{
				ret.push_back(c);
			}
		}
		return ret;
	}

	std::u16string joinHangul(const k_string& hangul)
	{
		std::u16string ret;
		ret.reserve(hangul.size());
		for (auto c : hangul)
		{
			if (isHangulCoda(c) && !ret.empty() && 0xAC00 <= ret.back() && ret.back() < 0xD7A4)
			{
				if ((ret.back() - 0xAC00) % 28) ret.push_back(c);
				else ret.back() += c - 0x11A7;
			}
			else
			{
				ret.push_back(c);
			}
		}
		return ret;
	}

	std::u16string utf8_to_utf16(const std::string & str)
	{
		std::u16string ret;
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t code = 0;
			uint8_t byte = *it;
			if ((byte & 0xF8) == 0xF0)
			{
				code = (byte & 0x07) << 18;
				if (++it == str.end()) throw KiwiUnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw KiwiUnicodeException{ "unexpected training byte" };
				code |= (byte & 0x3F) << 12;
				if (++it == str.end()) throw KiwiUnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw KiwiUnicodeException{ "unexpected training byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw KiwiUnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw KiwiUnicodeException{ "unexpected training byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xF0) == 0xE0)
			{
				code = (byte & 0x0F) << 12;
				if (++it == str.end()) throw KiwiUnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw KiwiUnicodeException{ "unexpected training byte" };
				code |= (byte & 0x3F) << 6;
				if (++it == str.end()) throw KiwiUnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw KiwiUnicodeException{ "unexpected training byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0xE0) == 0xC0)
			{
				code = (byte & 0x1F) << 6;
				if (++it == str.end()) throw KiwiUnicodeException{ "unexpected ending" };
				if (((byte = *it) & 0xC0) != 0x80) throw KiwiUnicodeException{ "unexpected training byte" };
				code |= (byte & 0x3F);
			}
			else if ((byte & 0x80) == 0x00)
			{
				code = byte;
			}
			else
			{
				throw KiwiUnicodeException{ "unicode error" };
			}

			if (code < 0x10000)
			{
				ret.push_back(code);
			}
			else if(code < 0x10FFFF)
			{
				ret.push_back(0xD800 | (code >> 10));
				ret.push_back(0xDC00 | (code & 0x3FF));
			}
			else
			{
				throw KiwiUnicodeException{ "unicode error" };
			}
		}
		return ret;
	}

	std::string utf16_to_utf8(const std::u16string & str)
	{
		std::string ret;
		for (auto it = str.begin(); it != str.end(); ++it)
		{
			size_t code = *it;
			if ((code & 0xFC00) == 0xD800)
			{
				if (++it == str.end()) throw KiwiUnicodeException{ "unpaired surrogate" };
				size_t code2 = *it;
				if ((code2 & 0xFC00) != 0xDC00) throw KiwiUnicodeException{ "unpaired surrogate" };
				code = ((code & 0x3FF) << 10) | (code2 & 0x3FF);
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
				throw KiwiUnicodeException{ "unicode error" };
			}
		}

		return ret;
	}

	KPOSTag identifySpecialChr(k_char chr)
	{
		switch (chr)
		{
		case ' ':
		case '\t':
		case '\r':
		case '\n':
		case '\v':
		case '\f':
			return KPOSTag::UNKNOWN;
		}
		if (iswdigit(chr)) return KPOSTag::SN;
		if (('A' <= chr && chr <= 'Z') ||
			('a' <= chr && chr <= 'z'))  return KPOSTag::SL;
		if (0xAC00 <= chr && chr < 0xD7A4) return KPOSTag::MAX;
		if (isHangulCoda(chr)) return KPOSTag::MAX;
		switch (chr)
		{
		case '.':
		case '!':
		case '?':
			return KPOSTag::SF;
		case '-':
		case '~':
		case 0x223c:
			return KPOSTag::SO;
		case 0x2026:
			return KPOSTag::SE;
		case ',':
		case ';':
		case ':':
		case '/':
		case 0xb7:
			return KPOSTag::SP;
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
		case 0xff0d:
			return KPOSTag::SS;
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
			(0xfa70 <= chr && chr <= 0xfad9)) return KPOSTag::SH;
		if (0xd800 <= chr && chr <= 0xdfff) return KPOSTag::SH;
		return KPOSTag::SW;
	}
}
