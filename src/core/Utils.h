#pragma once
#include "KForm.h"

namespace kiwi
{
	bool isHangulCoda(char16_t chr);
	k_string normalizeHangul(const std::u16string& hangul);
	std::u16string joinHangul(const k_string& hangul);

	template<class BaseChr, class OutIterator>
	void split(const std::basic_string<BaseChr>& s, BaseChr delim, OutIterator result) 
	{
		size_t p = 0;
		while (1)
		{
			size_t t = s.find(delim, p);
			if (t == s.npos)
			{
				*(result++) = s.substr(p);
				break;
			}
			else
			{
				*(result++) = s.substr(p, t - p);
				p = t + 1;
			}
		}
	}

	template<class BaseChr>
	inline std::vector<std::basic_string<BaseChr>> split(const std::basic_string<BaseChr>&s, BaseChr delim) 
	{
		std::vector<std::basic_string<BaseChr>> elems;
		split(s, delim, std::back_inserter(elems));
		return elems;
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
		static std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> convert;
		auto p = convert.from_bytes(utf8_string);
		return { p.begin(), p.end() };
	}
	inline std::string utf16_to_utf8(const std::u16string& utf16_string)
	{
		static std::wstring_convert<std::codecvt_utf8_utf16<int16_t>, int16_t> convert;
		auto p = reinterpret_cast<const int16_t *>(utf16_string.data());
		return convert.to_bytes(p, p + utf16_string.size());
	}
#else
	inline std::u16string utf8_to_utf16(const std::string& utf16_string)
	{
		static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
		return convert.from_bytes(utf16_string);
	}
	inline std::string utf16_to_utf8(const std::u16string& utf16_string)
	{
		static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
		return convert.to_bytes(utf16_string);
	}
#endif

	inline std::ostream& operator <<(std::ostream& os, const k_string& str)
	{
		return os << utf16_to_utf8({ str.begin(), str.end() });
	}

	KPOSTag identifySpecialChr(k_char chr);

	class SpaceSplitIterator
	{
		static bool isspace(char16_t c)
		{
			switch (c)
			{
			case u' ':
			case u'\f':
			case u'\n':
			case u'\r':
			case u'\t':
			case u'\v':
				return true;
			}
			return false;
		}

		std::u16string::const_iterator mBegin, mChunk, mEnd;
	public:
		SpaceSplitIterator(const std::u16string::const_iterator& _begin = {}, const std::u16string::const_iterator& _end = {})
			: mBegin(_begin), mEnd(_end)
		{
			while (mBegin != mEnd && isspace(*mBegin)) ++mBegin;
			mChunk = mBegin;
			while (mChunk != mEnd && !isspace(*mChunk)) ++mChunk;
		}

		SpaceSplitIterator& operator++()
		{
			mBegin = mChunk;
			while (mBegin != mEnd && isspace(*mBegin)) ++mBegin;
			mChunk = mBegin;
			while (mChunk != mEnd && !isspace(*mChunk)) ++mChunk;
			return *this;
		}

		bool operator==(const SpaceSplitIterator& o) const
		{
			if (mBegin == mEnd && o.mBegin == o.mEnd) return true;
			return mBegin == o.mBegin;
		}

		bool operator!=(const SpaceSplitIterator& o) const
		{
			return !operator==(o);
		}

		std::u16string operator*() const
		{
			return { mBegin, mChunk };
		}

		std::u16string::const_iterator strBegin() const { return mBegin; }
		std::u16string::const_iterator strEnd() const { return mChunk; }
		size_t strSize() const { return distance(mBegin, mChunk); }
	};

}