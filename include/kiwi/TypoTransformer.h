#pragma once

#include "Types.h"
#include "Utils.h"

namespace kiwi
{
	template<bool u16wrap>
	class TypoIterator;

	template<bool u16wrap>
	class TypoCandidates
	{
		friend class TypoTransformer;
		template<bool u> friend class TypoIterator;
		KString data;
		Vector<size_t> ptrs, pptrs;
		Vector<float> cost;

		template<class It>
		void insertSinglePath(It first, It last);

		template<class It>
		void addBranch(It first, It last, float _cost);

		void finishBranch();
	public:
		TypoCandidates();
		~TypoCandidates();
		TypoCandidates(const TypoCandidates&);
		TypoCandidates(TypoCandidates&&) noexcept;
		TypoCandidates& operator=(const TypoCandidates&);
		TypoCandidates& operator=(TypoCandidates&&) noexcept;

		size_t size() const
		{
			if (pptrs.empty()) return 0;
			size_t ret = 1;
			for (size_t i = 1; i < pptrs.size(); ++i)
			{
				ret *= pptrs[i] - pptrs[i - 1];
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

	public:

		using value_type = std::pair<typename std::conditional<u16wrap, std::u16string, KString>::type, float>;
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
		TypoIterator& operator=(TypoIterator&&) noexcept;

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

	class TypoTransformer
	{
		friend class KiwiBuilder;

		UnorderedMap<char16_t, Vector<std::pair<char16_t, float>>> xformMap;

		void _addTypo(char16_t orig, char16_t error, float cost);

		template<bool u16wrap = false>
		TypoCandidates<u16wrap> _generate(const KString& orig) const;

	public:
		TypoTransformer();
		TypoTransformer(std::initializer_list<std::tuple<const char16_t*, const char16_t*, float>> l);
		~TypoTransformer();
		TypoTransformer(const TypoTransformer&);
		TypoTransformer(TypoTransformer&&) noexcept;
		TypoTransformer& operator=(const TypoTransformer&);
		TypoTransformer& operator=(TypoTransformer&&) noexcept;

		bool empty() const
		{
			return xformMap.empty();
		}

		void addTypo(char16_t orig, char16_t error, float cost = 1);

		void addTypo(const KString& orig, const KString& error, float cost = 1)
		{
			for(auto o : orig) for (auto e : error) addTypo(o, e, cost);
		}

		TypoCandidates<true> generate(const std::u16string& orig) const;
	};

	extern TypoTransformer withoutTypo, basicTypoSet;
}
