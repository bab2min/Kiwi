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
	std::string utf8FromCode(size_t code);
	std::string utf16To8(const std::u16string& str);

	inline bool isWebTag(POSTag t)
	{
		return POSTag::w_url <= t && t <= POSTag::w_hashtag;
	}

	POSTag toPOSTag(const std::u16string& tagStr);
	const char* tagToString(POSTag t);
	const kchar_t* tagToKString(POSTag t);

	inline bool isHangulSyllable(char16_t chr)
	{
		return 0xAC00 <= chr && chr < 0xD7A4;
	}

	inline bool isHangulCoda(char16_t chr)
	{
		return 0x11A8 <= chr && chr < (0x11A7 + 28);
	}

	inline std::ostream& operator <<(std::ostream& os, const KString& str)
	{
		return os << utf16To8({ str.begin(), str.end() });
	}

	inline std::u16string joinHangul(const KString& hangul)
	{
		std::u16string ret;
		ret.reserve(hangul.size());
		for (auto c : hangul)
		{
			if (isHangulCoda(c) && !ret.empty() && isHangulSyllable(ret.back()))
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

	bool isClosingPair(char16_t c);
	POSTag identifySpecialChr(kchar_t chr);

	inline bool isSpace(char16_t c)
	{
		switch (c)
		{
		case u' ':
		case u'\f':
		case u'\n':
		case u'\r':
		case u'\t':
		case u'\v':
		case u'\u2800':
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
			while (mBegin != mEnd && isSpace(*mBegin)) ++mBegin;
			mChunk = mBegin;
			while (mChunk != mEnd && !isSpace(*mChunk)) ++mChunk;
		}

		SpaceSplitIterator& operator++()
		{
			mBegin = mChunk;
			while (mBegin != mEnd && isSpace(*mBegin)) ++mBegin;
			mChunk = mBegin;
			while (mChunk != mEnd && !isSpace(*mChunk)) ++mChunk;
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

