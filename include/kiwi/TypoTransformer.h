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
		template<bool u> friend class TypoIterator;

		KString strPool;
		Vector<size_t> strPtrs, branchPtrs;
		Vector<float> cost;
		Vector<CondVowel> leftCond;
		float costThreshold = 0;

		template<class It>
		void insertSinglePath(It first, It last);

		template<class It>
		void addBranch(It first, It last, float _cost, CondVowel _leftCond);

		template<class It1, class It2, class It3>
		void addBranch(It1 first1, It1 last1, It2 first2, It2 last2, It3 first3, It3 last3, float _cost, CondVowel _leftCond);

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

			RetType(const StrType& _str = {}, float _cost = 0, CondVowel _leftCond = CondVowel::none)
				: str{ _str }, cost{ _cost }, leftCond{ _leftCond }
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

	class PreparedTypoTransformer
	{
		friend class KiwiBuilder;

		struct ReplInfo
		{
			const char16_t* str;
			uint32_t length;
			float cost;
			CondVowel leftCond;

			ReplInfo(const char16_t* _str = nullptr, uint32_t _length = 0, float _cost = 0, CondVowel _leftCond = CondVowel::none)
				: str{ _str }, length{ _length }, cost{ _cost }, leftCond{ _leftCond }
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

		TypoCandidates<true> generate(const std::u16string& orig, float costThreshold = 2.5f) const;
	};

	class TypoTransformer
	{
		friend class KiwiBuilder;
		friend class PreparedTypoTransformer;

		using TrieNode = utils::TrieNode<char16_t, size_t, utils::ConstAccess<UnorderedMap<char16_t, int32_t>>>;

		struct ReplInfo
		{
			uint32_t begin, end;
			float cost;
			CondVowel leftCond;

			ReplInfo(uint32_t _begin = 0, uint32_t _end = 0, float _cost = 0, CondVowel _leftCond = CondVowel::none)
				: begin{ _begin }, end{ _end }, cost{ _cost }, leftCond{ _leftCond }
			{}
		};

		utils::ContinuousTrie<TrieNode> patTrie;
		KString strPool;
		Vector<Vector<ReplInfo>> replacements;

		void addTypoImpl(const KString& orig, const KString& error, float cost, CondVowel leftCond = CondVowel::none);
		void addTypoWithCond(const KString& orig, const KString& error, float cost, CondVowel leftCond = CondVowel::none);
		void addTypoNormalized(const KString& orig, const KString& error, float cost = 1, CondVowel leftCond = CondVowel::none);

	public:
		using TypoDef = std::tuple<std::initializer_list<const char16_t*>, std::initializer_list<const char16_t*>, float, CondVowel>;

		TypoTransformer();
		TypoTransformer(std::initializer_list<TypoDef> lst)
			: TypoTransformer()
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
		}

		~TypoTransformer();
		TypoTransformer(const TypoTransformer&);
		TypoTransformer(TypoTransformer&&) noexcept;
		TypoTransformer& operator=(const TypoTransformer&);
		TypoTransformer& operator=(TypoTransformer&&);

		bool empty() const
		{
			return replacements.empty();
		}

		void addTypo(const std::u16string& orig, const std::u16string& error, float cost = 1, CondVowel leftCond = CondVowel::none);

		PreparedTypoTransformer prepare() const
		{
			return { *this };
		}
	};

	extern const TypoTransformer withoutTypo, basicTypoSet;
}
