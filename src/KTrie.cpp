#include <numeric>
#include <kiwi/Types.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Utils.h>
#include "ArchAvailable.h"
#include "KTrie.h"
#include "FeatureTestor.h"
#include "FrozenTrie.hpp"

using namespace std;
using namespace kiwi;

template<ArchType arch>
Vector<KGraphNode> kiwi::splitByTrie(const utils::FrozenTrie<kchar_t, const Form*>& trie, const KString& str, Match matchOptions)
{
	Vector<KGraphNode> ret;
	ret.reserve(8);
	ret.emplace_back();
	size_t n = 0;
	Vector<const Form*> candidates;
	auto* curNode = trie.root();
	auto* nextNode = trie.root();
	UnorderedMap<uint32_t, int> spacePos;
	size_t lastSpecialEndPos = 0, specialStartPos = 0;
	POSTag chrType, lastChrType = POSTag::unknown;
	auto branchOut = [&](bool makeLongMatch = false)
	{
		if (!candidates.empty())
		{
			bool alreadySpecialChrProcessed = false;
			for (auto& cand : candidates)
			{
				size_t nBegin = n - cand->form.size();
				auto it = spacePos.find(nBegin - 1);
				int space = it == spacePos.end() ? 0 : it->second;
				bool longestMatched = any_of(ret.begin() + 1, ret.end(), [lastSpecialEndPos, nBegin](const KGraphNode& g)
				{
					return nBegin == g.endPos && lastSpecialEndPos == g.endPos - (g.uform.empty() ? g.form->form.size() : g.uform.size());
				});

				// insert unknown form 
				if (nBegin > lastSpecialEndPos && !longestMatched 
					&& !(0x11A8 <= cand->form[0] && cand->form[0] < (0x11A7 + 28)) 
					&& str[nBegin - space - 1] != 0x11BB) // cannot end with ¤¶
				{
					auto it2 = spacePos.find(lastSpecialEndPos - 1);
					int space2 = it2 == spacePos.end() ? 0 : it2->second;
					KGraphNode newNode{ str.substr(lastSpecialEndPos, nBegin - space - lastSpecialEndPos), (uint16_t)(nBegin - space) };
					for (auto& g : ret)
					{
						if (g.endPos != lastSpecialEndPos - space2) continue;
						newNode.addPrev(&ret.back() + 1 - &g);
					}
					ret.emplace_back(move(newNode));
				}

				// if special character
				if (cand->candidate[0] <= trie.value((size_t)POSTag::sn)->candidate[0])
				{
					// special character should be processed one by one chr.
					if (!alreadySpecialChrProcessed)
					{
						auto it = spacePos.find(n - 2);
						space = it == spacePos.end() ? 0 : it->second;
						KGraphNode newNode{ cand->form.substr(cand->form.size() - 1), (uint16_t)n };
						for (auto& g : ret)
						{
							if (g.endPos != n - 1 - space) continue;
							newNode.addPrev(&ret.back() + 1 - &g);
						}
						ret.emplace_back(move(newNode));
						ret.back().form = trie.value((size_t)cand->candidate[0]->tag);
						lastSpecialEndPos = n;
						alreadySpecialChrProcessed = true;
					}
				}
				else
				{
					KGraphNode newNode{ cand, (uint16_t)n };
					for (auto& g : ret)
					{
						if (g.endPos != nBegin - space) continue;
						newNode.addPrev(&ret.back() + 1 - &g);
					}
					if (newNode.prevs[0])
					{
						ret.emplace_back(move(newNode));
					}
				}
			}
			candidates.clear();
		}

		bool duplicated = any_of(ret.begin() + 1, ret.end(), [lastSpecialEndPos, n](const KGraphNode& g)
		{
			return n == g.endPos /*&& lastSpecialEndPos == g.endPos - (g.uform.empty() ? g.form->form.size() : g.uform.size())*/;
		});
		if (makeLongMatch && n != lastSpecialEndPos && !duplicated)
		{
			auto it2 = spacePos.find(lastSpecialEndPos - 1);
			int space2 = it2 == spacePos.end() ? 0 : it2->second;
			KGraphNode newNode{ str.substr(lastSpecialEndPos, n - lastSpecialEndPos), (uint16_t)n };
			for (auto& g : ret)
			{
				if (g.endPos != lastSpecialEndPos - space2) continue;
				newNode.addPrev(&ret.back() + 1 - &g);
			}
			ret.emplace_back(move(newNode));
		}
	};

	for (; n < str.size(); ++n)
	{
		auto& c = str[n];
		if (!!matchOptions)
		{
			auto m = matchPattern(str.data() + n, str.data() + str.size(), matchOptions);
			chrType = m.second;
			if (chrType != POSTag::unknown)
			{
				branchOut();
				auto it = spacePos.find(n - 1);
				int space = it == spacePos.end() ? 0 : it->second;
				KGraphNode newNode{ KString{ &c, m.first }, (uint16_t)(n + m.first) };
				for (auto& g : ret)
				{
					if (g.endPos != n - space) continue;
					newNode.addPrev(&ret.back() + 1 - &g);
				}
				ret.emplace_back(move(newNode));
				ret.back().form = trie.value((size_t)chrType);

				n += m.first - 1;
				curNode = trie.root();
				goto continueFor;
			}
		}

		chrType = identifySpecialChr(c);

		if (lastChrType != chrType)
		{
			// sequence of speical characters found
			if (lastChrType != POSTag::max && lastChrType != POSTag::unknown && !isWebTag(lastChrType) /*&& n - specialStartPos > 1*/)
			{
				auto it = spacePos.find(specialStartPos - 1);
				int space = it == spacePos.end() ? 0 : it->second;
				KGraphNode newNode{ KString{ &str[specialStartPos], n - specialStartPos }, (uint16_t)n };
				for (auto& g : ret)
				{
					if (g.endPos != specialStartPos - space) continue;
					newNode.addPrev(&ret.back() + 1 - &g);
				}
				ret.emplace_back(move(newNode));
				ret.back().form = trie.value((size_t)lastChrType);
			}
			specialStartPos = n;
		}

		nextNode = curNode->template nextOpt<arch>(trie, c);
		while (!nextNode) // if curNode has no exact next node, goto fail
		{
			if (curNode->fail())
			{
				curNode = curNode->fail();
				for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
				{
					if (!submatcher->val(trie)) break;
					else if (submatcher->val(trie) != (const Form*)trie.hasSubmatch)
					{
						if (find(candidates.begin(), candidates.end(), submatcher->val(trie)) != candidates.end()) break;
						const Form* cand = submatcher->val(trie);
						while (1)
						{
							if (FeatureTestor::isMatched(&str[0], &str[0] + n + 1 - cand->form.size(), cand->vowel, cand->polar))
							{
								candidates.emplace_back(cand);
							}
							if (cand[0].form != cand[1].form) break;
							++cand;
						}
					}
				}
				nextNode = curNode->template nextOpt<arch>(trie, c);
			}
			else
			{
				branchOut(chrType != POSTag::max);
				
				// the root node has no exact next node, test special chr
				// space
				if (chrType == POSTag::unknown)
				{
					// insert new space
					if (chrType != lastChrType)
					{
						spacePos[n] = 1;
					}
					// extend last space span
					else
					{
						spacePos[n] = spacePos[n - 1] + 1;
					}
					lastSpecialEndPos = n + 1;
				}
				// not space
				else if(chrType != POSTag::max)
				{
					lastSpecialEndPos = n + 1;
				}
				
				goto continueFor; 
			}
		}
		branchOut();
		// from this, curNode has the exact next node
		curNode = nextNode;
		// if it has exit node, a pattern has found
		for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
		{
			if (!submatcher->val(trie)) break;
			else if (submatcher->val(trie) != (const Form*)trie.hasSubmatch)
			{
				if (find(candidates.begin(), candidates.end(), submatcher->val(trie)) != candidates.end()) break;
				const Form* cand = submatcher->val(trie);
				while (1)
				{
					if (FeatureTestor::isMatched(&str[0], &str[0] + n + 1 - cand->form.size(), cand->vowel, cand->polar))
					{
						candidates.emplace_back(cand);
					}
					if (cand[0].form != cand[1].form) break;
					++cand;
				}
			}
		}
	continueFor:
		lastChrType = chrType;
	}

	// sequence of speical characters found
	if (lastChrType != POSTag::max && lastChrType != POSTag::unknown /*&& n - specialStartPos > 1*/)
	{
		auto it = spacePos.find(specialStartPos - 1);
		int space = it == spacePos.end() ? 0 : it->second;
		KGraphNode newNode{ KString{ &str[specialStartPos], n - specialStartPos }, (uint16_t)n };
		for (auto& g : ret)
		{
			if (g.endPos != specialStartPos - space) continue;
			newNode.addPrev(&ret.back() + 1 - &g);
		}
		ret.emplace_back(move(newNode));
		ret.back().form = trie.value((size_t)lastChrType);
	}

	curNode = curNode->fail();
	while (curNode)
	{
		if (curNode->val(trie) && curNode->val(trie) != (const Form*)trie.hasSubmatch)
		{
			if (find(candidates.begin(), candidates.end(), curNode->val(trie)) != candidates.end()) break;
			const Form* cand = curNode->val(trie);
			while (1)
			{
				if (FeatureTestor::isMatched(&str[0], &str[0] + n + 1 - cand->form.size(), cand->vowel, cand->polar))
				{
					candidates.emplace_back(cand);
				}
				if (cand[0].form != cand[1].form) break;
				++cand;
			}
		}
		curNode = curNode->fail();
	}
	branchOut(true);

	ret.emplace_back();
	ret.back().endPos = n;
	auto it = spacePos.find(n - 1);
	int space = it == spacePos.end() ? 0 : it->second;
	for (auto& r : ret)
	{
		if (r.endPos < n - space) continue;
		ret.back().addPrev(&ret.back() - &r);
	}
	return KGraphNode::removeUnconnected(ret);
}

template<ptrdiff_t ...indices>
inline FnSplitByTrie getSplitByTrieFnDispatch(ArchType arch, tp::seq<indices...>)
{
	static FnSplitByTrie table[] = {
		&splitByTrie<static_cast<ArchType>(indices + 1)>...
	};
	return table[static_cast<int>(arch) - 1];
}

struct SplitByTrieGetter
{
	template<std::ptrdiff_t i>
	struct Wrapper
	{
		static constexpr FnSplitByTrie value = &splitByTrie<static_cast<ArchType>(i)>;
	};
};

FnSplitByTrie kiwi::getSplitByTrieFn(ArchType arch)
{
	static tp::Table<FnSplitByTrie, AvailableArch> table{ SplitByTrieGetter{} };
	return table[static_cast<std::ptrdiff_t>(arch)];
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
