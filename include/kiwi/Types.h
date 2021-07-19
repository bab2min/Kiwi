#pragma once

#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <type_traits>
#include <functional>
#include <stdexcept>

#ifdef USE_MIMALLOC
#include <mimalloc.h>
#endif

#define KIWI_DEFINE_ENUM_FLAG_OPERATORS(Type) \
inline Type operator~(Type a)\
{\
	return static_cast<Type>(~static_cast<typename std::underlying_type<Type>::type>(a));\
}\
inline bool operator!(Type a)\
{\
	return a == static_cast<Type>(0);\
}\
inline Type operator|(Type a, Type b)\
{\
	return static_cast<Type>(static_cast<typename std::underlying_type<Type>::type>(a) | static_cast<typename std::underlying_type<Type>::type>(b));\
}\
inline Type operator&(Type a, Type b)\
{\
	return static_cast<Type>(static_cast<typename std::underlying_type<Type>::type>(a) & static_cast<typename std::underlying_type<Type>::type>(b));\
}\
inline Type operator^(Type a, Type b)\
{\
	return static_cast<Type>(static_cast<typename std::underlying_type<Type>::type>(a) ^ static_cast<typename std::underlying_type<Type>::type>(b));\
}\
inline Type operator|=(Type& a, Type b)\
{\
	return reinterpret_cast<Type&>(reinterpret_cast<typename std::underlying_type<Type>::type&>(a) |= static_cast<typename std::underlying_type<Type>::type>(b));\
}\
inline Type operator&=(Type& a, Type b)\
{\
	return reinterpret_cast<Type&>(reinterpret_cast<typename std::underlying_type<Type>::type&>(a) &= static_cast<typename std::underlying_type<Type>::type>(b));\
}\
inline Type operator^=(Type& a, Type b)\
{\
	return reinterpret_cast<Type&>(reinterpret_cast<typename std::underlying_type<Type>::type&>(a) ^= static_cast<typename std::underlying_type<Type>::type>(b));\
}

namespace kiwi
{
	typedef char16_t kchar_t;

	class Exception : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	class UnicodeException : public Exception
	{
	public:
		using Exception::Exception;
	};

#ifdef USE_MIMALLOC
	template<typename _Ty>
	using Vector = std::vector<_Ty, mi_stl_allocator<_Ty>>;

	template<typename _K, typename _V>
	using UnorderedMap = std::unordered_map<_K, _V, std::hash<_K>, std::equal_to<_K>, mi_stl_allocator<std::pair<const _K, _V>>>;

	using KString = std::basic_string<kchar_t, std::char_traits<kchar_t>, mi_stl_allocator<kchar_t>>;
	using KStringStream = std::basic_stringstream<kchar_t, std::char_traits<kchar_t>, mi_stl_allocator<kchar_t>>;
	using KcVector = Vector<kchar_t>;
	using KcScores = Vector<std::pair<KcVector, float>>;
#else
	template<typename _Ty>
	using Vector = std::vector<_Ty>;

	template<typename _K, typename _V>
	using UnorderedMap = std::unordered_map<_K, _V>;

	using KString = std::basic_string<kchar_t>;
	using KStringStream = std::basic_stringstream<kchar_t>;
	using KcVector = Vector<kchar_t>;
	using KcScores = Vector<std::pair<KcVector, float>>;
#endif

	enum class POSTag : uint8_t
	{
		unknown,
		nng, nnp, nnb,
		vv, va,
		mag,
		nr, np,
		vx,
		mm, maj,
		ic,
		xpn, xsn, xsv, xsa, xr,
		vcp, vcn,
		sf, sp, ss, se, so, sw,
		sl, sh, sn,
		w_url, w_email, w_mention, w_hashtag,
		jks, jkc, jkg, jko, jkb, jkv, jkq, jx, jc,
		ep, ef, ec, etn, etm,
		v,
		max,
	};

	constexpr size_t defaultTagSize = (size_t)POSTag::jks;

	enum class CondVowel : uint8_t
	{
		none,
		any,
		vowel,
		vocalic,
		vocalic_h,
		non_vowel,
		non_vocalic,
		non_vocalic_h,
	};

	enum class CondPolarity : char
	{
		none,
		positive,
		negative
	};

	enum class BuildOption
	{
		none = 0,
		integrateAllomorph = 1 << 0,
		loadDefaultDict = 1 << 1,
	};

	struct Morpheme;

	struct TokenInfo
	{
		std::u16string str;
		uint32_t position = 0;
		uint16_t length = 0;
		POSTag tag = POSTag::unknown;
		const Morpheme* morph = nullptr;

		TokenInfo() = default;

		TokenInfo(const std::u16string& _str,
			POSTag _tag = POSTag::unknown,
			uint16_t _length = 0,
			uint32_t _position = 0
		)
			: str{ _str }, position{ _position }, length{ _length }, tag{ _tag }
		{
		}

		bool operator==(const TokenInfo& o) const
		{
			return str == o.str && tag == o.tag;
		}

		bool operator!=(const TokenInfo& o) const
		{
			return !operator==(o);
		}
	};

	struct FormCond
	{
		KString form;
		CondVowel vowel;
		CondPolarity polar;
		
		FormCond() = default;
		FormCond(const KString& _form, CondVowel _vowel, CondPolarity _polar)
			: form{ _form }, vowel{ _vowel }, polar{ _polar }
		{
		}

		bool operator==(const FormCond& o) const
		{
			return form == o.form && vowel == o.vowel && polar == o.polar;
		}

		bool operator!=(const FormCond& o) const
		{
			return !operator==(o);
		}
	};

	using TokenResult = std::pair<std::vector<TokenInfo>, float>;

	using U16Reader = std::function<std::u16string()>;
	using U16MultipleReader = std::function<U16Reader()>;
}

namespace std
{
#ifdef USE_MIMALLOC
	template<>
	struct hash<kiwi::KString>
	{
		size_t operator()(const kiwi::KString& s) const
		{
			return hash<basic_string<kiwi::kchar_t>>{}({ s.begin(), s.end() });
		}
	};
#endif

	template<>
	struct hash<kiwi::FormCond>
	{
		size_t operator()(const kiwi::FormCond& fc) const
		{
			return hash<kiwi::KString>{}(fc.form) ^ ((size_t)fc.vowel | ((size_t)fc.polar << 8));
		}
	};
}

KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::BuildOption);