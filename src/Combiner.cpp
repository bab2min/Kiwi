#include <array>
#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <limits>

#include <kiwi/Utils.h>
#include <kiwi/TagUtils.h>
#include "Combiner.h"
#include "FeatureTestor.h"
#include "StrUtils.h"
#include "RaggedVector.hpp"

using namespace std;
using namespace kiwi;
using namespace kiwi::cmb;

ChrSet::ChrSet() = default;
ChrSet::ChrSet(const ChrSet&) = default;
ChrSet::ChrSet(ChrSet&&) noexcept = default;
ChrSet::~ChrSet() = default;
ChrSet& ChrSet::operator=(const ChrSet&) = default;
ChrSet& ChrSet::operator=(ChrSet&&) = default;

Pattern::Pattern() = default;
Pattern::Pattern(const Pattern&) = default;
Pattern::Pattern(Pattern&&) noexcept = default;
Pattern::~Pattern() = default;
Pattern& Pattern::operator=(const Pattern&) = default;
Pattern& Pattern::operator=(Pattern&&) = default;

CompiledRule::CompiledRule() = default;
CompiledRule::CompiledRule(const CompiledRule&) = default;
CompiledRule::CompiledRule(CompiledRule&&) noexcept = default;
CompiledRule::~CompiledRule() = default;
CompiledRule& CompiledRule::operator=(const CompiledRule&) = default;
CompiledRule& CompiledRule::operator=(CompiledRule&&) = default;

RuleSet::RuleSet() = default;
RuleSet::RuleSet(const RuleSet&) = default;
RuleSet::RuleSet(RuleSet&&) noexcept = default;
RuleSet::~RuleSet() = default;
RuleSet& RuleSet::operator=(const RuleSet&) = default;
RuleSet& RuleSet::operator=(RuleSet&&) = default;

namespace kiwi
{
	inline bool hasRange(const ChrSet& set, char16_t b, char16_t e)
	{
		for (auto& p : set.ranges)
		{
			if (p.first <= b && e <= p.second) return !set.negation;
		}
		return set.negation;
	}

	inline Vector<pair<POSTag, uint8_t>> getSubTagset(const string& prefix)
	{
		Vector<pair<POSTag, uint8_t>> ret;
		for (auto pf : split(prefix, ','))
		{
			uint8_t additionalFeature = 0;
			auto chunks = split(pf, '+');
			if (chunks.size() == 1)
			{
			}
			else if (chunks.size() == 2)
			{
				pf = chunks[0];
				if (chunks[1] == "ha")
				{
					additionalFeature = additionalFeatureToMask(POSTag::unknown_feat_ha);
				}
				else
				{
					throw runtime_error{ "unsupported additional feature in rule: " + string{ chunks[1] } };
				}
			}
			else
			{
				throw runtime_error{ "invalid POS tag in rule: " + string{ pf } };
			}

			if (pf == "P")
			{
				ret.emplace_back(POSTag::pv, additionalFeature);
				ret.emplace_back(POSTag::pa, additionalFeature);
				ret.emplace_back(POSTag::pvi, additionalFeature);
				ret.emplace_back(POSTag::pai, additionalFeature);
			}
			else if (pf == "PV")
			{
				ret.emplace_back(POSTag::pv, additionalFeature);
				ret.emplace_back(POSTag::pvi, additionalFeature);
			}
			else if (pf == "PA")
			{
				ret.emplace_back(POSTag::pa, additionalFeature);
				ret.emplace_back(POSTag::pai, additionalFeature);
			}
			else if (pf == "PV-I")
			{
				ret.emplace_back(POSTag::pvi, additionalFeature);
			}
			else if (pf == "PA-I")
			{
				ret.emplace_back(POSTag::pai, additionalFeature);
			}
			else if (pf == "PR")
			{
				ret.emplace_back(POSTag::pv, additionalFeature);
				ret.emplace_back(POSTag::pa, additionalFeature);
			}
			else if (pf == "PI")
			{
				ret.emplace_back(POSTag::pvi, additionalFeature);
				ret.emplace_back(POSTag::pai, additionalFeature);
			}
			else if (pf == "VV-R")
			{
				ret.emplace_back(POSTag::vv, additionalFeature);
			}
			else if (pf == "VA-R")
			{
				ret.emplace_back(POSTag::va, additionalFeature);
			}
			else if (pf == "VX-R")
			{
				ret.emplace_back(POSTag::vx, additionalFeature);
			}
			else
			{
				bool inserted = false;
				for (auto i = POSTag::nng; i < POSTag::p; i = (POSTag)((size_t)i + 1))
				{
					if (strncmp(tagToString(i), pf.data(), pf.size()) == 0)
					{
						ret.emplace_back(i, additionalFeature);
						inserted = true;
					}
				}
				for (auto i : { POSTag::vvi, POSTag::vai, POSTag::vxi, POSTag::xsai })
				{
					if (strncmp(tagToString(i), pf.data(), pf.size()) == 0)
					{
						ret.emplace_back(i, additionalFeature);
						inserted = true;
					}
				}

				if (!inserted)
				{
					throw runtime_error{ "unsupported POS tag in rule: " + string{ pf } };
				}
			}
		}
		return ret;
	}

	inline Vector<uint8_t> getSubFeatset(CondPolarity polar)
	{
		switch (polar)
		{
		case CondPolarity::none:
			return { 0, 1 };
		case CondPolarity::negative:
			return { 0 };
		case CondPolarity::positive:
		default:
			return { 1 };
		}
	}

	inline Vector<uint8_t> getSubFeatset(CondVowel vowel)
	{
		switch (vowel)
		{
		case CondVowel::none:
			return { 0, 2 };
		case CondVowel::non_vowel:
			return { 0 };
		case CondVowel::vowel:
		default:
			return { 2 };
		}
	}

	inline Vector<Dialect> getSubDialectset(Dialect dialect)
	{
		Vector<Dialect> ret;
		if (dialect == Dialect::standard)
		{
			ret.emplace_back(Dialect::standard);
		}
		else
		{
			for (size_t i = 1; i < (size_t)Dialect::lastPlus1; i <<= 1)
			{
				const auto d = (Dialect)i;
				if (!!(dialect & d))
				{
					ret.emplace_back(d);
				}
			}
		}
		return ret;
	}

	template<class Ty, class It>
	inline void inplaceUnion(Vector<Ty>& dest, It first, It last)
	{
		auto mid = dest.size();
		dest.insert(dest.end(), first, last);
		inplace_merge(dest.begin(), dest.begin() + mid, dest.end());
		dest.erase(unique(dest.begin(), dest.end()), dest.end());
	}

	inline ReplString parseReplString(KString&& str)
	{
		bool escape = false;
		size_t leftEnd = -1, rightBegin = 0;
		size_t target = 0;
		float score = 0;
		POSTag parsedAdditionalFeature = POSTag::unknown;
		size_t pos;
		KString additionalFeature;
		for (size_t i = 0; i < str.size(); ++i)
		{
			if (escape)
			{
				switch (str[i])
				{
				case '1':
					str[target++] = 1;
					break;
				case '2':
					str[target++] = 2;
					break;
				case '(':
				case ')':
				case '\\':
				case '+':
				case '-':
					str[target++] = str[i];
					break;
				default:
					str[target++] = '\\';
					str[target++] = str[i];
				}
				escape = false;
			}
			else
			{
				switch (str[i])
				{
				case '\\':
					escape = true;
					break;
				case '(':
					rightBegin = target;
					break;
				case ')':
					leftEnd = target;
					break;
				case '+':
					pos = str.find('-', i + 1);
					if (pos == KString::npos)
					{
						additionalFeature = str.substr(i + 1);
						i = str.size();
					}
					else
					{
						additionalFeature = str.substr(i + 1, pos - (i + 1));
						i = pos - 1;
					}
					break;
				case '-':
					score = stof(str.begin() + i, str.end());
					i = str.size();
					break;
				default:
					str[target++] = str[i];
				}
			}
		}

		if (additionalFeature.empty())
		{
			// no op
		}
		else if (additionalFeature == u"ha")
		{
			parsedAdditionalFeature = POSTag::unknown_feat_ha;
		}
		else
		{
			throw runtime_error{ "unsupported additional feature in replacement string: " + utf16To8(additionalFeature) };
		}
		str.erase(str.begin() + target, str.end());
		return { move(str), leftEnd, rightBegin, score, parsedAdditionalFeature };
	}
}

ChrSet::ChrSet(char16_t chr)
{
	ranges.emplace_back((uint16_t)chr, (uint16_t)chr);
}

Pattern::Pattern(const KString& expr)
{
	static constexpr ptrdiff_t endIdx = numeric_limits<ptrdiff_t>::max();
	struct Group
	{
		ptrdiff_t prev = 0;
		Vector<ptrdiff_t> last;
	};
	Vector<Group> groupStack;
	ptrdiff_t lastNode = 0, beforeNode = 0;
	nodes.emplace_back();
	groupStack.emplace_back();
	groupStack.back().prev = lastNode;
	bool inCharClass = false;
	bool inRange = false;
	ChrSet charClass;

	for (size_t i = 0; i < expr.size(); ++i)
	{
		switch (expr[i])
		{
		case u'(':
			if (inCharClass) goto addIntoClass;
			groupStack.emplace_back();
			groupStack.back().prev = lastNode;
			continue;
		case u')':
			if (inCharClass) goto addIntoClass;
			if (groupStack.size() <= 1) throw runtime_error{ "unpaired closing parenthesis" };
			beforeNode = groupStack.back().prev;
			for (auto& node : nodes)
			{
				auto& nodeNext = node.next;
				auto it = nodeNext.find(endIdx);
				if (it == nodeNext.end()) continue;
				swap(nodeNext[lastNode - (&node - nodes.data())], it->second);
				nodeNext.erase(it);
			}
			groupStack.pop_back();
			continue;
		case u'|':
			if (inCharClass) goto addIntoClass;
			{
				nodes.pop_back();
				groupStack.back().last.emplace_back(lastNode - 1);
				for (auto& node : nodes)
				{
					auto& nodeNext = node.next;
					auto it = nodeNext.find(lastNode - (&node - nodes.data()));
					if (it == nodeNext.end()) continue;
					swap(nodeNext[endIdx], it->second);
					nodeNext.erase(it);
				}
				lastNode = groupStack.back().prev;
			}
			continue;
		case u'[':
			if (inCharClass) goto addIntoClass;
			inCharClass = true;
			inRange = false;
			charClass = {};
			continue;
		case u']':
			if (inCharClass)
			{
				if (inRange) throw runtime_error{ "unpaired range in character class" };
				inCharClass = false;
				goto addSingleNode;
			}
			else
			{
				throw runtime_error{ "unpaired ]" };
			}
			continue;
		case u'^':
			if (inCharClass)
			{
				if (charClass.ranges.empty())
				{
					charClass.negation = true;
				}
				else goto addIntoClass;
			}
			else
			{
				charClass = { bos };
				goto addSingleNode;
			}
			continue;
		case u'$':
			if (inCharClass) goto addIntoClass;
			charClass = { eos };
			goto addSingleNode;
		case u'-':
			if (inCharClass)
			{
				if (charClass.ranges.empty())
				{
					goto addIntoClass;
				}
				else
				{
					inRange = true;
				}
			}
			else
			{
				charClass = { expr[i] };
				goto addSingleNode;
			}
			continue;
		case u'+':
		case u'*':
		case u'?':
			if (inCharClass) goto addIntoClass;
			if (beforeNode == lastNode) throw runtime_error{ "not allowed quantifier position" };

			if (expr[i] == u'*' || expr[i] == u'?')
			{
				nodes[beforeNode].next[lastNode - beforeNode].skippable = true;
			}

			if (expr[i] == u'+' || expr[i] == u'*')
			{
				ptrdiff_t c = (ptrdiff_t)nodes.size();
				nodes[lastNode].next[beforeNode - lastNode].skippable = true;
				nodes[lastNode].next[c - lastNode].skippable = true;
				nodes.emplace_back();
				beforeNode = c;
				lastNode = c;
			}
			continue;
		case u'.':
			if (inCharClass) goto addIntoClass;
			charClass = {};
			charClass.ranges.emplace_back(bos, 0xFFFF);
			goto addSingleNode;
		default:
			if (inCharClass) goto addIntoClass;
			charClass = { expr[i] };
			goto addSingleNode;
		}

		addIntoClass:
		{
			if (inRange)
			{
				if (charClass.ranges.back().first != charClass.ranges.back().second)
				{
					throw runtime_error{ "unpaired range in character class" };
				}

				char16_t leftChr = charClass.ranges.back().first;
				if (leftChr >= expr[i])
				{
					throw runtime_error{ "invalid character range: " + utf8FromCode(leftChr) + "-" + utf8FromCode(expr[i])};
				}
				charClass.ranges.back().second = expr[i];
				inRange = false;
			}
			else
			{
				charClass.ranges.emplace_back(expr[i], expr[i]);
			}
			continue;
		}

		addSingleNode:
		{
			ptrdiff_t c = (ptrdiff_t)nodes.size();
			nodes[lastNode].next[c - lastNode] = move(charClass);
			nodes.emplace_back();
			beforeNode = lastNode;
			lastNode = c;
			continue;
		}
	}

	if (groupStack.size() > 1) throw runtime_error{ "unpaired open parenthesis" };

	for (auto n : groupStack.back().last)
	{
		auto& nodeNext = nodes[n].next;
		auto it = nodeNext.find(endIdx);
		swap(nodeNext[lastNode - n], it->second);
		nodeNext.erase(it);
	}

	for (auto& n : nodes)
	{
		for (auto& nn : n.next)
		{
			if (nn.second.negation)
			{
				nn.second.ranges.emplace_back(ruleSep, ruleSep);
			}
			sort(nn.second.ranges.begin(), nn.second.ranges.end());
		}
	}
}

void Pattern::Node::getEpsilonTransition(ptrdiff_t thisOffset, Vector<ptrdiff_t>& ret) const
{
	ret.emplace_back(thisOffset);
	for (auto& p : next)
	{
		if (p.second.skippable)
		{
			if (find(ret.begin(), ret.end(), p.first + thisOffset) != ret.end()) continue;
			this[p.first].getEpsilonTransition(p.first + thisOffset, ret);
		}
	}
}

Vector<ptrdiff_t> Pattern::Node::getEpsilonTransition(ptrdiff_t thisOffset) const
{
	Vector<ptrdiff_t> ret;
	getEpsilonTransition(thisOffset, ret);
	sort(ret.begin(), ret.end());
	ret.erase(unique(ret.begin(), ret.end()), ret.end());
	return ret;
}

RaggedVector<ptrdiff_t> Pattern::Node::getTransitions(const Vector<char16_t>& vocabs, const Vector<Vector<ptrdiff_t>>& epsilonTable, ptrdiff_t thisOffset) const
{
	RaggedVector<ptrdiff_t> ret;

	for (size_t i = 0; i < vocabs.size(); ++i)
	{
		char16_t b = vocabs[i];
		char16_t e = (i + 1 < vocabs.size()) ? (vocabs[i + 1] - 1) : numeric_limits<char16_t>::max();
		ret.emplace_back();
		for (auto& p : next)
		{
			if (hasRange(p.second, b, e))
			{
				auto& et = epsilonTable[p.first + thisOffset];
				ret.insert_data(et.begin(), et.end());
			}
		}
		sort(ret[i].begin(), ret[i].end());
	}

	return ret;
}

Vector<char16_t> RuleSet::getVocabList(const Vector<Pattern::Node>& nodes)
{
	using CRange = pair<const char16_t*, const char16_t*>;
	Vector<char16_t> ret;
	ret.emplace_back(0);
	ret.emplace_back(1);
	ret.emplace_back(2);
	ret.emplace_back(3);
	Vector<CRange> ranges;

	for (auto& n : nodes)
	{
		for (auto& p : n.next)
		{
			if (p.second.ranges.empty()) continue;
			ranges.emplace_back(
				(const char16_t*)p.second.ranges.data(),
				(const char16_t*)(p.second.ranges.data() + p.second.ranges.size())
			);
		}
	}

	while (!all_of(ranges.begin(), ranges.end(), [](const CRange& x)
	{
		return x.first == x.second;
	}))
	{
		auto& minElem = *min_element(ranges.begin(), ranges.end(), [](const CRange& x, const CRange& y)
		{
			if (x.first == x.second) return false;
			if (y.first == y.second) return true;
			auto xv = *x.first + (x.second - x.first) % 2;
			auto yv = *y.first + (y.second - y.first) % 2;
			return xv < yv;
		});
		char16_t v = *minElem.first + (minElem.second - minElem.first) % 2;
		if (ret.empty() || ret.back() < v)
		{
			ret.emplace_back(v);
		}
		minElem.first++;
	}
	return ret;
}

template<class NodeSizeTy, class GroupSizeTy>
template<class _NodeSizeTy>
auto MultiRuleDFA<NodeSizeTy, GroupSizeTy>::fromOther(MultiRuleDFA<_NodeSizeTy, GroupSizeTy>&& o) -> MultiRuleDFA
{
	MultiRuleDFA ret;
	ret.vocabs = move(o.vocabs);
	ret.finish = move(o.finish);
	ret.finishGroup = move(o.finishGroup);
	ret.groupInfo = move(o.groupInfo);
	ret.sepGroupFlatten = move(o.sepGroupFlatten);
	ret.sepGroupPtrs.insert(ret.sepGroupPtrs.end(), o.sepGroupPtrs.begin(), o.sepGroupPtrs.end());
	ret.transition.insert(ret.transition.end(), o.transition.begin(), o.transition.end());
	return ret;
}

template<class GroupSizeTy>
MultiRuleDFAErased RuleSet::buildRules(
	size_t ruleSize,
	const Vector<Pattern::Node>& nodes,
	const Vector<size_t>& ends,
	const Vector<size_t>& startingGroup,
	const Vector<size_t>& sepPositions,
	Vector<Replacement>&& finish
)
{
	MultiRuleDFA<uint64_t, GroupSizeTy> ret;
	ret.finish = move(finish);

	Vector<Vector<ptrdiff_t>> epsilonTable;
	for (auto& n : nodes)
	{
		epsilonTable.emplace_back(n.getEpsilonTransition(&n - nodes.data()));
	}

	ret.vocabs = getVocabList(nodes);
	Vector<RaggedVector<ptrdiff_t>> transitionTable;
	for (auto& n : nodes)
	{
		transitionTable.emplace_back(n.getTransitions(ret.vocabs, epsilonTable, &n - nodes.data()));
	}

	UnorderedMap<Vector<ptrdiff_t>, size_t> dfaIdxMapper;
	Vector<Vector<ptrdiff_t>> dfaInvIdx;
	size_t visited = 0;
	dfaInvIdx.emplace_back(dfaIdxMapper.emplace(Vector<ptrdiff_t>{ { 0 } }, 0).first->first);

	for (size_t visited = 0; visited < dfaInvIdx.size(); ++visited)
	{
		auto p = dfaInvIdx[visited];
		size_t finishGroup = -1;
		for (auto& e : ends)
		{
			if (binary_search(p.begin(), p.end(), e))
			{
				finishGroup = (size_t)(&e - ends.data());
				break;
			}
		}
		ret.finishGroup.emplace_back(finishGroup);
		ret.groupInfo.emplace_back(ruleSize);
		for (auto i : p)
		{
			if (startingGroup[i] == (size_t)-1) continue;
			ret.groupInfo.back().set(startingGroup[i]);
		}

		ret.sepGroupPtrs.emplace_back(ret.sepGroupFlatten.size());
		for (auto& e : sepPositions)
		{
			if (binary_search(p.begin(), p.end(), e))
			{
				ret.sepGroupFlatten.emplace_back((GroupSizeTy)(&e - sepPositions.data()));
			}
		}

		for (size_t v = 0; v < ret.vocabs.size(); ++v)
		{
			Vector<ptrdiff_t> set;
			if (p[0] == 0 && v != Pattern::ruleSep) // begin node
			{
				set.emplace_back(0);
			}
			if (finishGroup != (size_t)-1) // end node
			{
				set.emplace_back(ends[finishGroup]);
			}

			for (auto i : p)
			{
				inplaceUnion(set, transitionTable[i][v].begin(), transitionTable[i][v].end());
			}

			if (set.empty())
			{
				ret.transition.emplace_back(-1);
			}
			else
			{
				auto inserted = dfaIdxMapper.emplace(move(set), dfaIdxMapper.size());
				if (inserted.second)
				{
					dfaInvIdx.emplace_back(inserted.first->first);
				}
				ret.transition.emplace_back(inserted.first->second);
			}
		}
	}

	if (dfaInvIdx.size() < 0xFF)
	{
		return MultiRuleDFA<uint8_t, GroupSizeTy>::fromOther(move(ret));
	}
	else if (dfaInvIdx.size() < 0xFFFF)
	{
		return MultiRuleDFA<uint16_t, GroupSizeTy>::fromOther(move(ret));
	}
	else if (dfaInvIdx.size() < 0xFFFFFFFF)
	{
		return MultiRuleDFA<uint32_t, GroupSizeTy>::fromOther(move(ret));
	}
	return ret;
}

template<class GroupSizeTy>
MultiRuleDFAErased RuleSet::buildRules(const Vector<Rule>& rules)
{
	Vector<Pattern::Node> nodes(1);
	Vector<size_t> ends, startingGroup, sepPositions;
	Vector<Replacement> finish;

	for (auto& r : rules)
	{
		ptrdiff_t size = nodes.size() - 1;
		for (auto& p : r.left.nodes[0].next)
		{
			nodes[0].next[p.first + size] = p.second;
		}
		nodes.insert(nodes.end(), r.left.nodes.begin() + 1, r.left.nodes.end());
		nodes.back().next[1] = { Pattern::ruleSep };
		sepPositions.emplace_back(nodes.size());
		nodes.insert(nodes.end(), r.right.nodes.begin(), r.right.nodes.end());
		startingGroup.resize(nodes.size(), -1);
		for (auto& p : r.left.nodes[0].next)
		{
			startingGroup[p.first + size] = finish.size();
		}
		ends.emplace_back(nodes.size() - 1);
		finish.emplace_back(r.repl);
	}

	auto ret = buildRules<GroupSizeTy>(rules.size(), nodes, ends, startingGroup, sepPositions, move(finish));
	return ret;
}

MultiRuleDFAErased RuleSet::buildRules(const Vector<Rule>& rules)
{
	if (rules.size() <= 0xFF)
	{
		return buildRules<uint8_t>(rules);
	}
	else if (rules.size() <= 0xFFFF)
	{
		return buildRules<uint16_t>(rules);
	}
	else if (rules.size() <= 0xFFFFFFFF)
	{
		return buildRules<uint32_t>(rules);
	}
	else
	{
		return buildRules<uint64_t>(rules);
	}
}

MultiRuleDFAErased RuleSet::buildRightPattern(const Vector<Rule>& rules)
{
	Vector<Pattern::Node> nodes(1);
	Vector<size_t> ends, startingGroup;

	for (auto& r : rules)
	{
		ptrdiff_t size = nodes.size();
		nodes[0].next[size] = { Pattern::bos };
		nodes.insert(nodes.end(), r.right.nodes.begin(), r.right.nodes.end());
		startingGroup.resize(nodes.size(), -1);
		ends.emplace_back(nodes.size() - 1);
	}

	auto ret = buildRules<uint8_t>(rules.size(), nodes, ends, startingGroup);
	return ret;
}

void RuleSet::addRule(const string& lTag, const string& rTag,
	const KString& lPat, const KString& rPat, const vector<ReplString>& results,
	CondVowel leftVowel, CondPolarity leftPolar, bool ignoreRCond, int lineNo,
	Dialect dialect
)
{
	auto lTags = getSubTagset(lTag);
	auto rTags = getSubTagset(rTag);
	if (lTags.empty() || rTags.empty()) return;

	bool broadcastableVowel = false;
	char16_t lPatVowel = 0;
	Vector<char16_t> resultsVowel;
	if (isHangulVowel(lPat[0]))
	{
		lPatVowel = lPat[0] - u'ㅏ';
		for (auto& r : results)
		{
			if (!isHangulVowel(r.str[0]))
			{
				throw runtime_error{ "invalid Hangul composition: left=" + utf16To8(joinHangul(lPat)) + ", result=" + utf16To8(joinHangul(r.str)) };
			}
			resultsVowel.emplace_back(r.str[0] - u'ㅏ');
		}
		broadcastableVowel = true;
	}

	const size_t ruleId = rules.size();
	if (broadcastableVowel)
	{
		auto bPat = lPat;
		Vector<ReplString> bResults{ results.begin(), results.end() };

		for (size_t onset = 0; onset < 19; ++onset)
		{
			bPat[0] = joinOnsetVowel(onset, lPatVowel);
			for (size_t n = 0; n < results.size(); ++n) bResults[n].str[0] = joinOnsetVowel(onset, resultsVowel[n]);
			rules.emplace_back(bPat, rPat, bResults, dialect, leftVowel, leftPolar, ignoreRCond, lineNo);
		}
	}
	else
	{
		rules.emplace_back(lPat, rPat, Vector<ReplString>{ results.begin(), results.end() }, 
			dialect, leftVowel, leftPolar, ignoreRCond, lineNo);
	}

	for (auto l : lTags)
	{
		for (auto r : rTags)
		{
			if (r.second)
			{
				throw runtime_error{ "right tag with additional feature is not supported in rule: line " + to_string(lineNo) };
			}

			for (auto f1 : getSubFeatset(leftVowel)) for (auto f2 : getSubFeatset(leftPolar)) for (auto subDialect : getSubDialectset(dialect))
			{
				auto& target = ruleset[RuleCategory{ l.first, r.first, (uint8_t)(f1 | f2 | l.second), subDialect }];
				if (broadcastableVowel)
				{
					for (size_t i = 0; i < 19; ++i)
					{
						target.emplace_back(ruleId + i);
					}
				}
				else
				{
					target.emplace_back(ruleId);
				}
			}
		}
	}
}

void RuleSet::loadRules(istream& istr, Dialect enabledDialects)
{
	using namespace std::literals;

	int lineNo = 0;
	string line;
	string lTag, rTag;
	Dialect dialect = Dialect::standard;
	while (getline(istr, line))
	{
		++lineNo;
		if (line[0] == '#') continue;
		while (!line.empty() && ((uint8_t)line.back() < 0x80) && isSpace(line.back())) line.pop_back();
		if (line.empty()) continue;

		auto fields = split(line, '\t');
		if (fields.size() < 2)
		{
			throw runtime_error{ "wrong line: " + line };
		}
		else if (fields.size() == 2 
			|| (fields.size() == 3 && fields[2].front() == '<' && fields[2].back() == '>'))
		{
			lTag = fields[0];
			rTag = fields[1];
			if (fields.size() == 3)
			{
				dialect = parseDialects(fields[2].substr(1, fields[2].size() - 2));
			}
			else
			{
				dialect = Dialect::standard;
			}
		}
		else
		{
			CondVowel cv = CondVowel::none;
			CondPolarity cp = CondPolarity::none;
			bool ignoreRCond = false;
			if (fields.size() >= 4)
			{
				static array<string_view, 5> fs = {
					"+positive"sv,
					"-positive"sv,
					"+coda"sv,
					"-coda"sv,
					"+ignorercond"sv,
				};

				transform(fields[3].begin(), fields[3].end(), const_cast<char*>(fields[3].data()), static_cast<int(*)(int)>(tolower));
				for (auto f : split(fields[3], ','))
				{
					size_t t = find(fs.begin(), fs.end(), f) - fs.begin();
					if (t >= fs.size())
					{
						throw runtime_error{ "invalid feature value: " + string{ f } };
					}

					switch (t)
					{
					case 0:
						cp = CondPolarity::positive;
						break;
					case 1:
						cp = CondPolarity::negative;
						break;
					case 2:
						cv = CondVowel::non_vowel;
						break;
					case 3:
						cv = CondVowel::vowel;
						break;
					case 4:
						ignoreRCond = true;
						break;
					}
				}
			}

			vector<ReplString> repl;
			auto normalizedResult = normalizeHangul(fields[2]);
			for (auto r : split(normalizedResult, u','))
			{
				repl.emplace_back(parseReplString(KString{ r.begin(), r.end() }));
			}

			if (dialect != Dialect::standard && !(enabledDialects & dialect))
			{
				continue;
			}

			addRule(lTag, rTag,
				normalizeHangul(fields[0]), 
				normalizeHangul(fields[1]), 
				repl,
				cv, 
				cp,
				ignoreRCond,
				lineNo,
				dialect
			);
		}
	}
}

CompiledRule RuleSet::compile() const
{
	CompiledRule ret;
	UnorderedMap<Vector<size_t>, size_t> mapper;

	for (auto& p : ruleset)
	{
		auto inserted = mapper.emplace(p.second, mapper.size());
		if (inserted.second)
		{
			Vector<Rule> rs;
			for (auto i : p.second) rs.emplace_back(rules[i]);
			ret.dfa.emplace_back(buildRules(rs));
			ret.dfaRight.emplace_back(buildRightPattern(rs));
		}
		ret.map.emplace(p.first, inserted.first->second);
	}
	return ret;
}

template<class NodeSizeTy, class GroupSizeTy>
Vector<Result> MultiRuleDFA<NodeSizeTy, GroupSizeTy>::combine(U16StringView left, U16StringView right) const
{
	static constexpr NodeSizeTy no_node = -1;
	static constexpr GroupSizeTy no_group = -1;
	Vector<Result> ret;
	Vector<size_t> capturedLefts(finish.size());
	size_t nidx = 0, cpos = 0, capturedLeft = 0, capturedRight = 0, groupCapturedR = no_group;
	const size_t vsize = vocabs.size();

	nidx = transition[nidx * vsize + Pattern::bos];
	groupInfo[nidx].visit([&](size_t i)
	{
		capturedLefts[i] = cpos;
	});

	for (auto c : left)
	{
		if (nidx == no_node) goto not_matched;
		size_t v = (size_t)(upper_bound(vocabs.begin(), vocabs.end(), c) - vocabs.begin()) - 1;
		nidx = transition[nidx * vsize + v];
		
		if (nidx != no_node) groupInfo[nidx].visit([&](size_t i)
		{
			capturedLefts[i] = cpos;
		});
		cpos++;
	}

	if (nidx == no_node) goto not_matched;
	nidx = transition[nidx * vsize + Pattern::ruleSep];
	cpos = 0;
	for (auto c : right)
	{
		if (nidx == no_node) goto not_matched;
		size_t v = (size_t)(upper_bound(vocabs.begin(), vocabs.end(), c) - vocabs.begin()) - 1;
		nidx = transition[nidx * vsize + v];
		cpos++;
		if (nidx != no_node && finishGroup[nidx] != no_group)
		{
			if(groupCapturedR != finishGroup[nidx]) capturedRight = cpos;
			groupCapturedR = finishGroup[nidx];
		}
	}
	
	if (nidx == no_node) goto not_matched;
	nidx = transition[nidx * vsize + Pattern::eos];

	if (nidx == no_node || finishGroup[nidx] == no_group) goto not_matched;
	if (capturedRight == 0) capturedRight = cpos;
	capturedLeft = capturedLefts[finishGroup[nidx]];

	for (auto& r : finish[finishGroup[nidx]].repl)
	{
		size_t leftEnd, rightBegin;
		KString t{ left.begin(), left.begin() + capturedLeft };
		if (r.leftEnd == 0) leftEnd = t.size();
		if (r.rightBegin == 0) rightBegin = t.size();
		for (size_t i = 0; i < r.str.size(); ++i)
		{
			auto c = r.str[i];
			switch (c)
			{
			case 1:
				t.insert(t.end(), left.begin() + capturedLeft, left.end());
				break;
			case 2:
				t.insert(t.end(), right.begin(), right.begin() + capturedRight);
				break;
			default:
				t.push_back(c);
				break;
			}
			if (r.leftEnd == i + 1) leftEnd = t.size();
			if (r.rightBegin == i + 1) rightBegin = t.size();
		}
		t.insert(t.end(), right.begin() + capturedRight, right.end());
		ret.emplace_back(
			move(t),
			leftEnd,
			rightBegin,
			finish[finishGroup[nidx]].dialect,
			finish[finishGroup[nidx]].leftVowel,
			finish[finishGroup[nidx]].leftPolarity,
			finish[finishGroup[nidx]].ignoreRCond,
			finish[finishGroup[nidx]].ruleLineNo,
			r.score,
			r.additionalFeature
		);
	}

not_matched:
	return ret;
}

template<class NodeSizeTy, class GroupSizeTy>
Vector<tuple<size_t, size_t, CondPolarity>> MultiRuleDFA<NodeSizeTy, GroupSizeTy>::searchLeftPat(U16StringView left, bool matchRulSep) const
{
	static constexpr NodeSizeTy no_node = -1;
	static constexpr GroupSizeTy no_group = -1;

	Vector<tuple<size_t, size_t, CondPolarity>> ret;
	Vector<size_t> capturedLefts(finish.size());
	size_t nidx = 0, cpos = 0;
	const size_t vsize = vocabs.size();

	nidx = transition[nidx * vsize + Pattern::bos];
	groupInfo[nidx].visit([&](size_t i)
	{
		capturedLefts[i] = cpos;
	});

	for (auto c : left)
	{
		if (nidx == no_node) goto not_matched;
		size_t v = (size_t)(upper_bound(vocabs.begin(), vocabs.end(), c) - vocabs.begin()) - 1;
		nidx = transition[nidx * vsize + v];

		if (nidx != no_node) groupInfo[nidx].visit([&](size_t i)
		{
			capturedLefts[i] = cpos;
		});
		cpos++;
	}

	if (nidx == no_node) goto not_matched;
	if (matchRulSep)
	{
		nidx = transition[nidx * vsize + Pattern::ruleSep];
		if (nidx == no_node) goto not_matched;

		size_t e = nidx + 1 < sepGroupPtrs.size() ? sepGroupPtrs[nidx + 1] : sepGroupFlatten.size();
		for (size_t i = sepGroupPtrs[nidx]; i < e; ++i)
		{
			ret.emplace_back(sepGroupFlatten[i], capturedLefts[sepGroupFlatten[i]], finish[sepGroupFlatten[i]].leftPolarity);
		}
	}
	else
	{
		nidx = transition[nidx * vsize + Pattern::eos];
		if (nidx == no_node) goto not_matched;

		if (finishGroup[nidx] != no_group)
		{
			ret.emplace_back(finishGroup[nidx], 0, CondPolarity::none);
		}
	}

not_matched:
	return ret;
}

namespace kiwi
{
	struct CombineVisitor
	{
		U16StringView left;
		U16StringView right;

		CombineVisitor(U16StringView _left, U16StringView _right)
			: left{ _left }, right{ _right }
		{
		}

		template<class Ty>
		Vector<Result> operator()(const Ty& e) const
		{
			return e.combine(left, right);
		}
	};

	struct SearchLeftVisitor
	{
		U16StringView left;
		bool matchRuleSep;

		SearchLeftVisitor(U16StringView _left, bool _matchRuleSep) : left{ _left }, matchRuleSep{ _matchRuleSep }
		{
		}

		template<class Ty>
		Vector<tuple<size_t, size_t, CondPolarity>> operator()(const Ty& e) const
		{
			return e.searchLeftPat(left, matchRuleSep);
		}
	};
}

uint8_t CompiledRule::toFeature(CondVowel cv, CondPolarity cp)
{
	uint8_t feat = 0;

	switch (cp)
	{
	case CondPolarity::none:
		//feat |= FeatureTestor::isMatched(&l, CondPolarity::positive) ? 1 : 0;
		break;
	case CondPolarity::positive:
		feat |= 1;
		break;
	case CondPolarity::negative:
		feat |= 0;
		break;
	}

	switch (cv)
	{
	case CondVowel::none:
		feat |= 0;
		break;
	case CondVowel::vowel:
		feat |= 2;
		break;
	case CondVowel::non_vowel:
		feat |= 0;
		break;
	default: 
		break;
	}
	return feat;
}

auto CompiledRule::findRule(POSTag leftTag, POSTag rightTag, CondVowel cv, CondPolarity cp, Dialect dialect) const -> decltype(map.end())
{
	return map.find(RuleCategory{ leftTag, rightTag, toFeature(cv, cp), dialect });
}

Vector<KString> CompiledRule::combineImpl(
	U16StringView leftForm, POSTag leftTag,
	U16StringView rightForm, POSTag rightTag,
	CondVowel cv, CondPolarity cp
) const
{
	Vector<KString> ret;
	if (cp == CondPolarity::none)
	{
		cp = FeatureTestor::isMatched(leftForm.data(), leftForm.data() + leftForm.size(), CondPolarity::positive) ? CondPolarity::positive : CondPolarity::negative;
	}

	auto it = findRule(leftTag, rightTag, cv, cp);
	if (it != map.end())
	{
		for (auto& p : visit(CombineVisitor{ leftForm, rightForm }, dfa[it->second]))
		{
			ret.emplace_back(move(p.str));
		}
		if (!ret.empty()) return ret;
	}

	// leftTag가 vv, va인데 일치하는 규칙이 없는 경우, pv, pa로 변경하여 재탐색
	bool irregular = isIrregular(leftTag);
	auto regLeftTag = clearIrregular(leftTag);
	if (regLeftTag == POSTag::vv || regLeftTag == POSTag::va)
	{
		leftTag = setIrregular(regLeftTag == POSTag::vv ? POSTag::pv : POSTag::pa, irregular);
		it = findRule(leftTag, rightTag, cv, cp);
		if (it != map.end())
		{
			for (auto& p : visit(CombineVisitor{ leftForm, rightForm }, dfa[it->second]))
			{
				ret.emplace_back(move(p.str));
			}
			if (!ret.empty()) return ret;
		}
	}

	KString r;
	r.reserve(leftForm.size() + rightForm.size());
	r.insert(r.end(), leftForm.begin(), leftForm.end());
	r.insert(r.end(), rightForm.begin(), rightForm.end());
	ret.emplace_back(r);
	return ret;
}

tuple<KString, size_t, size_t> CompiledRule::combineOneImpl(
	U16StringView leftForm, POSTag leftTag,
	U16StringView rightForm, POSTag rightTag,
	CondVowel cv, CondPolarity cp
) const
{
	if (cp == CondPolarity::none)
	{
		cp = FeatureTestor::isMatched(leftForm.data(), leftForm.data() + leftForm.size(), CondPolarity::positive) ? CondPolarity::positive : CondPolarity::negative;
	}

	auto it = findRule(leftTag, rightTag, cv, cp);
	if (it != map.end())
	{
		for (auto& p : visit(CombineVisitor{ leftForm, rightForm }, dfa[it->second]))
		{
			if(p.score >= 0) return make_tuple(p.str, p.leftEnd, p.rightBegin);
			KString ret;
			ret.reserve(leftForm.size() + rightForm.size());
			ret.insert(ret.end(), leftForm.begin(), leftForm.end());
			ret.insert(ret.end(), rightForm.begin(), rightForm.end());
			return make_tuple(ret, leftForm.size(), leftForm.size());
		}
	}

	// leftTag가 용언 계열인데 일치하는 규칙이 없는 경우, pv, pa로 변경하여 재탐색
	bool irregular = isIrregular(leftTag);
	auto regLeftTag = clearIrregular(leftTag);
	if (regLeftTag == POSTag::vv || regLeftTag == POSTag::va)
	{
		leftTag = setIrregular(regLeftTag == POSTag::vv ? POSTag::pv : POSTag::pa, irregular);
		it = findRule(leftTag, rightTag, cv, cp);
		if (it != map.end())
		{
			for (auto& p : visit(CombineVisitor{ leftForm, rightForm }, dfa[it->second]))
			{
				return make_tuple(p.str, p.leftEnd, p.rightBegin);
			}
		}
	}

	if(isVerbClass(regLeftTag))
	{
		// rightTag가 어미이며 일치하는 규칙이 없고 `어`로 시작하는 형태일 경우
		if (isEClass(rightTag) && rightForm[0] == u'어' && cp == CondPolarity::positive)
		{
			KString ret;
			ret.reserve(leftForm.size() + rightForm.size());
			ret.insert(ret.end(), leftForm.begin(), leftForm.end());
			ret.push_back(u'아'); // `어`를 `아`로 교체하여 삽입
			ret.insert(ret.end(), rightForm.begin() + 1, rightForm.end());
			return make_tuple(ret, leftForm.size(), leftForm.size());
		}
	}
	KString ret;
	ret.reserve(leftForm.size() + rightForm.size());
	ret.insert(ret.end(), leftForm.begin(), leftForm.end());
	ret.insert(ret.end(), rightForm.begin(), rightForm.end());
	return make_tuple(ret, leftForm.size(), leftForm.size());
}

Vector<tuple<size_t, size_t, CondPolarity>> CompiledRule::testLeftPattern(U16StringView leftForm, size_t ruleId) const
{
	return visit(SearchLeftVisitor{ leftForm, true }, dfa[ruleId]);
}

Vector<tuple<size_t, size_t, CondPolarity>> CompiledRule::testRightPattern(U16StringView rightForm, size_t ruleId) const
{
	return visit(SearchLeftVisitor{ rightForm, false }, dfaRight[ruleId]);
}

vector<tuple<size_t, size_t, CondPolarity>> CompiledRule::testLeftPattern(U16StringView leftForm, POSTag leftTag, POSTag rightTag, CondVowel cv, CondPolarity cp) const
{
	vector<tuple<size_t, size_t, CondPolarity>> ret;
	KString l = normalizeHangul(leftForm);
	if (cp == CondPolarity::none)
	{
		cp = FeatureTestor::isMatched(&l, CondPolarity::positive) ? CondPolarity::positive : CondPolarity::negative;
	}

	auto it = findRule(leftTag, rightTag, cv, cp);
	if (it == map.end()) return ret;

	auto p = visit(SearchLeftVisitor{ l, true }, dfa[it->second]);
	ret.insert(ret.end(), p.begin(), p.end());
	return ret;
}

UnorderedMap<tuple<POSTag, uint8_t>, Vector<size_t>> CompiledRule::getRuleIdsByLeftTag() const
{
	UnorderedMap<tuple<POSTag, uint8_t>, Vector<size_t>> ret;
	for (auto& p : map)
	{
		ret[make_tuple(p.first.leftTag, p.first.feature)].emplace_back(p.second);
	}

	for (auto& r : ret)
	{
		sort(r.second.begin(), r.second.end());
		r.second.erase(unique(r.second.begin(), r.second.end()), r.second.end());
	}
	return ret;
}

UnorderedMap<POSTag, Vector<size_t>> CompiledRule::getRuleIdsByRightTag() const
{
	UnorderedMap<POSTag, Vector<size_t>> ret;
	for (auto& p : map)
	{
		ret[p.first.rightTag].emplace_back(p.second);
	}

	for (auto& r : ret)
	{
		sort(r.second.begin(), r.second.end());
		r.second.erase(unique(r.second.begin(), r.second.end()), r.second.end());
	}
	return ret;
}

Vector<Result> CompiledRule::combine(U16StringView leftForm, U16StringView rightForm, size_t ruleId) const
{
	return visit(CombineVisitor{ leftForm, rightForm }, dfa[ruleId]);
}

vector<u16string> CompiledRule::combine(U16StringView leftForm, POSTag leftTag, U16StringView rightForm, POSTag rightTag, CondVowel cv, CondPolarity cp) const
{
	vector<u16string> ret;
	for (auto& r : combineImpl(normalizeHangul(leftForm), leftTag, normalizeHangul(rightForm), rightTag, cv, cp))
	{
		ret.emplace_back(joinHangul(r));
	}
	return ret;
}

vector<u16string> CompiledRule::combine(const char16_t* leftForm, POSTag leftTag, const char16_t* rightForm, POSTag rightTag, CondVowel cv, CondPolarity cp) const
{
	return combine(U16StringView{ leftForm }, leftTag, U16StringView{ rightForm }, rightTag, cv, cp);
}

template<class FormsTy>
void CompiledRule::addAllomorphImpl(const FormsTy& forms, POSTag tag)
{
	size_t ptrBegin = allomorphData.size();
	size_t ptrEnd = allomorphData.size() + forms.size();

	using FormTy = typename FormsTy::value_type;
	vector<const FormTy*> sortedForms;
	for (auto& p : forms) sortedForms.emplace_back(&p);

	// vocalic, priority 순으로 정렬
	sort(sortedForms.begin(), sortedForms.end(), [](const FormTy* a, const FormTy* b)
	{
		if (get<1>(*a) == CondVowel::vocalic && get<1>(*b) != CondVowel::vocalic) return true;
		if (get<1>(*a) != CondVowel::vocalic && get<1>(*b) == CondVowel::vocalic) return false;
		if (get<1>(*a) < get<1>(*b)) return true;
		if (get<1>(*a) > get<1>(*b)) return false;
		return get<2>(*a) > get<2>(*b);
	});

	for (auto p : sortedForms)
	{
		auto normForm = normalizeHangul(U16StringView{ get<0>(*p) });
		allomorphPtrMap[make_pair(normForm, tag)] = make_pair(ptrBegin, ptrEnd);
		allomorphData.emplace_back(normForm, get<1>(*p), get<2>(*p));
	}
}

void CompiledRule::addAllomorph(const vector<tuple<U16StringView, CondVowel, uint8_t>>& forms, POSTag tag)
{
	return addAllomorphImpl(forms, tag);
}
