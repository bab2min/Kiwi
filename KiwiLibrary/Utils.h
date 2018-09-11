#pragma once

#include "KForm.h"
#define LEN_ARRAY(p) (sizeof(p)/sizeof(p[0]))

bool isHangulCoda(char16_t chr);
k_string normalizeHangul(const std::u16string& hangul);
std::u16string joinHangul(const k_string& hangul);

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
inline void readFromBinStream(std::istream& is, _Ty& v)
{
	if (!is.read((char*)&v, sizeof(_Ty))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(_Ty).name() + "' failed");
}

template<class _Ty>
inline _Ty readFromBinStream(std::istream& is)
{
	_Ty v;
	readFromBinStream(is, v);
	return v;
}


template<class _Ty1, class _Ty2>
inline void writeToBinStream(std::ostream& os, const std::pair<_Ty1, _Ty2>& v)
{
	writeToBinStream(os, v.first);
	writeToBinStream(os, v.second);
}

template<class _Ty1, class _Ty2>
inline void readFromBinStream(std::istream& is, std::pair<_Ty1, _Ty2>& v)
{
	v.first = readFromBinStream<_Ty1>(is);
	v.second = readFromBinStream<_Ty2>(is);
}

template<>
inline void writeToBinStream<k_string>(std::ostream& os, const k_string& v)
{
	writeToBinStream<uint32_t>(os, v.size());
	if(!os.write((const char*)&v[0], v.size() * sizeof(k_string::value_type))) throw std::ios_base::failure(std::string{ "writing type '" } +typeid(k_string).name() + "' failed");
}

template<>
inline void readFromBinStream<k_string>(std::istream& is, k_string& v)
{
	v.resize(readFromBinStream<uint32_t>(is));
	if (!is.read((char*)&v[0], v.size() * sizeof(k_string::value_type))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(k_string).name() + "' failed");
}

template<>
inline void writeToBinStream<std::u16string>(std::ostream& os, const std::u16string& v)
{
	writeToBinStream<uint32_t>(os, v.size());
	if (!os.write((const char*)&v[0], v.size() * sizeof(char16_t))) throw std::ios_base::failure(std::string{ "writing type '" } +typeid(k_string).name() + "' failed");
}

template<>
inline void readFromBinStream<std::u16string>(std::istream& is, std::u16string& v)
{
	v.resize(readFromBinStream<uint32_t>(is));
	if (!is.read((char*)&v[0], v.size() * sizeof(char16_t))) throw std::ios_base::failure(std::string{ "reading type '" } +typeid(k_string).name() + "' failed");
}


template<class _Ty1, class _Ty2>
inline void writeToBinStream(std::ostream& os, const std::map<_Ty1, _Ty2>& v)
{
	writeToBinStream<uint32_t>(os, v.size());
	for (auto& p : v)
	{
		writeToBinStream(os, p);
	}
}

template<class _Ty1, class _Ty2>
inline void readFromBinStream(std::istream& is, std::map<_Ty1, _Ty2>& v)
{
	size_t len = readFromBinStream<uint32_t>(is);
	v.clear();
	for (size_t i = 0; i < len; ++i)
	{
		v.emplace(readFromBinStream<std::pair<_Ty1, _Ty2>>(is));
	}
}


template<class ChrIterator>
inline float stof(ChrIterator begin, ChrIterator end)
{
	if (begin == end) return 0;
	bool sign = false;
	switch (*begin)
	{
	case '-': 
		sign = true;
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

inline std::u16string utf8_to_utf16(const std::string& utf8_string)
{
	std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> convert;
	auto p = convert.from_bytes(utf8_string);
	return { p.begin(), p.end() };
}
inline std::string utf16_to_utf8(const std::u16string& utf16_string)
{
	std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> convert;
	auto p = reinterpret_cast<const int16_t *>(utf16_string.data());
	return convert.to_bytes(p, p + utf16_string.size());
}
#else
inline std::u16string utf8_to_utf16(const std::string& utf16_string)
{
	std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
	return convert.from_bytes(utf16_string);
}
inline std::string utf16_to_utf8(const std::u16string& utf16_string)
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

uint32_t readVFromBinStream(std::istream& is);
void writeVToBinStream(std::ostream& os, uint32_t v);

int32_t readSVFromBinStream(std::istream& is);
void writeSVToBinStream(std::ostream& os, int32_t v);