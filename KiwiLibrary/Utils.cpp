#include "stdafx.h"
#include "KTrie.h"
#include "KForm.h"
#include "Utils.h"

k_string normalizeHangul(std::u16string hangul)
{
	k_string ret;
	ret.reserve(hangul.size() * 1.5);
	for (auto c : hangul)
	{
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

std::u16string joinHangul(k_string hangul)
{
	std::u16string ret;
	ret.reserve(hangul.size());
	for (auto c : hangul)
	{
		if (0x11A8 <= c && c < (0x11A7 + 28) && 0xAC00 <= ret.back() && ret.back() < 0xD7A4)
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
	if (0x11A8 <= chr && chr < 0x11A7 + 28) return KPOSTag::MAX;
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