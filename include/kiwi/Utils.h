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
	std::string utf8FromCode(char32_t code);
	size_t utf8FromCode(std::string& ret, char32_t code);
	std::string utf16To8(const std::u16string& str);

	inline bool isWebTag(POSTag t)
	{
		return POSTag::w_url <= t && t <= POSTag::w_hashtag;
	}

	POSTag toPOSTag(const std::u16string& tagStr);
	const char* tagToString(POSTag t);
	const kchar_t* tagToKString(POSTag t);
	
	const char* tagRToString(char16_t form, POSTag t);
	const kchar_t* tagRToKString(char16_t form, POSTag t);

	inline bool isHangulSyllable(char16_t chr)
	{
		return 0xAC00 <= chr && chr < 0xD7A4;
	}

	inline bool isHangulCoda(char16_t chr)
	{
		return 0x11A8 <= chr && chr < (0x11A7 + 28);
	}

	inline bool isOldHangulOnset(char16_t chr)
	{
		return (0x1100 <= chr && chr < 0x1160) || (0xA960 <= chr && chr < 0xA980);
	}

	inline bool isOldHangulVowel(char16_t chr)
	{
		return (0x1160 <= chr && chr < 0x11A8) || (0xD7B0 <= chr && chr < 0xD7CB);
	}

	inline bool isOldHangulCoda(char16_t chr)
	{
		return (0x11A8 <= chr && chr < 0x1200) || (0xD7CB <= chr && chr < 0xD800);
	}

	inline bool isOldHangulToneMark(char16_t chr)
	{
		return 0x302E <= chr && chr < 0x3030;
	}

	inline std::ostream& operator <<(std::ostream& os, const KString& str)
	{
		return os << utf16To8({ str.begin(), str.end() });
	}

	template<class It>
	inline std::u16string joinHangul(It first, It last)
	{
		std::u16string ret;
		ret.reserve(std::distance(first, last));
		for (; first != last; ++first)
		{
			auto c = *first;
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

	inline std::u16string joinHangul(const KString& hangul)
	{
		return joinHangul(hangul.begin(), hangul.end());
	}

	POSTag identifySpecialChr(char32_t chr);
	size_t getSSType(char16_t c);

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

	template<class Ty, size_t size>
	class HiddenMember
	{
		union { std::array<uint8_t, size> dummy; };
	public:
		template<class ...Args>
		HiddenMember(Args&&... args)
		{
			new (&get()) Ty{ std::forward<Args>(args)... };
		}

		HiddenMember(const HiddenMember& o)
		{
			new (&get()) Ty{ o.get() };
		}

		HiddenMember(HiddenMember&& o) noexcept
		{
			new (&get()) Ty{ std::move(o.get()) };
		}

		HiddenMember& operator=(const HiddenMember& o)
		{
			get() = o.get();
			return *this;
		}

		HiddenMember& operator=(HiddenMember&& o) noexcept
		{
			get() = std::move(o.get());
			return *this;
		}

		~HiddenMember()
		{
			get().~Ty();
		}

		Ty& get() { return *reinterpret_cast<Ty*>(dummy.data()); }
		const Ty& get() const { return *reinterpret_cast<const Ty*>(dummy.data()); }
	};

	template<class It>
	class Range : std::pair<It, It>
	{
	public:
		using std::pair<It, It>::pair;
		using Reference = decltype(*std::declval<It>());

		It begin() const
		{
			return this->first;
		}

		It end() const
		{
			return this->second;
		}

		It begin()
		{
			return this->first;
		}

		It end()
		{
			return this->second;
		}

		std::reverse_iterator<It> rbegin() const
		{
			return std::reverse_iterator<It>{ this->second };
		}

		std::reverse_iterator<It> rend() const
		{
			return std::reverse_iterator<It>{ this->first };
		}

		std::reverse_iterator<It> rbegin()
		{
			return std::reverse_iterator<It>{ this->second };
		}

		std::reverse_iterator<It> rend()
		{
			return std::reverse_iterator<It>{ this->first };
		}

		size_t size() const { return this->second - this->first; }

		const Reference operator[](size_t idx) const
		{
			return this->first[idx];
		}

		Reference operator[](size_t idx)
		{
			return this->first[idx];
		}
	};

	std::ifstream& openFile(std::ifstream& f, const std::string& filePath, std::ios_base::openmode mode = std::ios_base::in);
	std::ofstream& openFile(std::ofstream& f, const std::string& filePath, std::ios_base::openmode mode = std::ios_base::out);
}

