#include "KiwiHeader.h"
#include "KTrie.h"
#include "KForm.h"
#include "Utils.h"

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
		if (isHangulCoda(c) && 0xAC00 <= ret.back() && ret.back() < 0xD7A4)
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

uint32_t readVFromBinStream(std::istream & is)
{
	static uint32_t vSize[] = { 0, 0x80, 0x4080, 0x204080, 0x10204080 };
	char c;
	uint32_t v = 0;
	size_t i;
	for (i = 0; (c = readFromBinStream<uint8_t>(is)) & 0x80; ++i)
	{
		v |= (c & 0x7F) << (i * 7);
	}
	v |= c << (i * 7);
	return v + vSize[i];
}

void writeVToBinStream(std::ostream & os, uint32_t v)
{
	static uint32_t vSize[] = {0, 0x80, 0x4080, 0x204080, 0x10204080};
	size_t i;
	for (i = 1; i <= 4; ++i)
	{
		if (v < vSize[i]) break;
	}
	v -= vSize[i - 1];
	for (size_t n = 0; n < i; ++n)
	{
		uint8_t c = (v & 0x7F) | (n + 1 < i ? 0x80 : 0);
		writeToBinStream(os, c);
		v >>= 7;
	}
}


int32_t readSVFromBinStream(std::istream & is)
{
	static int32_t vSize[] = { 0x40, 0x2000, 0x100000, 0x8000000 };
	char c;
	uint32_t v = 0;
	size_t i;
	for (i = 0; (c = readFromBinStream<uint8_t>(is)) & 0x80; ++i)
	{
		v |= (c & 0x7F) << (i * 7);
	}
	v |= c << (i * 7);
	if(i >= 4) return (int32_t)v;
	return v - (v >= vSize[i] ? (1 << ((i + 1) * 7)) : 0);
}

void writeSVToBinStream(std::ostream & os, int32_t v)
{
	static int32_t vSize[] = { 0, 0x40, 0x2000, 0x100000, 0x8000000 };
	size_t i;
	for (i = 1; i <= 4; ++i)
	{
		if (-vSize[i] <= v && v < vSize[i]) break;
	}
	uint32_t u;
	if (i >= 5) u = (uint32_t)v;
	else u = v + (v < 0 ? (1 << (i * 7)) : 0);
	for (size_t n = 0; n < i; ++n)
	{
		uint8_t c = (u & 0x7F) | (n + 1 < i ? 0x80 : 0);
		writeToBinStream(os, c);
		u >>= 7;
	}
}
