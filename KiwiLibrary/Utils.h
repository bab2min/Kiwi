#pragma once

#include "KForm.h"
#define LEN_ARRAY(p) (sizeof(p)/sizeof(p[0]))

k_string normalizeHangul(std::u16string hangul);
std::u16string joinHangul(k_string hangul);

template<class BaseChr, class OutIterator>
void split(const std::basic_string<BaseChr>& s, BaseChr delim, OutIterator result) {
	std::basic_stringstream<BaseChr> ss;
	ss.str(s);
	std::basic_string<BaseChr> item;
	while (std::getline(ss, item, delim)) {
		*(result++) = item;
	}
}

template<class BaseChr>
inline std::vector<std::basic_string<BaseChr>> split(const std::basic_string<BaseChr>&s, BaseChr delim) {
	std::vector<std::basic_string<BaseChr>> elems;
	split(s, delim, std::back_inserter(elems));
	return elems;
}

template<class _Ty>
inline void writeToBinStream(std::ostream& os, const _Ty& v)
{
	if (!os.write((const char*)&v, sizeof(_Ty))) throw std::ios_base::failure(std::string{ "writing type '" } + typeid(_Ty).name() + "' failed");
}

template<class _Ty>
inline _Ty readFromBinStream(std::istream& is)
{
	_Ty v;
	if (!is.read((char*)&v, sizeof(_Ty))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(_Ty).name() + "' failed");
	return v;
}

template<class _Ty>
inline void readFromBinStream(std::istream& is, _Ty& v)
{
	if (!is.read((char*)&v, sizeof(_Ty))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(_Ty).name() + "' failed");
}

template<class _Ty1, class _Ty2>
inline void writeToBinStream(std::ostream& os, const std::pair<_Ty1, _Ty2>& v)
{
	writeToBinStream(os, v.first);
	writeToBinStream(os, v.second);
}

template<class _Ty1, class _Ty2>
inline std::pair<_Ty1, _Ty2> readFromBinStream(std::istream& is)
{
	return std::make_pair(readFromBinStream<_Ty1>(is), readFromBinStream<_Ty2>(is));
}


template<>
inline void writeToBinStream<k_string>(std::ostream& os, const k_string& v)
{
	writeToBinStream<uint32_t>(os, v.size());
	if(!os.write((const char*)&v[0], v.size() * sizeof(k_string::value_type))) throw std::ios_base::failure(std::string{ "writing type '" } +typeid(k_string).name() + "' failed");
}

template<>
inline k_string readFromBinStream<k_string>(std::istream& is)
{
	k_string v; 
	v.resize(readFromBinStream<uint32_t>(is));
	if (!is.read((char*)&v[0], v.size() * sizeof(k_string::value_type))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(k_string).name() + "' failed");
	return v;
}

template<class ChrIterator>
inline float stof(ChrIterator begin, ChrIterator end)
{
	if (begin == end) return 0;
	bool sign = false;
	switch (*begin)
	{
	case '-': 
		sign = false;
	case '+':
		++begin;
		break;
	}
	double up = 0, down = 0;
	for (; begin != end; ++begin)
	{
		if ('0' <= *begin && *begin <= '9') up = up * 10 + (*begin - '0');
		else break;
	}
	if (begin != end && *begin == '.')
	{
		++begin;
		int cnt = 0;
		for (; begin != end; ++begin)
		{
			if ('0' <= *begin && *begin <= '9')
			{
				down = down * 10 + (*begin - '0');
				++cnt;
			}
			else break;
		}
		up += down * pow(10, -cnt);
	}
	return up * (sign ? -1 : 1);
}

#if _MSC_VER >= 1900

inline std::u16string utf8_to_utf16(std::string utf8_string)
{
	std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> convert;
	auto p = convert.from_bytes(utf8_string);
	return { p.begin(), p.end() };
}
inline std::string utf16_to_utf8(std::u16string utf16_string)
{
	std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> convert;
	auto p = reinterpret_cast<const int16_t *>(utf16_string.data());
	return convert.to_bytes(p, p + utf16_string.size());
}
#else
inline std::u16string utf8_to_utf16(std::string utf18_string)
{
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
	return convert.from_bytes(utf16_string);
}
inline std::string utf16_to_utf8(std::u16string utf16_string)
{
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
	return convert.to_bytes(utf16_string);
}
#endif

inline std::ostream& operator <<(std::ostream& os, const k_string& str)
{
	return os << utf16_to_utf8({ str.begin(), str.end() });
}

KPOSTag identifySpecialChr(k_char chr);