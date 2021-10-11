﻿/**
 * @file Types.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kiwi C++ API에 쓰이는 주요 타입들을 모아놓은 헤더 파일
 * @version 0.10.0
 * @date 2021-08-31
 * 
 * 
 */

#pragma once

#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <type_traits>
#include <functional>
#include <stdexcept>

#ifdef KIWI_USE_MIMALLOC
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

#ifdef KIWI_USE_MIMALLOC
	template<typename _Ty>
	using Vector = std::vector<_Ty, mi_stl_allocator<_Ty>>;

	template<typename _K, typename _V>
	using UnorderedMap = std::unordered_map<_K, _V, std::hash<_K>, std::equal_to<_K>, mi_stl_allocator<std::pair<const _K, _V>>>;

	using KString = std::basic_string<kchar_t, std::char_traits<kchar_t>, mi_stl_allocator<kchar_t>>;
	using KStringStream = std::basic_stringstream<kchar_t, std::char_traits<kchar_t>, mi_stl_allocator<kchar_t>>;
	using KcVector = Vector<kchar_t>;
	using KcScores = Vector<std::pair<KcVector, float>>;
#else
	/**
	 * @brief std::vector의 내부용 타입. mimalloc 옵션에 따라 mi_stl_allocator로부터 메모리를 할당받는다.
	 * 
	 * @note Vector는 std::vector와 동일한 역할을 수행하지만,
	 * mimalloc 사용시 Vector가 좀 더 빠른 속도로 메모리를 할당 받을 수 있음.
	 * Vector와 std::vector는 섞어 쓸 수 없다. 
	 * Kiwi 내부에서만 사용할 것이라면 Vector를, 외부로 반환해야할 값이라면 std::vector를 사용할 것.
	 */
	template<typename _Ty>
	using Vector = std::vector<_Ty>;

	/**
	 * @brief std::unordered_map의 내부용 타입. mimalloc 옵션에 따라 mi_stl_allocator로부터 메모리를 할당받는다.
	 * 
	 * @note UnorderMap은 std::unordered_map과 동일한 역할을 수행하지만,
	 * mimalloc 사용시 UnorderMap이 좀 더 빠른 속도로 메모리를 할당 받을 수 있음.
	 * @sa Vector
	 */
	template<typename _K, typename _V>
	using UnorderedMap = std::unordered_map<_K, _V>;

	/**
	 * @brief std::u16string의 내부용 타입. mimalloc 옵션에 따라 mi_stl_allocator로부터 메모리를 할당받는다.
	 * 
	 * @note KString은 std::u16string과 동일한 역할을 수행하지만,
	 * mimalloc 사용시 KString이 좀 더 빠른 속도로 메모리를 할당 받을 수 있음.
	 * @sa Vector
	 */
	using KString = std::basic_string<kchar_t>;
	using KStringStream = std::basic_stringstream<kchar_t>;
	using KcVector = Vector<kchar_t>;
	using KcScores = Vector<std::pair<KcVector, float>>;
#endif

	/**
	 * @brief 형태소 품사 태그와 관련된 열거형
	 * 
	 * @note 나머지 품사 태그에 대한 정보는 README.md 를 참조할 것.
	 */
	enum class POSTag : uint8_t
	{
		unknown, /**< 미설정 */
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
		v, /**< 분할된 동사/형용사를 나타내는데 사용됨 */
		max, /**< POSTag의 총 개수를 나타내는 용도 */
	};

	constexpr size_t defaultTagSize = (size_t)POSTag::jks;

	/**
	 * @brief 선행 형태소의 종성 여부 조건과 관련된 열거형
	 * 
	 */
	enum class CondVowel : uint8_t
	{
		none, /**< 조건이 설정되지 않음 */
		any, /**< 자음, 모음 여부와 상관 없이 등장 가능 */
		vowel, /**< 선행 형태소가 받침이 없는 경우만 등장 가능*/
		vocalic, /**< 선행 형태소가 받침이 없거나 ㄹ받침인 경우만 등장 가능*/
		vocalic_h, /**< 선행 형태소가 받침이 없거나 ㄹ, ㅎ 받침인 경우만 등장 가능 */
		non_vowel, /**< `vowel`의 부정 */
		non_vocalic, /**< `vocalic`의 부정 */
		non_vocalic_h, /**< `vocalic_h`의 부정 */
	};

	/**
	 * @brief 선행 형태소의 양/음성 조건(모음 조화)과 관련된 열거형
	 * 
	 */
	enum class CondPolarity : char
	{
		none, /**< 조건이 설정되지 않음 */
		positive, /**< 선행 형태소가 양성(ㅏ,ㅑ,ㅗ)인 경우만 등장 가능 */
		negative, /**< 선행 형태소가 음성(그 외)인 경우만 등장 가능 */
	};

	/**
	 * @brief KiwiBuilder 생성시 사용되는 비트 플래그
	 * 
	 * @sa `kiwi::KiwiBuilder`
	 */
	enum class BuildOption
	{
		none = 0,

		integrateAllomorph = 1 << 0, /**< 이형태 통합 여부를 설정한다. 이 옵션을 사용시 `아/EC, 어/EC, 여/EC` 와 같은 형태소들이 `어/EC`로 통합되어 출력된다. */
		
		loadDefaultDict = 1 << 1, /**< 기본 사전(default.dict)의 로딩 여부를 설정한다. 기본 사전은 위키백과 및 나무위키의 표제어로 구성되어 있다. */
	};

	struct Morpheme;

	/**
	 * @brief 분석 완료된 각 형태소들의 정보를 담는 구조체
	 * 
	 */
	struct TokenInfo
	{
		std::u16string str; /**< 형태 */
		uint32_t position = 0; /**< 시작 위치(UTF16 문자 기준) */
		uint16_t length = 0; /**< 길이(UTF16 문자 기준) */
		uint16_t wordPosition = 0; /**< 어절 번호(공백 기준)*/
		POSTag tag = POSTag::unknown; /**< 품사 태그 */
		const Morpheme* morph = nullptr; /**< 기타 형태소 정보에 대한 포인터 (OOV인 경우 nullptr) */

		TokenInfo() = default;

		TokenInfo(const std::u16string& _str,
			POSTag _tag = POSTag::unknown,
			uint16_t _length = 0,
			uint32_t _position = 0,
			uint16_t _wordPosition = 0
		)
			: str{ _str }, position{ _position }, length{ _length }, wordPosition{ _wordPosition }, tag{ _tag }
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
		
		FormCond();
		~FormCond();
		FormCond(const FormCond&);
		FormCond(FormCond&&);
		FormCond& operator=(const FormCond&);
		FormCond& operator=(FormCond&&);

		FormCond(const KString& _form, CondVowel _vowel, CondPolarity _polar);
		bool operator==(const FormCond& o) const;
		bool operator!=(const FormCond& o) const;
	};

	/**
	 * @brief 분석 완료된 형태소의 목록(`std::vector<TokenInfo>`)과 점수(`float`)의 pair 타입
	 * 
	 */
	using TokenResult = std::pair<std::vector<TokenInfo>, float>;

	using U16Reader = std::function<std::u16string()>;
	using U16MultipleReader = std::function<U16Reader()>;
}

namespace std
{
#ifdef KIWI_USE_MIMALLOC
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
