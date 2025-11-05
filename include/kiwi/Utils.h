#pragma once
#include <iostream>
#include <string>
#include <memory>
#include <array>
#include <functional>
#include <unordered_map>
#include <vector>
#include "Types.h"

namespace kiwi
{
	namespace utils
	{
		class MemoryObject; // Forward declaration
	}
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
	KString normalizeHangul(const std::u16string& hangul);

	inline bool isWebTag(POSTag t)
	{
		return POSTag::w_url <= t && t <= POSTag::w_emoji;
	}

	POSTag toPOSTag(const std::u16string& tagStr);
	const char* tagToString(POSTag t);
	const kchar_t* tagToKString(POSTag t);
	
	const char* tagRToString(char16_t form, POSTag t);
	const kchar_t* tagRToKString(char16_t form, POSTag t);

	template<class A, class B, class C>
	inline bool within(A value, B lower, C upper)
	{
		return lower <= value && value < upper;
	}

	template<class A, class B>
	inline bool within(const A* value, const std::vector<A, B>& cont)
	{
		return cont.data() <= value && value < cont.data() + cont.size();
	}

	inline bool isHangulSyllable(char16_t chr)
	{
		return within(chr, 0xAC00, 0xD7A4);
	}

	inline bool isHangulOnset(char16_t chr)
	{
		return within(chr, 0x1100, 0x1100 + 19);
	}

	inline bool isHangulCoda(char16_t chr)
	{
		return within(chr, 0x11A8, 0x11A8 + 27);
	}

	inline bool isHangulVowel(char16_t chr)
	{
		return within(chr, 0x314F, 0x3164);
	}

	inline char16_t joinOnsetVowel(size_t onset, size_t vowel)
	{
		return 0xAC00 + (char16_t)((onset * 21 + vowel) * 28);
	}

	inline int extractVowel(char16_t chr)
	{
		return ((chr - 0xAC00) / 28) % 21;
	}

	inline bool isOldHangulOnset(char16_t chr)
	{
		return within(chr, 0x1100, 0x1160) || within(chr, 0xA960, 0xA980);
	}

	inline bool isOldHangulVowel(char16_t chr)
	{
		return within(chr, 0x1160, 0x11A8) || within(chr, 0xD7B0, 0xD7CB);
	}

	inline bool isOldHangulCoda(char16_t chr)
	{
		return within(chr, 0x11A8, 0x1200) || within(chr, 0xD7CB, 0xD800);
	}

	inline bool isOldHangulToneMark(char16_t chr)
	{
		return within(chr, 0x302E, 0x3030);
	}

	inline bool isCompatibleHangulConsonant(char16_t chr)
	{
		return within(chr, 0x3131, 0x314E) || within(chr, 0x3165, 0x3186);
	}

	char16_t toCompatibleHangulConsonant(char16_t chr);

	struct ComparatorIgnoringSpace
	{
		static bool less(const KString& a, const KString& b, const kchar_t space = u' ');
		static bool equal(const KString& a, const KString& b, const kchar_t space = u' ');
	};

	KString removeSpace(const KString& str, const kchar_t space = u' ');

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
			if (!ret.empty() && isHangulSyllable(ret.back()))
			{
				const bool alreadyHasCoda = (ret.back() - 0xAC00) % 28;
				if (alreadyHasCoda)
				{
					ret.push_back(c);
				}
				else if (isHangulCoda(c))
				{
					ret.back() += c - 0x11A7;
				}
				else if (isOldHangulCoda(c))
				{
					const auto onset = (ret.back() - 0xAC00) / 28 / 21;
					const auto vowel = (ret.back() - 0xAC00) / 28 % 21;
					ret.back() = 0x1100 + onset;
					ret.push_back(0x1161 + vowel);
					ret.push_back(c);
				}
				else
				{
					ret.push_back(c);
				}
			}
			else
			{
				ret.push_back(c);
			}
		}
		return ret;
	}

	template<class It, class Ty, class Alloc>
	inline std::u16string joinHangul(It first, It last, std::vector<Ty, Alloc>& positionOut)
	{
		std::u16string ret;
		ret.reserve(std::distance(first, last));
		positionOut.clear();
		positionOut.reserve(std::distance(first, last));
		for (; first != last; ++first)
		{
			auto c = *first;
			if (!ret.empty() && isHangulSyllable(ret.back()))
			{
				const bool alreadyHasCoda = (ret.back() - 0xAC00) % 28;
				if (alreadyHasCoda)
				{
					positionOut.emplace_back(ret.size());
					ret.push_back(c);
				}
				else if (isHangulCoda(c))
				{
					positionOut.emplace_back(ret.size() - 1);
					ret.back() += c - 0x11A7;
				}
				else if (isOldHangulCoda(c))
				{
					const auto onset = (ret.back() - 0xAC00) / 28 / 21;
					const auto vowel = (ret.back() - 0xAC00) / 28 % 21;
					positionOut.emplace_back(ret.size() - 1);
					ret.back() = 0x1100 + onset;
					ret.push_back(0x1161 + vowel);
					ret.push_back(c);
				}
				else
				{
					positionOut.emplace_back(ret.size());
					ret.push_back(c);
				}	
			}
			else
			{
				positionOut.emplace_back(ret.size());
				ret.push_back(c);
			}
		}
		return ret;
	}

	inline std::u16string joinHangul(const KString& hangul)
	{
		return joinHangul(hangul.begin(), hangul.end());
	}

	inline bool isHighSurrogate(char16_t c)
	{
		return (c & 0xFC00) == 0xD800;
	}

	inline bool isLowSurrogate(char16_t c)
	{
		return (c & 0xFC00) == 0xDC00;
	}

	inline char32_t mergeSurrogate(char16_t h, char16_t l)
	{
		return (((h & 0x3FF) << 10) | (l & 0x3FF)) + 0x10000;
	}

	inline std::array<char16_t, 2> decomposeSurrogate(char32_t c)
	{
		std::array<char16_t, 2> ret;
		if (c < 0x10000)
		{
			ret[0] = c;
			ret[1] = 0;
		}
		else
		{
			c -= 0x10000;
			ret[0] = ((c >> 10) & 0x3FF) | 0xD800;
			ret[1] = (c & 0x3FF) | 0xDC00;
		}
		return ret;
	}

	POSTag identifySpecialChr(char32_t chr);
	size_t getSSType(char16_t c);
	size_t getSBType(const std::u16string& form);

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
		case u'\xA0':
		case u'\u1680':
		case u'\u2000':
		case u'\u2001':
		case u'\u2002':
		case u'\u2003':
		case u'\u2004':
		case u'\u2005':
		case u'\u2006':
		case u'\u2007':
		case u'\u2008':
		case u'\u2009':
		case u'\u200A':
		case u'\u202F':
		case u'\u205F':
		case u'\u2800':
		case u'\u3000':
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
	bool isOpenable(const std::string& filePath);

	// Forward declaration for StreamProvider (defined in Kiwi.h)
	class KiwiBuilder;

	namespace utils
	{
		/**
		 * @brief 파일 시스템에서 모델을 읽어오는 스트림 제공자를 생성한다.
		 *
		 * @param modelPath 모델이 위치한 기본 경로
		 * @return StreamProvider 파일명을 받아 해당 경로의 파일 스트림을 반환하는 함수 객체
		 */
		std::function<std::unique_ptr<std::istream>(const std::string&)> makeFilesystemProvider(const std::string& modelPath);

		/**
		 * @brief 메모리의 바이트 배열에서 모델을 읽어오는 스트림 제공자를 생성한다.
		 *
		 * @param fileData 파일명을 키로 하고 파일 내용을 값으로 하는 맵
		 * @return StreamProvider 파일명을 받아 해당 메모리 데이터의 스트림을 반환하는 함수 객체
		 */
		std::function<std::unique_ptr<std::istream>(const std::string&)> makeMemoryProvider(const std::unordered_map<std::string, std::vector<char>>& fileData);

		/**
		 * @brief 스트림에서 MemoryObject를 생성한다.
		 *
		 * @param stream 읽어올 스트림
		 * @return MemoryObject 스트림 내용을 담은 메모리 객체
		 */
		utils::MemoryObject createMemoryObjectFromStream(std::istream& stream);
	}

	const char* modelTypeToStr(ModelType type);

	Dialect toDialect(std::string_view str);
	const char* dialectToStr(Dialect dialect);
	Dialect parseDialects(std::string_view str);

	inline Dialect dialectAnd(Dialect a, Dialect b)
	{
		if (a == Dialect::standard) return b;
		if (b == Dialect::standard) return a;
		return a & b;
	}

	inline Dialect dialectOr(Dialect a, Dialect b)
	{
		if (a == Dialect::standard) return a;
		if (b == Dialect::standard) return b;
		return a | b;
	}

	inline bool dialectHasIntersection(Dialect a, Dialect b)
	{
		if (a == Dialect::standard || b == Dialect::standard) return true;
		return (a & b) != Dialect::standard;
	}
}

