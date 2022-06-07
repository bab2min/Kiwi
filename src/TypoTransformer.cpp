#include <kiwi/TypoTransformer.h>
#include <kiwi/Utils.h>
#include "StrUtils.h"
#include "FrozenTrie.hpp"

using namespace std;
using namespace kiwi;

template<bool u16wrap>
TypoCandidates<u16wrap>::TypoCandidates()
	: strPtrs(1), branchPtrs(1)
{}

template<bool u16wrap>
TypoCandidates<u16wrap>::~TypoCandidates() = default;

template<bool u16wrap>
TypoCandidates<u16wrap>::TypoCandidates(const TypoCandidates&) = default;

template<bool u16wrap>
TypoCandidates<u16wrap>::TypoCandidates(TypoCandidates&&) noexcept = default;

template<bool u16wrap>
TypoCandidates<u16wrap>& TypoCandidates<u16wrap>::operator=(const TypoCandidates&) = default;

template<bool u16wrap>
TypoCandidates<u16wrap>& TypoCandidates<u16wrap>::operator=(TypoCandidates&&) noexcept = default;

template<bool u16wrap>
TypoIterator<u16wrap> TypoCandidates<u16wrap>::begin() const
{
	return { *this };
}

template<bool u16wrap>
TypoIterator<u16wrap> TypoCandidates<u16wrap>::end() const
{
	return { *this, 0 };
}

template<bool u16wrap>
template<class It>
void TypoCandidates<u16wrap>::insertSinglePath(It first, It last)
{
	strPool.insert(strPool.end(), first, last);
	strPtrs.emplace_back(strPool.size());
}

template<bool u16wrap>
template<class It>
void TypoCandidates<u16wrap>::addBranch(It first, It last, float _cost)
{
	strPool.insert(strPool.end(), first, last);
	strPtrs.emplace_back(strPool.size());
	cost.emplace_back(_cost);
}

template<bool u16wrap>
template<class It1, class It2, class It3>
void TypoCandidates<u16wrap>::addBranch(It1 first1, It1 last1, It2 first2, It2 last2, It3 first3, It3 last3, float _cost)
{
	strPool.insert(strPool.end(), first1, last1);
	strPool.insert(strPool.end(), first2, last2);
	strPool.insert(strPool.end(), first3, last3);
	strPtrs.emplace_back(strPool.size());
	cost.emplace_back(_cost);
}

template<bool u16wrap>
void TypoCandidates<u16wrap>::finishBranch()
{
	branchPtrs.emplace_back(strPtrs.size() - 1);
}

template<bool u16wrap>
TypoIterator<u16wrap>::TypoIterator(const TypoCandidates<u16wrap>& _cand)
	: cands{ &_cand }, digit(std::max(_cand.branchPtrs.size(), (size_t)2) - 1)
{
	while (!valid())
	{
		if (increase()) break;
	}
}

template<bool u16wrap>
TypoIterator<u16wrap>::TypoIterator(const TypoCandidates<u16wrap>& _cand, int)
	: cands{ &_cand }, digit(std::max(_cand.branchPtrs.size(), (size_t)2) - 1)
{
	if (cands->branchPtrs.size() <= 1)
	{
		digit.back() = 1;
	}
	else
	{
		digit.back() = _cand.branchPtrs.end()[-1] - _cand.branchPtrs.end()[-2] - 1;
	}
}

template<bool u16wrap>
TypoIterator<u16wrap>::~TypoIterator() = default;

template<bool u16wrap>
TypoIterator<u16wrap>::TypoIterator(const TypoIterator&) = default;

template<bool u16wrap>
TypoIterator<u16wrap>::TypoIterator(TypoIterator&&) noexcept = default;

template<bool u16wrap>
TypoIterator<u16wrap>& TypoIterator<u16wrap>::operator=(const TypoIterator&) = default;

template<bool u16wrap>
TypoIterator<u16wrap>& TypoIterator<u16wrap>::operator=(TypoIterator&&) noexcept = default;

namespace detail
{
	template<bool u16wrap>
	struct ToU16;

	template<>
	struct ToU16<true>
	{
		u16string operator()(KString&& o)
		{
			return joinHangul(o);
		}
	};

	template<>
	struct ToU16<false>
	{
		KString operator()(KString&& o)
		{
			return std::move(o);
		}
	};
}

template<bool u16wrap>
bool TypoIterator<u16wrap>::increase()
{
	if (cands->branchPtrs.size() <= 1)
	{
		digit[0]++;
		return true;
	}
	if (digit.back() >= cands->branchPtrs.end()[-1] - cands->branchPtrs.end()[-2] - 1) return true;

	digit[0]++;
	for (size_t i = 0; i < digit.size() - 1; ++i)
	{
		if (digit[i] < cands->branchPtrs[i + 1] - cands->branchPtrs[i] - 1) break;
		digit[i] = 0;
		digit[i + 1]++;
	}
	return digit.back() >= cands->branchPtrs.end()[-1] - cands->branchPtrs.end()[-2] - 1;
}

template<bool u16wrap>
bool TypoIterator<u16wrap>::valid() const
{
	if (cands->branchPtrs.size() <= 1) return true;
	float cost = 0;
	for (size_t i = 0; i < digit.size(); ++i)
	{
		size_t s = cands->branchPtrs[i] + digit[i];
		cost += cands->cost[s - i];
	}
	return cost <= cands->costThreshold;
}

template<bool u16wrap>
auto TypoIterator<u16wrap>::operator*() const -> value_type
{
	KString ret;
	float cost = 0;
	if (cands->branchPtrs.size() > 1)
	{
		for (size_t i = 0; i < digit.size(); ++i)
		{
			ret.insert(ret.end(), cands->strPool.begin() + cands->strPtrs[cands->branchPtrs[i]], cands->strPool.begin() + cands->strPtrs[cands->branchPtrs[i] + 1]);
			size_t s = cands->branchPtrs[i] + digit[i];
			cost += cands->cost[s - i];
			ret.insert(ret.end(), cands->strPool.begin() + cands->strPtrs[s + 1], cands->strPool.begin() + cands->strPtrs[s + 2]);
		}
	}
	ret.insert(ret.end(), cands->strPool.begin() + cands->strPtrs[cands->branchPtrs.back()], cands->strPool.begin() + cands->strPtrs[cands->branchPtrs.back() + 1]);
	return { detail::ToU16<u16wrap>{}(move(ret)), cost };
}

template<bool u16wrap>
TypoIterator<u16wrap>& TypoIterator<u16wrap>::operator++()
{
	if (digit.empty()) return *this;
	do
	{
		if (increase()) break;
	} while (!valid());

	return *this;
}

TypoTransformer::TypoTransformer()
	: patTrie{ 1 }
{}
TypoTransformer::~TypoTransformer() = default;
TypoTransformer::TypoTransformer(const TypoTransformer&) = default;
TypoTransformer::TypoTransformer(TypoTransformer&&) noexcept = default;
TypoTransformer& TypoTransformer::operator=(const TypoTransformer&) = default;
TypoTransformer& TypoTransformer::operator=(TypoTransformer&&) noexcept = default;

void TypoTransformer::addTypoImpl(const KString& orig, const KString& error, float cost, CondVowel leftCond)
{
	if (orig == error) return;

	size_t replIdx = patTrie.build(orig.begin(), orig.end(), replacements.size() + 1)->val - 1;
	if (replIdx == replacements.size())
	{
		replacements.emplace_back();
	}
	bool updated = false;
	for (auto& p : replacements[replIdx])
	{
		if (strPool.substr(p.first.first, p.first.second) == error)
		{
			p.second = min(p.second, cost);
			updated = true;
			break;
		}
	}
	if (!updated)
	{
		replacements[replIdx].emplace_back(make_pair(strPool.size(), strPool.size() + error.size()), cost);
		strPool += error;
	}
}

void TypoTransformer::addTypoNormalized(const KString& orig, const KString& error, float cost, CondVowel leftCond)
{
	if (isHangulVowel(orig[0]) != isHangulVowel(error[0]))
		throw invalid_argument{ "`orig` and `error` are both vowel or not. But `orig`=" + utf16To8(orig) + ", `error`=" + utf16To8(error) };

	if (isHangulVowel(orig[0]))
	{
		KString tOrig = orig, tError = error;
		for (size_t i = 0; i < 19; ++i)
		{
			tOrig[0] = joinOnsetVowel(i, orig[0] - u'ㅏ');
			tError[0] = joinOnsetVowel(i, error[0] - u'ㅏ');
			addTypoImpl(tOrig, tError, cost, leftCond);
		}
	}
	else
	{
		addTypoImpl(orig, error, cost, leftCond);
	}
}

void TypoTransformer::addTypo(const u16string& orig, const u16string& error, float cost, CondVowel leftCond)
{
	return addTypoNormalized(normalizeHangul(orig), normalizeHangul(error), cost, leftCond);
}

PreparedTypoTransformer::PreparedTypoTransformer(const TypoTransformer& tt)
	: strPool{ tt.strPool }
{
	size_t tot = 0;
	for (auto& rs : tt.replacements) tot += rs.size();
	replacements.reserve(tot);
	
	Vector<std::pair<const ReplInfo*, uint32_t>> patData;
	for (auto& rs : tt.replacements)
	{
		patData.emplace_back(replacements.data() + replacements.size(), rs.size());
		for (auto& r : rs)
		{
			replacements.emplace_back(strPool.data() + r.first.first, r.first.second - r.first.first, r.second);
		}
	}
	
	patTrie = decltype(patTrie){ tt.patTrie, ArchTypeHolder<ArchType::none>{}, [&](const TypoTransformer::TrieNode& o) -> PatInfo
		{
			if (o.val) return { patData[o.val - 1].first, patData[o.val - 1].second, o.depth };
			else return { nullptr, 0, o.depth };
		}
	};
}

PreparedTypoTransformer::~PreparedTypoTransformer() = default;
PreparedTypoTransformer::PreparedTypoTransformer(PreparedTypoTransformer&&) noexcept = default;
PreparedTypoTransformer& PreparedTypoTransformer::operator=(PreparedTypoTransformer&&) noexcept = default;

template<bool u16wrap>
TypoCandidates<u16wrap> PreparedTypoTransformer::_generate(const KString& orig, float costThreshold) const
{
	TypoCandidates<u16wrap> ret;
	ret.costThreshold = costThreshold;
	Vector<pair<size_t, const PatInfo&>> matches;
	size_t last = 0;
	const auto& insertBranch = [&]()
	{
		size_t totStartPos = matches[0].first - matches[0].second.patLength;
		size_t totEndPos = matches.back().first;
		ret.insertSinglePath(orig.begin() + last, orig.begin() + totStartPos);
		ret.addBranch(orig.begin() + totStartPos, orig.begin() + totEndPos, 0.f);
		for (auto& m : matches)
		{
			size_t e = m.first;
			size_t s = e - m.second.patLength;
			for (size_t j = 0; j < m.second.size; ++j)
			{
				auto& repl = m.second.repl[j];
				ret.addBranch(orig.begin() + totStartPos, orig.begin() + s,
					repl.str, repl.str + repl.length,
					orig.begin() + e, orig.begin() + totEndPos,
					repl.cost
				);
			}
		}
		ret.finishBranch();
		last = totEndPos;
		matches.clear();
	};

	auto node = patTrie.root();
	for (size_t i = 0; i < orig.size(); ++i)
	{
		auto nnode = node->nextOpt<ArchType::none>(patTrie, orig[i]);
		while (!nnode)
		{
			node = node->fail();
			if (node)
			{
				nnode = node->nextOpt<ArchType::none>(patTrie, orig[i]);
			}
			else
			{
				node = patTrie.root();
				goto nextchar;
			}
		}
		node = nnode;

		auto& v = node->val(patTrie);
		if (patTrie.isNull(v)) goto nextchar;

		size_t endPos = i + 1;
		size_t startPos = endPos - v.patLength;
		if (!matches.empty() && matches.back().first < startPos)
		{
			insertBranch();
		}
		for (auto sub = node; sub; sub = sub->fail())
		{
			auto& sv = sub->val(patTrie);
			if (patTrie.isNull(sv)) break;
			if (patTrie.hasSubmatch(sv)) continue;
			matches.emplace_back(endPos, sv);
		}
	nextchar:;
	}
	if (!matches.empty())
	{
		insertBranch();
	}
	ret.insertSinglePath(orig.begin() + last, orig.end());
	return ret;
}

TypoCandidates<true> PreparedTypoTransformer::generate(const u16string& orig, float costThreshold) const
{
	return _generate<true>(normalizeHangul(orig), costThreshold);
}


template class TypoCandidates<true>;
template class TypoCandidates<false>;
template class TypoIterator<true>;
template class TypoIterator<false>;

template TypoCandidates<true> PreparedTypoTransformer::_generate<true>(const KString&, float) const;
template TypoCandidates<false> PreparedTypoTransformer::_generate<false>(const KString&, float) const;

const TypoTransformer kiwi::withoutTypo;

const TypoTransformer kiwi::basicTypoSet = {
	{ {u"ㅐ", u"ㅔ"}, {u"ㅐ", u"ㅔ"}, 1.f, CondVowel::none },
	{ {u"ㅐ", u"ㅔ"}, {u"ㅒ", u"ㅖ"}, 1.5f, CondVowel::none },
	{ {u"ㅒ", u"ㅖ"}, {u"ㅐ", u"ㅔ"}, 1.5f, CondVowel::none },
	{ {u"ㅒ", u"ㅖ"}, {u"ㅒ", u"ㅖ"}, 1.f, CondVowel::none },
	{ {u"ㅚ", u"ㅙ", u"ㅞ"}, {u"ㅚ", u"ㅙ", u"ㅞ", u"ㅐ", u"ㅔ"}, 1.f, CondVowel::none },
	{ {u"ㅝ"}, {u"ㅗ", u"ㅓ"}, 1.f, CondVowel::none },
	{ {u"ㅟ"}, {u"ㅣ"}, 1.f, CondVowel::none },
	{ {u"ㅢ"}, {u"ㅣ"}, 1.f, CondVowel::none },
	{ {u"자", u"쟈"}, {u"자", u"쟈"}, 1.f, CondVowel::none },
	{ {u"재", u"쟤"}, {u"재", u"쟤"}, 1.f, CondVowel::none },
	{ {u"저", u"져"}, {u"저", u"져"}, 1.f, CondVowel::none },
	{ {u"제", u"졔"}, {u"제", u"졔"}, 1.f, CondVowel::none },
	{ {u"조", u"죠", u"줘"}, {u"조", u"죠", u"줘"}, 1.f, CondVowel::none },
	{ {u"주", u"쥬"}, {u"주", u"쥬"}, 1.f, CondVowel::none },
	{ {u"차", u"챠"}, {u"차", u"챠"}, 1.f, CondVowel::none },
	{ {u"채", u"챼"}, {u"채", u"챼"}, 1.f, CondVowel::none },
	{ {u"처", u"쳐"}, {u"처", u"쳐"}, 1.f, CondVowel::none },
	{ {u"체", u"쳬"}, {u"체", u"쳬"}, 1.f, CondVowel::none },
	{ {u"초", u"쵸", u"춰"}, {u"초", u"쵸", u"춰"}, 1.f, CondVowel::none },
	{ {u"추", u"츄"}, {u"추", u"츄"}, 1.f, CondVowel::none },
	{ {u"유", u"류"}, {u"유", u"류"}, 1.f, CondVowel::none },
	{ {u"므", u"무"}, {u"므", u"무"}, 1.f, CondVowel::none },
	{ {u"브", u"부"}, {u"브", u"부"}, 1.f, CondVowel::none },
	{ {u"프", u"푸"}, {u"프", u"푸"}, 1.f, CondVowel::none },
	{ {u"르", u"루"}, {u"르", u"루"}, 1.f, CondVowel::none },
	{ {u"러", u"뤄"}, {u"러", u"뤄"}, 1.f, CondVowel::none },
	{ {u"ᆩ", u"ᆪ"}, {u"ᆨ", u"ᆩ", u"ᆪ"}, 1.5f, CondVowel::none },
	{ {u"ᆬ", u"ᆭ"}, {u"ᆫ", u"ᆬ", u"ᆭ"}, 1.5f, CondVowel::none },
	{ {u"ᆰ", u"ᆱ", u"ᆲ", u"ᆳ", u"ᆴ", u"ᆵ", u"ᆶ"}, {u"ᆯ", u"ᆰ", u"ᆱ", u"ᆲ", u"ᆳ", u"ᆴ", u"ᆵ", u"ᆶ"}, 1.5f, CondVowel::none },
	{ {u"ᆺ", u"ᆻ"}, {u"ᆺ", u"ᆻ"}, 1.f, CondVowel::none },
	{ {u"안"}, {u"않"}, 1.5f, CondVowel::none },
	{ {u"맞추", u"맞히"}, {u"맞추", u"맞히"}, 1.5f, CondVowel::none },
	{ {u"맞춰", u"맞혀"}, {u"맞춰", u"맞혀"}, 1.5f, CondVowel::none },
	{ {u"던", u"든"}, {u"던", u"든"}, 1.f, CondVowel::none },
	{ {u"때", u"데"}, {u"때", u"데"}, 1.5f, CondVowel::none },
	{ {u"ᆮ이", u"지"}, {u"ᆮ이", u"지"}, 1.f, CondVowel::none },
	{ {u"ᆮ여", u"져"}, {u"ᆮ여", u"져"}, 1.f, CondVowel::none },
	{ {u"ᇀ이", u"치"}, {u"ᇀ이", u"치"}, 1.f, CondVowel::none },
	{ {u"ᇀ여", u"쳐"}, {u"ᇀ여", u"쳐"}, 1.f, CondVowel::none },
};
