/**
 * @file Types.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kiwi C++ API에 쓰이는 주요 타입들을 모아놓은 헤더 파일
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * 
 */

#pragma once

#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <memory>
#include <type_traits>
#include <functional>
#include <stdexcept>
#include <iostream>

#ifdef KIWI_USE_MIMALLOC
#include <mimalloc.h>
#endif

#include "TemplateUtils.hpp"
#include "ScriptType.h"

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

	class IOException : public Exception
	{
	public:
		using Exception::Exception;
	};

	class SerializationException : public std::ios_base::failure
	{
	public:
		using std::ios_base::failure::failure;
	};

	class FormatException : public Exception
	{
	public:
		using Exception::Exception;
	};

	class UnicodeException : public Exception
	{
	public:
		using Exception::Exception;
	};

	class UnknownMorphemeException : public Exception
	{
	public:
		using Exception::Exception;
	};

	class SwTokenizerException : public Exception
	{
	public:
		using Exception::Exception;
	};

	template<class Ty>
	struct Hash
	{
		template<class V>
		size_t operator()(V&& v) const
		{
			return std::hash<Ty>{}(std::forward<V>(v));
		}
	};

#ifdef KIWI_USE_MIMALLOC
	template<typename _Ty>
	using Vector = std::vector<_Ty, mi_stl_allocator<_Ty>>;

	template<typename _Ty>
	using Deque = std::deque<_Ty, mi_stl_allocator<_Ty>>;

	template<typename _K, typename _V>
	using Map = std::map<_K, _V, std::less<_K>, mi_stl_allocator<std::pair<const _K, _V>>>;

	template<typename _K, typename _V, typename _Hash=Hash<_K>>
	using UnorderedMap = std::unordered_map<_K, _V, _Hash, std::equal_to<_K>, mi_stl_allocator<std::pair<const _K, _V>>>;

	template<typename _K, typename _Hash = Hash<_K>>
	using UnorderedSet = std::unordered_set<_K, _Hash, std::equal_to<_K>, mi_stl_allocator<_K>>;

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

	template<typename _Ty>
	using Deque = std::deque<_Ty>;

	template<typename _K, typename _V>
	using Map = std::map<_K, _V>;

	/**
	 * @brief std::unordered_map의 내부용 타입. mimalloc 옵션에 따라 mi_stl_allocator로부터 메모리를 할당받는다.
	 * 
	 * @note UnorderMap은 std::unordered_map과 동일한 역할을 수행하지만,
	 * mimalloc 사용시 UnorderMap이 좀 더 빠른 속도로 메모리를 할당 받을 수 있음.
	 * @sa Vector
	 */
	template<typename _K, typename _V, typename _Hash = Hash<_K>>
	using UnorderedMap = std::unordered_map<_K, _V, _Hash>;

	template<typename _K, typename _Hash = Hash<_K>>
	using UnorderedSet = std::unordered_set<_K, _Hash>;

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

	using U16StringView = std::u16string_view;

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
		xpn, xsn, xsv, xsa, xsm, xr,
		vcp, vcn,
		sf, sp, ss, sso, ssc, se, so, sw, sb,
		sl, sh, sn,
		w_url, w_email, w_mention, w_hashtag, w_serial, w_emoji,
		jks, jkc, jkg, jko, jkb, jkv, jkq, jx, jc,
		ep, ef, ec, etn, etm,
		z_coda, z_siot, 
		user0, user1, user2, user3, user4,
		p, /**< 분할된 동사/형용사를 나타내는데 사용됨 */
		max, /**< POSTag의 총 개수를 나타내는 용도 */
		pv = p,
		pa = p + 1,
		irregular = 0x80, /**< 불규칙 활용을 하는 동/형용사를 나타내는데 사용됨 */
		unknown_feat_ha = 0xFF,

		vvi = vv | irregular,
		vai = va | irregular,
		vxi = vx | irregular,
		xsai = xsa | irregular,
		pvi = pv | irregular,
		pai = pa | irregular,
	};

	inline constexpr bool isIrregular(POSTag tag)
	{
		return !!(static_cast<uint8_t>(tag) & static_cast<uint8_t>(POSTag::irregular));
	}

	inline constexpr POSTag setIrregular(POSTag tag)
	{
		return static_cast<POSTag>(static_cast<uint8_t>(tag) | static_cast<uint8_t>(POSTag::irregular));
	}

	inline constexpr POSTag clearIrregular(POSTag tag)
	{
		return static_cast<POSTag>(static_cast<uint8_t>(tag) & ~static_cast<uint8_t>(POSTag::irregular));
	}

	inline constexpr POSTag setIrregular(POSTag tag, bool irregular)
	{
		return irregular ? setIrregular(tag) : clearIrregular(tag);
	}

    inline constexpr bool areTagsEqual(POSTag a, POSTag b, bool ignoreRegularity = false)
    {
        return ignoreRegularity ? (clearIrregular(a) == clearIrregular(b)) : (a == b);
    }

	constexpr size_t defaultTagSize = (size_t)POSTag::p;

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
		applosive = 8, /**< 불파음 받침(ㄴㄹㅁㅇ을 제외한 모든 받침)*/ // not necessary, but fixed MSVC's weird bug
	};

	/**
	 * @brief 선행 형태소의 양/음성 조건(모음 조화)과 관련된 열거형
	 * 
	 */
	enum class CondPolarity : uint8_t
	{
		none, /**< 조건이 설정되지 않음 */
		positive, /**< 선행 형태소가 양성(ㅏ,ㅑ,ㅗ)인 경우만 등장 가능 */
		negative, /**< 선행 형태소가 음성(그 외)인 경우만 등장 가능 */
		non_adj, /**< 선행 형태소가 형용사가 아닌 경우만 등장 가능 (모음조화와 관련없지만 효율성을 위해 여기에 삽입)*/
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

		loadTypoDict = 1 << 2, /**< 오타 사전(typo.dict)의 로딩 여부를 설정한다.*/

		loadMultiDict = 1 << 3, /**< 복합명사 사전(multi.dict)의 로딩 여부를 설정한다. 복합명사 사전은 복합명사의 구성 형태소를 저장하고 있다. */

		default_ = integrateAllomorph | loadDefaultDict | loadTypoDict | loadMultiDict,
	};

	enum class ModelType
	{
		none = 0, /**< Select default (smallest one of available) model */
		largest = 1, /**< Select default (largest one of available) model */
		knlm = 2, /**< Kneser-Ney Language Model */
		sbg = 3, /**< Skip-Bigram Model */
		cong = 4, /**< Contextual N-gram embedding Language Model (Only local context) */
		congGlobal = 5, /**< Contextual N-gram embedding Language Model (local and global context) */
		congFp32 = 6, /**< Contextual N-gram embedding Language Model (Only local context, non-quantized(slow) version) */
		congGlobalFp32 = 7, /**< Contextual N-gram embedding Language Model (local and global context, non-quantized(slow) version) */
		knlmTransposed,
	};

	enum class Dialect : uint16_t
	{
		standard = 0, /**< 표준어 */
		gyeonggi = 1 << 0, /**< 경기 방언 */
		chungcheong = 1 << 1, /**< 충청 방언 */
		gangwon = 1 << 2, /**< 강원 방언 */
		gyeongsang = 1 << 3, /**< 경상 방언 */
		jeolla = 1 << 4, /**< 전라 방언 */
		jeju = 1 << 5, /**< 제주 방언 */
		hwanghae = 1 << 6, /**< 황해 방언 */
		hamgyeong = 1 << 7, /**< 함경 방언 */
		pyeongan = 1 << 8, /**< 평안 방언 */
		archaic = 1 << 9, /**< 옛말 */
		lastPlus1,
		all = lastPlus1 * 2 - 3,
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
		uint32_t wordPosition = 0; /**< 어절 번호(공백 기준)*/
		uint32_t sentPosition = 0; /**< 문장 번호*/
		uint32_t lineNumber = 0; /**< 줄 번호*/
		uint16_t length = 0; /**< 길이(UTF16 문자 기준) */
		POSTag tag = POSTag::unknown; /**< 품사 태그 */
		union {
			uint8_t senseId = 0; /**< 의미 번호 */
			ScriptType script; /**< 유니코드 영역에 기반한 문자 타입 */
		};
		float score = 0; /**< 해당 형태소의 언어모델 점수 */
		float typoCost = 0; /**< 오타가 교정된 경우 오타 비용. 그렇지 않은 경우 0 */
		uint32_t typoFormId = 0; /**< 교정 전 오타의 형태에 대한 정보 (typoCost가 0인 경우 PreTokenizedSpan의 ID값) */
		uint32_t pairedToken = -1; /**< SSO, SSC 태그에 속하는 형태소의 경우 쌍을 이루는 반대쪽 형태소의 위치(-1인 경우 해당하는 형태소가 없는 것을 뜻함) */
		uint32_t subSentPosition = 0; /**< 인용부호나 괄호로 둘러싸인 하위 문장의 번호. 1부터 시작. 0인 경우 하위 문장이 아님을 뜻함 */
		Dialect dialect = Dialect::standard; /**< 방언 정보 */
		const Morpheme* morph = nullptr; /**< 기타 형태소 정보에 대한 포인터 (OOV인 경우 nullptr) */

		TokenInfo() = default;

		TokenInfo(const std::u16string& _str,
			POSTag _tag = POSTag::unknown,
			uint16_t _length = 0,
			uint32_t _position = 0,
			uint32_t _wordPosition = 0,
			float _score = 0
		)
			: str{ _str }, position{ _position }, wordPosition{ _wordPosition }, length{ _length }, tag{ _tag }, score{ _score }
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

		uint32_t endPos() const { return position + length; }
	};

	struct BasicToken
	{
		std::u16string form;
		uint32_t begin = -1, end = -1;
		POSTag tag = POSTag::unknown;
		uint8_t inferRegularity = 1;

		BasicToken(const std::u16string& _form = {}, uint32_t _begin = -1, uint32_t _end = -1, POSTag _tag = POSTag::unknown, uint8_t _inferRegularity = 1)
			: form{ _form }, begin{ _begin }, end{ _end }, tag{ _tag }, inferRegularity{ _inferRegularity }
		{}
	};

	struct PretokenizedSpan
	{
		uint32_t begin = 0, end = 0;
		std::vector<BasicToken> tokenization;

		PretokenizedSpan(uint32_t _begin = 0, uint32_t _end = 0, const std::vector<BasicToken>& _tokenization = {})
			: begin{ _begin }, end{ _end }, tokenization{ _tokenization }
		{}
	};

	/**
	 * @brief 분석 완료된 형태소의 목록(`std::vector<TokenInfo>`)과 점수(`float`)의 pair 타입
	 * 
	 */
	using TokenResult = std::pair<std::vector<TokenInfo>, float>;

	using U16Reader = std::function<std::u16string()>;
	using U16MultipleReader = std::function<U16Reader()>;

	template<>
	struct Hash<POSTag>
	{
		size_t operator()(POSTag v) const
		{
			return std::hash<uint8_t>{}(static_cast<uint8_t>(v));
		}
	};

	template<>
	struct Hash<CondVowel>
	{
		size_t operator()(CondVowel v) const
		{
			return std::hash<uint8_t>{}(static_cast<uint8_t>(v));
		}
	};

	template<>
	struct Hash<CondPolarity>
	{
		size_t operator()(CondPolarity v) const
		{
			return std::hash<uint8_t>{}(static_cast<uint8_t>(v));
		}
	};

	template<>
	struct Hash<Dialect>
	{
		size_t operator()(Dialect v) const
		{
			return std::hash<uint16_t>{}(static_cast<uint16_t>(v));
		}
	};

	template<class Ty, class Alloc>
	struct Hash<std::vector<Ty, Alloc>>
	{
		size_t operator()(const std::vector<Ty, Alloc>& p) const
		{
			size_t hash = p.size();
			for (auto& v : p)
			{
				hash ^= Hash<Ty>{}(v) + (hash << 6) + (hash >> 2);
			}
			return hash;
		}
	};

	template<class Ty, size_t n>
	struct Hash<std::array<Ty, n>>
	{
		size_t operator()(const std::array<Ty, n>& p) const
		{
			size_t hash = p.size();
			for (auto& v : p)
			{
				hash ^= Hash<Ty>{}(v) + (hash << 6) + (hash >> 2);
			}
			return hash;
		}
	};

	template<class Ty1, class Ty2>
	struct Hash<std::pair<Ty1, Ty2>>
	{
		size_t operator()(const std::pair<Ty1, Ty2>& p) const
		{
			size_t hash = Hash<Ty2>{}(p.second);
			hash ^= Hash<Ty1>{}(p.first) + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	template<class Ty>
	struct Hash<std::tuple<Ty>>
	{
		size_t operator()(const std::tuple<Ty>& p) const
		{
			return Hash<Ty>{}(std::get<0>(p));
		}

	};

	template<class Ty1, class...Rest>
	struct Hash<std::tuple<Ty1, Rest...>>
	{
		size_t operator()(const std::tuple<Ty1, Rest...>& p) const
		{
			size_t hash = Hash<std::tuple<Rest...>>{}(kiwi::tp::tuple_tail(p));
			hash ^= Hash<Ty1>{}(std::get<0>(p)) + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

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
}

KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::BuildOption);
KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::Dialect);
