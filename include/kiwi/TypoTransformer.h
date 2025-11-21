/**
 * @file TypoTransformer.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 오타 교정에 사용되는 TypoTransformer 및 관련 클래스들을 정의합니다.
 * @version 0.22.1
 * @date 2025-11-21
 *
 *
 */
#pragma once

#include "Types.h"
#include "Trie.hpp"
#include "FrozenTrie.h"
#include "Utils.h"

namespace kiwi
{
	template<bool u16wrap>
	class TypoIterator;

	template<bool u16wrap>
	class TypoCandidates
	{
		friend class TypoTransformer;
		friend class PreparedTypoTransformer;
		template<bool> friend class TypoIterator;

		KString strPool;
		Vector<size_t> strPtrs, branchPtrs;
		Vector<std::tuple<float, CondVowel, Dialect>> candidates; // (cost, leftCond, dialect)
		float costThreshold = 0;

		template<class It>
		void insertSinglePath(It first, It last);

		template<class It>
		void addBranch(It first, It last, float _cost, CondVowel _leftCond, Dialect _dialect);

		template<class It1, class It2, class It3>
		void addBranch(It1 first1, It1 last1, It2 first2, It2 last2, It3 first3, It3 last3, float _cost, CondVowel _leftCond, Dialect _dialect);

		void finishBranch();
	public:
		TypoCandidates();
		~TypoCandidates();
		TypoCandidates(const TypoCandidates&);
		TypoCandidates(TypoCandidates&&) noexcept;
		TypoCandidates& operator=(const TypoCandidates&);
		TypoCandidates& operator=(TypoCandidates&&);

		size_t size() const
		{
			if (branchPtrs.empty()) return 0;
			size_t ret = 1;
			for (size_t i = 1; i < branchPtrs.size(); ++i)
			{
				ret *= branchPtrs[i] - branchPtrs[i - 1] - 1;
			}
			return ret;
		}

		TypoIterator<u16wrap> begin() const;
		TypoIterator<u16wrap> end() const;
	};

	template<bool u16wrap>
	class TypoIterator
	{
		const TypoCandidates<u16wrap>* cands = nullptr;
		Vector<size_t> digit;

		bool increase();
		bool valid() const;

	public:

		using StrType = typename std::conditional<u16wrap, std::u16string, KString>::type;

		struct RetType
		{
			StrType str;
			float cost;
			CondVowel leftCond;
			Dialect dialect;

			RetType(const StrType& _str = {}, 
				float _cost = 0, 
				CondVowel _leftCond = CondVowel::none, 
				Dialect _dialect = Dialect::standard
			)
				: str{ _str }, cost{ _cost }, leftCond{ _leftCond }, dialect{ _dialect }
			{}
		};

		using value_type = RetType;
		using reference = value_type;
		using iterator_category = std::forward_iterator_tag;

		// for begin
		TypoIterator(const TypoCandidates<u16wrap>& _cand);

		// for end
		TypoIterator(const TypoCandidates<u16wrap>& _cand, int);

		~TypoIterator();
		TypoIterator(const TypoIterator&);
		TypoIterator(TypoIterator&&) noexcept;
		TypoIterator& operator=(const TypoIterator&);
		TypoIterator& operator=(TypoIterator&&);

		value_type operator*() const;

		bool operator==(const TypoIterator& o) const
		{
			return cands == o.cands && digit == o.digit;
		}

		bool operator!=(const TypoIterator& o) const
		{
			return !operator==(o);
		}

		TypoIterator& operator++();
	};

	class KiwiBuilder;
	class TypoTransformer;

	/**
	* @brief 오타 생성 및 교정 준비가 완료된 오타 생성기. kiwi::TypoTransformer::prepare()로부터 생성됩니다.
	*/
	class PreparedTypoTransformer
	{
		friend class KiwiBuilder;

		struct ReplInfo
		{
			const char16_t* str;
			uint32_t length;
			float cost;
			CondVowel leftCond;
			Dialect dialect;

			ReplInfo(const char16_t* _str = nullptr, uint32_t _length = 0, float _cost = 0, 
				CondVowel _leftCond = CondVowel::none,
				Dialect _dialect = Dialect::standard)
				: str{ _str }, length{ _length }, cost{ _cost }, leftCond{ _leftCond }, dialect{ _dialect }
			{}
		};

		struct PatInfo
		{
			const ReplInfo* repl;
			uint32_t size;
			uint32_t patLength;

			constexpr PatInfo(const ReplInfo* _repl = nullptr, uint32_t _size = 0, uint32_t _patLength = 0)
				: repl{ _repl }, size{ _size }, patLength{ _patLength }
			{}
		};

		struct PatInfoHasSubmatch
		{
			static constexpr bool isNull(const PatInfo& v)
			{
				return !v.repl && v.size != (uint32_t)-1 && v.patLength != (uint32_t)-1;
			}

			static void setHasSubmatch(PatInfo& v)
			{
				v.repl = nullptr;
				v.size = (uint32_t)-1;
				v.patLength = (uint32_t)-1;
			}

			static constexpr bool hasSubmatch(const PatInfo& v)
			{
				return !v.repl && v.size == (uint32_t)-1 && v.patLength == (uint32_t)-1;
			}
		};

		utils::FrozenTrie<char16_t, PatInfo, int32_t, PatInfoHasSubmatch> patTrie;
		KString strPool;
		Vector<ReplInfo> replacements;
		float continualTypoThreshold = INFINITY;
		float lengtheningTypoThreshold = INFINITY;

		template<bool u16wrap = false>
		TypoCandidates<u16wrap> _generate(const KString& orig, float costThreshold = 2.5f) const;

	public:
		PreparedTypoTransformer();
		PreparedTypoTransformer(const TypoTransformer& tt);
		~PreparedTypoTransformer();
		PreparedTypoTransformer(const PreparedTypoTransformer&) = delete;
		PreparedTypoTransformer(PreparedTypoTransformer&&) noexcept;
		PreparedTypoTransformer& operator=(const PreparedTypoTransformer&) = delete;
		PreparedTypoTransformer& operator=(PreparedTypoTransformer&&);

		bool ready() const { return !replacements.empty(); }
		
		float getContinualTypoCost() const
		{
			return continualTypoThreshold;
		}

		float getLengtheningTypoCost() const
		{
			return lengtheningTypoThreshold;
		}

		/**
		* @brief 주어진 문자열에 대해 오타 후보를 생성합니다.
		* 
		* @param orig 원본 문자열
		* @param costThreshold 생성할 오타 후보의 비용 상한
		*/
		TypoCandidates<true> generate(const std::u16string& orig, float costThreshold = 2.5f) const;
	};

	/**
	* @brief 오타 교정에 사용되는 오타 생성기 정의자
	*/
	class TypoTransformer
	{
		friend class KiwiBuilder;
		friend class PreparedTypoTransformer;

		float continualTypoThreshold = INFINITY;
		float lengtheningTypoThreshold = INFINITY;

		UnorderedMap<std::tuple<KString, KString, CondVowel, Dialect>, float> typos;

		void addTypoWithCond(const KString& orig, const KString& error, float cost, CondVowel leftCond = CondVowel::none, Dialect dialect = Dialect::standard);
		void addTypoNormalized(const KString& orig, const KString& error, float cost = 1, CondVowel leftCond = CondVowel::none, Dialect dialect = Dialect::standard);

	public:
		using TypoDef = std::tuple<std::initializer_list<const char16_t*>, std::initializer_list<const char16_t*>, float, CondVowel>;

		TypoTransformer();

		TypoTransformer(std::initializer_list<TypoDef> lst)
			: TypoTransformer()
		{
			addTypos(lst);
		}

		~TypoTransformer();
		TypoTransformer(const TypoTransformer&);
		TypoTransformer(TypoTransformer&&) noexcept;
		TypoTransformer& operator=(const TypoTransformer&);
		TypoTransformer& operator=(TypoTransformer&&);

		bool isContinualTypoEnabled() const;
		bool isLengtheningTypoEnabled() const;
		bool empty() const;

		/**
		* @brief 새 오타를 정의합니다.
		* 
		* @param orig 원본 문자열
		* @param error 오류 문자열
		* @param cost 오류 문자열로 변환하는데 드는 비용. 이 값을 무한대로 설정하면 해당 오타가 비활성화됩니다.
		* @param leftCond 원본 문자열이 오류 문자열로 변환될 때 요구되는 왼쪽 모음의 조건
		* @param dialect 원본 문자열이 오류 문자열로 변환 가능한 방언. 기본값은 표준어입니다.
		* 
		* @note orig, error는 모두 완전한 음절이거나 모음이거나 초성이어야 합니다. 그렇지 않은 경우 invalid_argument 예외가 발생합니다.
		*		addTypo(u"ㅐ", u"ㅔ")는 비용 1을 들여 ㅐ를 ㅔ로 바꾸는 변환을 새로 정의합니다. 
		*		addTypo(u"ㅐ", u"에")는 실패하고 예외를 발생시킵니다.
		*/
		void addTypo(const std::u16string& orig, const std::u16string& error, float cost = 1, CondVowel leftCond = CondVowel::none, Dialect dialect = Dialect::standard);

		TypoTransformer& addTypos(std::initializer_list<TypoDef> lst)
		{
			for (auto& l : lst)
			{
				for (auto i : std::get<0>(l))
				{
					for (auto o : std::get<1>(l))
					{
						addTypo(i, o, std::get<2>(l), std::get<3>(l));
					}
				}
			}
			return *this;
		}

		const UnorderedMap<std::tuple<KString, KString, CondVowel, Dialect>, float>& getTypos() const
		{
			return typos;
		}

		/**
		* @brief 연철 오타의 비용을 새로 설정합니다.
		* 
		* @param threshold 연철 오타의 비용
		* @note 연철 오타의 초기값은 무한대, 즉 비활성화 상태입니다. 유한한 값으로 설정하면 연철 오타가 활성화됩니다.
		*/
		void setContinualTypoCost(float threshold)
		{
			continualTypoThreshold = threshold;
		}

		float getContinualTypoCost() const
		{
			return continualTypoThreshold;
		}

		static TypoTransformer fromContinualTypoCost(float threshold)
		{
			TypoTransformer ret;
			ret.setContinualTypoCost(threshold);
			return ret;
		}

		TypoTransformer copyWithNewContinualTypoCost(float threshold) const;

		/**
		* @brief 장음화 오타의 비용을 새로 설정합니다.
		* 
		* @param threshold 장음화 오타의 비용
		* @note 장음화 오타의 초기값은 무한대, 즉 비활성화 상태입니다. 유한한 값으로 설정하면 장음화 오타가 활성화됩니다.
		*/
		void setLengtheningTypoCost(float threshold)
		{
			lengtheningTypoThreshold = threshold;
		}

		float getLengtheningTypoCost() const
		{
			return lengtheningTypoThreshold;
		}

		static TypoTransformer fromLengtheningTypoCost(float threshold)
		{
			TypoTransformer ret;
			ret.setLengtheningTypoCost(threshold);
			return ret;
		}

		TypoTransformer copyWithNewLengtheningTypoCost(float threshold) const;

		TypoTransformer copyWithDialectOverriding(Dialect dialect) const;

		/**
		* @brief 다른 TypoTransformer의 오타를 현재 TypoTransformer에 추가합니다.
		* 
		* @param o 추가할 TypoTransformer
		* @note 현재 TypoTransformer와 o에서 동일한 오타를 정의하고 있는 경우 비용이 더 낮은 정의가 선택됩니다.
		*		연철 오타와 장음화 오타 역시 마찬가지로 양쪽 중 더 낮은 쪽의 비용이 선택됩니다.
		*/
		void update(const TypoTransformer& o);

		TypoTransformer& operator|=(const TypoTransformer& o)
		{
			update(o);
			return *this;
		}

		TypoTransformer operator|(const TypoTransformer& o) const
		{
			TypoTransformer ret = *this;
			ret.update(o);
			return ret;
		}

		/**
		* @brief 현재 TypoTransformer의 모든 오타의 비용을 scale배 합니다.
		*
		* @param scale 배율
		* @note scale은 0보다 큰 양수여야 합니다. 0, 음수, 무한대의 경우 invalid_argument 예외가 발생합니다.
		*/
		void scaleCost(float scale);

		TypoTransformer& operator*=(float scale)
		{
			scaleCost(scale);
			return *this;
		}

		TypoTransformer operator*(float scale) const
		{
			TypoTransformer ret = *this;
			ret.scaleCost(scale);
			return ret;
		}

		/**
		* @brief 현재 TypoTransformer를 사용하여 PreparedTypoTransformer를 생성합니다. 
		*		PreparedTypoTransformer는 실제로 오타를 생성하거나 kiwi::KiwiBuilder에 전달되어 오타 교정에 사용될 수 있습니다.
		*/
		PreparedTypoTransformer prepare() const
		{
			return { *this };
		}
	};

	enum class DefaultTypoSet
	{
		withoutTypo,
		basicTypoSet,
		continualTypoSet,
		basicTypoSetWithContinual,
		lengtheningTypoSet,
		basicTypoSetWithContinualAndLengthening,
		dialect,
	};

	/**
	* @brief 기본 내장 오타 생성기를 반환합니다.
	* 
	* @param set 사용할 기본 내장 오타 생성기의 종류
	*/
	const TypoTransformer& getDefaultTypoSet(DefaultTypoSet set);
}
