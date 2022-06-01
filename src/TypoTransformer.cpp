#include <kiwi/TypoTransformer.h>
#include <kiwi/Utils.h>
#include "StrUtils.h"

using namespace std;
using namespace kiwi;

template<bool u16wrap>
TypoCandidates<u16wrap>::TypoCandidates()
	: ptrs(1), pptrs(1)
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
	data.insert(data.end(), first, last);
	ptrs.emplace_back(data.size());
	cost.emplace_back(0);
	pptrs.emplace_back(ptrs.size() - 1);
}

template<bool u16wrap>
template<class It>
void TypoCandidates<u16wrap>::addBranch(It first, It last, float _cost)
{
	data.insert(data.end(), first, last);
	ptrs.emplace_back(data.size());
	cost.emplace_back(_cost);
}

template<bool u16wrap>
void TypoCandidates<u16wrap>::finishBranch()
{
	pptrs.emplace_back(ptrs.size() - 1);
}

template<bool u16wrap>
TypoIterator<u16wrap>::TypoIterator(const TypoCandidates<u16wrap>& _cand)
	: cands{ &_cand }, digit(std::max(_cand.pptrs.size(), (size_t)1) - 1)
{
	while (!valid())
	{
		if (increase()) break;
	}
}

template<bool u16wrap>
TypoIterator<u16wrap>::TypoIterator(const TypoCandidates<u16wrap>& _cand, int)
	: cands{ &_cand }, digit(std::max(_cand.pptrs.size(), (size_t)1) - 1)
{
	if (!digit.empty())
	{
		digit.back() = _cand.pptrs.end()[-1] - _cand.pptrs.end()[-2];
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
	if (digit.empty()) return true;
	if (digit.back() >= cands->pptrs.end()[-1] - cands->pptrs.end()[-2]) return true;

	digit[0]++;
	for (size_t i = 0; i < digit.size() - 1; ++i)
	{
		if (digit[i] < cands->pptrs[i + 1] - cands->pptrs[i]) break;
		digit[i] = 0;
		digit[i + 1]++;
	}
	return digit.back() >= cands->pptrs.end()[-1] - cands->pptrs.end()[-2];
}

template<bool u16wrap>
bool TypoIterator<u16wrap>::valid() const
{
	float cost = 0;
	for (size_t i = 0; i < digit.size(); ++i)
	{
		size_t s = cands->pptrs[i] + digit[i];
		cost += cands->cost[s];
	}
	return cost <= cands->costThreshold;
}

template<bool u16wrap>
auto TypoIterator<u16wrap>::operator*() const -> value_type
{
	KString ret;
	float cost = 0;
	for (size_t i = 0; i < digit.size(); ++i)
	{
		size_t s = cands->pptrs[i] + digit[i];
		cost += cands->cost[s];
		ret.insert(ret.end(), cands->data.begin() + cands->ptrs[s], cands->data.begin() + cands->ptrs[s + 1]);
	}
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

TypoTransformer::TypoTransformer() = default;
TypoTransformer::~TypoTransformer() = default;
TypoTransformer::TypoTransformer(const TypoTransformer&) = default;
TypoTransformer::TypoTransformer(TypoTransformer&&) noexcept = default;
TypoTransformer& TypoTransformer::operator=(const TypoTransformer&) = default;
TypoTransformer& TypoTransformer::operator=(TypoTransformer&&) noexcept = default;

TypoTransformer::TypoTransformer(initializer_list<tuple<const char16_t*, const char16_t*, float>> lst)
{
	for (auto& l : lst)
	{
		addTypo(get<0>(l), get<1>(l), get<2>(l));
	}
}

void TypoTransformer::_addTypo(char16_t orig, char16_t error, float cost)
{
	if (orig == error) cost = 0;
	bool updated = false;
	auto& v = xformMap[orig];
	for (auto& p : v)
	{
		if (p.first == error)
		{
			p.second = min(p.second, cost);
			updated = true;
			break;
		}
	}
	if (!updated)
	{
		v.emplace_back(error, cost);
	}
}

void TypoTransformer::addTypo(char16_t orig, char16_t error, float cost)
{
	if (isHangulVowel(orig) != isHangulVowel(error)) 
		throw invalid_argument{ "`orig` and `error` are both vowel or not. But `orig`=" + utf16To8(orig) + ", `error`=" + utf16To8(error) };

	if (isHangulVowel(orig))
	{
		for (size_t i = 0; i < 19; ++i)
		{
			_addTypo(joinOnsetVowel(i, orig - u'ㅏ'), joinOnsetVowel(i, error - u'ㅏ'), cost);
		}
	}
	else
	{
		_addTypo(orig, error, cost);
	}
}

template<bool u16wrap>
TypoCandidates<u16wrap> TypoTransformer::_generate(const KString& orig) const
{
	TypoCandidates<u16wrap> ret;
	ret.costThreshold = costThreshold;
	size_t last = 0;
	for (size_t i = 0; i < orig.size(); ++i)
	{
		auto it = xformMap.find(orig[i]);
		if (it == xformMap.end()) continue;
		
		if (last < i)
		{
			ret.insertSinglePath(orig.begin() + last, orig.begin() + i);
		}
		
		for (auto& p : it->second)
		{
			ret.addBranch(&p.first, &p.first + 1, p.second);
		}
		ret.finishBranch();
		last = i + 1;
	}
	if (last < orig.size())
	{
		ret.insertSinglePath(orig.begin() + last, orig.end());
	}
	return ret;
}

TypoCandidates<true> TypoTransformer::generate(const u16string& orig) const
{
	return _generate<true>(normalizeHangul(orig));
}

template class TypoCandidates<true>;
template class TypoCandidates<false>;
template class TypoIterator<true>;
template class TypoIterator<false>;

template TypoCandidates<true> TypoTransformer::_generate<true>(const KString&) const;
template TypoCandidates<false> TypoTransformer::_generate<false>(const KString&) const;

const TypoTransformer kiwi::withoutTypo;

const TypoTransformer kiwi::basicTypoSet = {
	{ u"ㅐㅔ", u"ㅐㅔ", 1.f },
	{ u"ㅐㅔ", u"ㅒㅖ", 1.5f },
	{ u"ㅒㅖ", u"ㅐㅔ", 1.5f },
	{ u"ㅒㅖ", u"ㅒㅖ", 1.f },
	{ u"ㅚㅙㅞ", u"ㅚㅙㅞㅐㅔ", 1.f },
	{ u"ㅝ", u"ㅝㅗㅓ", 1.f },
	{ u"ㅟ", u"ㅟㅣ", 1.f },
	{ u"ㅢ", u"ㅢㅣ", 1.f },
	{ u"자쟈", u"자쟈", 1.f },
	{ u"재쟤", u"재쟤", 1.f },
	{ u"저져", u"저져", 1.f },
	{ u"제졔", u"제졔", 1.f },
	{ u"조죠줘", u"조죠줘", 1.f },
	{ u"주쥬", u"주쥬", 1.f },
	{ u"차챠", u"차챠", 1.f },
	{ u"채챼", u"채챼", 1.f },
	{ u"처쳐", u"처쳐", 1.f },
	{ u"체쳬", u"체쳬", 1.f },
	{ u"초쵸춰", u"초쵸춰", 1.f },
	{ u"추츄", u"추츄", 1.f },
	{ u"유류", u"유류", 1.f },
	{ u"므무", u"므무", 1.f },
	{ u"브부", u"브부", 1.f },
	{ u"프푸", u"프푸", 1.f },
	{ u"ᆬᆭ", u"ᆫᆬᆭ", 1.5f },
	{ u"ᆰᆱᆲᆳᆴᆵᆶ", u"ᆯᆰᆱᆲᆳᆴᆵᆶ", 1.5f },
	{ u"ᆺᆻ", u"ᆺᆻ", 1.f },
};
