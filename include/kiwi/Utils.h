#pragma once
#include <iostream>
#include <string>
#include <memory>
#include "Types.h"

namespace kiwi
{
	template<typename T, typename... Args,
		typename std::enable_if<!std::is_array<T>::value, int>::type = 0
	>
		std::unique_ptr<T> make_unique(Args&&... args)
	{
		return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
	}

	template<typename T,
		typename std::enable_if<std::is_array<T>::value, int>::type = 0
	>
		std::unique_ptr<T> make_unique(size_t size)
	{
		return std::unique_ptr<T>(new typename std::remove_extent<T>::type[size]);
	}

	std::u16string utf8To16(const std::string& str);
	std::u16string utf8To16(const std::string& str, std::vector<size_t>& bytePositions);
	std::string utf16To8(const std::u16string& str);

	inline bool isWebTag(POSTag t)
	{
		return POSTag::w_url <= t && t <= POSTag::w_hashtag;
	}

	POSTag toPOSTag(const std::u16string& tagStr);
	const char* tagToString(POSTag t);
	const kchar_t* tagToKString(POSTag t);

	inline bool isHangulCoda(int chr)
	{
		return 0x11A8 <= chr && chr < (0x11A7 + 28);
	}

	KString normalizeHangul(const std::u16string& hangul);
	std::u16string joinHangul(const KString& hangul);

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
	inline std::vector<std::basic_string<BaseChr>> split(const std::basic_string<BaseChr>& s, BaseChr delim)
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
			float d = 1;
			for (; begin != end; ++begin)
			{
				if ('0' <= *begin && *begin <= '9')
				{
					down = down * 10 + (*begin - '0');
					d /= 10;
				}
				else break;
			}
			up += down * d;
		}
		return up * (sign ? -1 : 1);
	}

	inline std::ostream& operator <<(std::ostream& os, const KString& str)
	{
		return os << utf16To8({ str.begin(), str.end() });
	}

	POSTag identifySpecialChr(kchar_t chr);

	inline bool isspace(char16_t c)
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

	class SpaceSplitIterator
	{
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
			if (o.mBegin == o.mEnd) return mBegin == mEnd;
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