#include <numeric>
#include <kiwi/Types.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Utils.h>
#include "ArchAvailable.h"
#include "KTrie.h"
#include "FeatureTestor.h"
#include "FrozenTrie.hpp"
#include "StrUtils.h"

using namespace std;
using namespace kiwi;

template<class... Args>
inline bool appendNewNode(Vector<KGraphNode>& nodes, Vector<Vector<uint32_t>>& endPosMap, size_t startPos, Args&&... args)
{
	size_t newId = nodes.size();
	nodes.emplace_back(forward<Args>(args)...);
	auto& nnode = nodes.back();

	for (auto i : endPosMap[startPos])
	{
		nnode.addPrev(newId - i);
	}
	if (!nnode.prevs[0])
	{
		nodes.pop_back();
		return false;
	}
	else
	{
		endPosMap[nnode.endPos].emplace_back(newId);
		return true;
	}
}

template<bool typoTolerant>
bool insertCandidates(Vector<const Form*>& candidates, const Form* foundCand, const Form* formBase, const KString& str, const Vector<uint32_t>& nonSpaces)
{
	if (typoTolerant)
	{
		auto tCand = reinterpret_cast<const TypoForm*>(foundCand);

		if (find(candidates.begin(), candidates.end(), &tCand->form(formBase)) != candidates.end()) return false;

		while (1)
		{
			auto cand = &tCand->form(formBase);
			if (FeatureTestor::isMatchedApprox(&str[0], &str[nonSpaces[nonSpaces.size() - cand->form.size()]], cand->vowel, cand->polar))
			{
				candidates.emplace_back(cand);
			}
			if (tCand[0].hash() != tCand[1].hash()) break;
			++tCand;
		}
	}
	else
	{
		if (find(candidates.begin(), candidates.end(), foundCand) != candidates.end()) return false;

		while (1)
		{
			if (FeatureTestor::isMatchedApprox(&str[0], &str[nonSpaces[nonSpaces.size() - foundCand->form.size()]], foundCand->vowel, foundCand->polar))
			{
				candidates.emplace_back(foundCand);
			}
			if (foundCand[0].formHash != foundCand[1].formHash) break;
			++foundCand;
		}
	}
	return true;
}

template<ArchType arch, bool typoTolerant>
Vector<KGraphNode> kiwi::splitByTrie(
	const Form* formBase,
	const utils::FrozenTrie<kchar_t, const Form*>& trie, 
	const KString& str, 
	Match matchOptions, 
	size_t maxUnkFormSize, 
	size_t spaceTolerance,
	float typoCostWeight
)
{
	Vector<KGraphNode> ret;
	Vector<Vector<uint32_t>> endPosMap(str.size() + 1);
	ret.reserve(8);
	ret.emplace_back();
	endPosMap[0].emplace_back(0);
	size_t n = 0;
	Vector<const Form*> candidates;
	auto* curNode = trie.root();
	auto* nextNode = trie.root();
	Vector<uint32_t> nonSpaces;
	nonSpaces.reserve(str.size());
	size_t lastSpecialEndPos = 0, specialStartPos = 0;
	POSTag chrType, lastChrType = POSTag::unknown, lastMatchedPattern = POSTag::unknown;
	auto branchOut = [&](size_t unkFormEndPos = 0, size_t unkFormEndPosWithSpace = 0)
	{
		if (!candidates.empty())
		{
			bool alreadySpecialChrProcessed = false;
			for (auto& cand : candidates)
			{
				size_t nBegin = nonSpaces.size() - cand->form.size();
				bool longestMatched = any_of(ret.begin() + 1, ret.end(), [&](const KGraphNode& g)
				{
					return nBegin == g.endPos && lastSpecialEndPos == g.endPos - (g.uform.empty() ? g.form->form.size() : g.uform.size());
				});

				// insert unknown form 
				if (maxUnkFormSize
					&& nBegin > lastSpecialEndPos && !longestMatched
					&& !isHangulCoda(cand->form[0])
					&& str[nonSpaces[nBegin - 1]] != 0x11BB) // cannot end with ㅆ
				{
					size_t newNodeLength = nBegin - lastSpecialEndPos;
					if (newNodeLength <= maxUnkFormSize)
					{
						appendNewNode(ret, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[nBegin] - nonSpaces[lastSpecialEndPos]), (uint16_t)nBegin);
					}
				}

				// if special character
				if (cand->candidate[0] <= trie.value((size_t)POSTag::sn)->candidate[0])
				{
					// special character should be processed one by one chr.
					if (!alreadySpecialChrProcessed)
					{
						if (appendNewNode(ret, endPosMap, nonSpaces.size() - 1, cand->form.substr(cand->form.size() - 1), (uint16_t)nonSpaces.size()))
						{
							ret.back().form = trie.value((size_t)cand->candidate[0]->tag);
						}
						lastSpecialEndPos = nonSpaces.size();
						alreadySpecialChrProcessed = true;
					}
				}
				else
				{
					size_t lengthWithSpaces = nonSpaces.back() + 1 - nonSpaces[nBegin];
					if (lengthWithSpaces <= cand->form.size() + spaceTolerance)
					{
						appendNewNode(ret, endPosMap, nBegin, cand, (uint16_t)nonSpaces.size());
					}
				}
			}
			candidates.clear();
		}

		bool duplicated = any_of(ret.begin() + 1, ret.end(), [&](const KGraphNode& g)
		{
			size_t startPos = g.endPos - (g.uform.empty() ? g.form->form.size() : g.uform.size());
			return startPos == lastSpecialEndPos && g.endPos == unkFormEndPos;
		});
		if (unkFormEndPos > lastSpecialEndPos && !duplicated)
		{
			appendNewNode(ret, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], unkFormEndPosWithSpace - nonSpaces[lastSpecialEndPos]), (uint16_t)unkFormEndPos);
		}
	};

	for (; n < str.size(); ++n)
	{
		auto& c = str[n];

		{
			auto m = matchPattern(str.data() + n, str.data() + str.size(), matchOptions);
			chrType = m.second;
			if (chrType != POSTag::unknown)
			{
				if (lastChrType != POSTag::unknown)
				{
					// sequence of speical characters found
					if (lastChrType != POSTag::max && !isWebTag(lastChrType))
					{
						if (appendNewNode(ret, endPosMap, specialStartPos, KString{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }, (uint16_t)nonSpaces.size()))
						{
							ret.back().form = trie.value((size_t)lastChrType);
						}
					}
					lastSpecialEndPos = specialStartPos;
					specialStartPos = nonSpaces.size();
				}

				branchOut(nonSpaces.size(), n);
				if (appendNewNode(ret, endPosMap, nonSpaces.size(), KString{ &c, m.first }, (uint16_t)(nonSpaces.size() + m.first)))
				{
					ret.back().form = trie.value((size_t)chrType);
				}

				for (size_t i = 0; i < m.first; ++i)
				{
					nonSpaces.emplace_back(n + i);
				}

				n += m.first - 1;
				curNode = trie.root();
				lastMatchedPattern = m.second;
				// SN태그 패턴 매칭의 경우 Web태그로 치환하여 Web와 동일하게 처리되도록 한다
				if (chrType == POSTag::sn)
				{
					chrType = POSTag::w_url;
					lastMatchedPattern = POSTag::w_url;
				}
				goto continueFor;
			}
		}

		chrType = identifySpecialChr(c);

		if (lastChrType != chrType)
		{
			// sequence of speical characters found
			if (lastChrType != POSTag::max && lastChrType != POSTag::unknown && lastChrType != lastMatchedPattern)
			{
				bool duplicated = any_of(ret.begin() + 1, ret.end(), [&](const KGraphNode& g)
				{
					return nonSpaces.size() == g.endPos;
				});
				if (nonSpaces.size() > lastSpecialEndPos && specialStartPos > lastSpecialEndPos && !duplicated)
				{
					appendNewNode(ret, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[specialStartPos] - nonSpaces[lastSpecialEndPos]), (uint16_t)specialStartPos);
				}
				if (appendNewNode(ret, endPosMap, specialStartPos, KString{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }, (uint16_t)nonSpaces.size()))
				{
					ret.back().form = trie.value((size_t)lastChrType);
				}
			}
			lastSpecialEndPos = specialStartPos;
			specialStartPos = nonSpaces.size();
		}
		lastMatchedPattern = POSTag::unknown;

		// spaceTolerance > 0이면 공백문자를 무시하고 분할 진행
		if (spaceTolerance > 0 && chrType == POSTag::unknown)
		{
			branchOut(nonSpaces.size(), n);
			lastSpecialEndPos = nonSpaces.size();
			goto continueFor;
		}

		nextNode = curNode->template nextOpt<arch>(trie, c);
		while (!nextNode) // if curNode has no exact next node, goto fail
		{
			if (curNode->fail())
			{
				curNode = curNode->fail();
				for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
				{
					const Form* cand = submatcher->val(trie);
					if (!cand) break;
					else if (cand != (const Form*)trie.hasSubmatch)
					{
						if (!insertCandidates<typoTolerant>(candidates, cand, formBase, str, nonSpaces)) break;
					}
				}
				nextNode = curNode->template nextOpt<arch>(trie, c);
			}
			else
			{
				if (chrType != POSTag::max)
				{
					branchOut(specialStartPos, specialStartPos < nonSpaces.size() ? nonSpaces[specialStartPos] : n);
				}
				else
				{
					branchOut();
				}
				
				// spaceTolerance == 0이고 공백 문자인 경우
				if (chrType == POSTag::unknown)
				{
					lastSpecialEndPos = nonSpaces.size();
				}
				// 그 외의 경우
				else
				{
					nonSpaces.emplace_back(n);
					if (chrType != POSTag::max)
					{
						lastSpecialEndPos = nonSpaces.size();
					}
				}
				
				goto continueFor; 
			}
		}
		if (chrType != POSTag::max)
		{
			branchOut(specialStartPos, specialStartPos < nonSpaces.size() ? nonSpaces[specialStartPos] : n);
		}
		else
		{
			branchOut();
		}
		
		nonSpaces.emplace_back(n);

		// from this, curNode has the exact next node
		curNode = nextNode;
		// if it has exit node, a pattern has found
		for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
		{
			const Form* cand = submatcher->val(trie);
			if (!cand) break;
			else if (cand != (const Form*)trie.hasSubmatch)
			{
				if (!insertCandidates<typoTolerant>(candidates, cand, formBase, str, nonSpaces)) break;
			}
		}
	continueFor:
		lastChrType = chrType;
	}

	// sequence of speical characters found
	if (lastChrType != POSTag::max && lastChrType != POSTag::unknown && !isWebTag(lastChrType))
	{
		bool duplicated = any_of(ret.begin() + 1, ret.end(), [&](const KGraphNode& g)
		{
			return nonSpaces.size() == g.endPos;
		});
		if (nonSpaces.size() > lastSpecialEndPos && specialStartPos > lastSpecialEndPos  && !duplicated)
		{
			appendNewNode(ret, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[specialStartPos] - nonSpaces[lastSpecialEndPos]), (uint16_t)specialStartPos);
		}
		if (appendNewNode(ret, endPosMap, specialStartPos, KString{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }, (uint16_t)nonSpaces.size()))
		{
			ret.back().form = trie.value((size_t)lastChrType);
		}
	}
	lastSpecialEndPos = specialStartPos;

	curNode = curNode->fail();
	while (curNode)
	{
		if (curNode->val(trie) && curNode->val(trie) != (const Form*)trie.hasSubmatch)
		{
			const Form* cand = curNode->val(trie);
			if (!insertCandidates<typoTolerant>(candidates, cand, formBase, str, nonSpaces)) break;
		}
		curNode = curNode->fail();
	}
	branchOut(nonSpaces.size(), n);

	appendNewNode(ret, endPosMap, nonSpaces.size(), nullptr, nonSpaces.size());

	nonSpaces.emplace_back(n);

	ret = KGraphNode::removeUnconnected(ret);
	for (size_t i = 1; i < ret.size() - 1; ++i)
	{
		auto& r = ret[i];
		r.startPos = r.endPos - (r.uform.empty() ? r.form->form.size() : r.uform.size());
		r.startPos = nonSpaces[r.startPos];
		r.endPos = nonSpaces[r.endPos - 1] + 1;
	}
	ret.back().startPos = ret.back().endPos = nonSpaces[ret.back().endPos];
	return ret;
}

template<ptrdiff_t ...indices>
inline FnSplitByTrie getSplitByTrieFnDispatch(ArchType arch, tp::seq<indices...>)
{
	static FnSplitByTrie table[] = {
		&splitByTrie<static_cast<ArchType>(indices + 1)>...
	};
	return table[static_cast<int>(arch) - 1];
}

template<bool typoTolerant>
struct SplitByTrieGetter
{
	template<std::ptrdiff_t i>
	struct Wrapper
	{
		static constexpr FnSplitByTrie value = &splitByTrie<static_cast<ArchType>(i), typoTolerant>;
	};
};

FnSplitByTrie kiwi::getSplitByTrieFn(ArchType arch, bool typoTolerant)
{
	static tp::Table<FnSplitByTrie, AvailableArch> table{ SplitByTrieGetter<false>{} };
	static tp::Table<FnSplitByTrie, AvailableArch> tableTT{ SplitByTrieGetter<true>{} };
	
	if (typoTolerant)
	{
		return tableTT[static_cast<std::ptrdiff_t>(arch)];
	}
	else
	{
		return table[static_cast<std::ptrdiff_t>(arch)];
	}
}

const Form * KTrie::findForm(const KString & str) const
{
	const KTrie* curTrie = this;
	for (auto c : str)
	{
		if (!curTrie->getNext(c)) return nullptr;
		curTrie = curTrie->getNext(c);
	}
	if (curTrie->val != (void*)-1) return curTrie->val;
	return nullptr;
}

Vector<KGraphNode> KGraphNode::removeUnconnected(const Vector<KGraphNode>& graph)
{
	Vector<uint16_t> connectedList(graph.size()), newIndexDiff(graph.size());
	connectedList[graph.size() - 1] = true;
	connectedList[0] = true;
	// forward searching
	for (size_t i = 1; i < graph.size(); ++i)
	{
		bool connected = false;
		for (size_t j = 0; j < KGraphNode::max_prev; ++j)
		{
			if (!graph[i].prevs[j]) break;
			if (connectedList[i - graph[i].prevs[j]])
			{
				connected = true;
				break;
			}
		}
		connectedList[i] = connected;
	}
	// backward searching
	for (size_t i = graph.size() - 1; i-- > 1; )
	{
		bool connected = false;
		for (size_t j = i + 1; j < graph.size(); ++j)
		{
			for (size_t k = 0; k < KGraphNode::max_prev; ++k)
			{
				if (!graph[j].prevs[k]) break;
				if (j - graph[j].prevs[k] > i) break;
				if (j - graph[j].prevs[k] < i) continue;
				if (connectedList[j])
				{
					connected = true;
					break;
				}
			}
		}
		connectedList[i] = connectedList[i] && connected;
	}
	size_t connectedCnt = accumulate(connectedList.begin(), connectedList.end(), 0);
	newIndexDiff[0] = connectedList[0];
	for (size_t i = 1; i < graph.size(); ++i)
	{
		newIndexDiff[i] = newIndexDiff[i - 1] + connectedList[i];
	}
	for (size_t i = 0; i < graph.size(); ++i)
	{
		newIndexDiff[i] = i + 1 - newIndexDiff[i];
	}

	Vector<KGraphNode> ret;
	ret.reserve(connectedCnt);
	for (size_t i = 0; i < graph.size(); ++i)
	{
		if (!connectedList[i]) continue;
		ret.emplace_back(graph[i]);
		auto& newNode = ret.back();
		size_t n = 0;
		for (size_t j = 0; j < max_prev; ++j)
		{
			auto idx = newNode.prevs[j];
			if (!idx) break;
			if(connectedList[i - idx]) newNode.prevs[n++] = newNode.prevs[j] - (newIndexDiff[i] - newIndexDiff[i - idx]);
		}
		newNode.prevs[n] = 0;
	}
	return ret;
}
