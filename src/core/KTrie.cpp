#include "KiwiHeader.h"
#include "KTrie.h"
#include "KFeatureTestor.h"
#include "KModelMgr.h"
#include "KMemory.h"
#include "Utils.h"
#include "serializer.hpp"

using namespace std;
using namespace kiwi;

vector<KGraphNode> KTrie::split(const k_string& str) const
{
	vector<KGraphNode> ret;
	ret.reserve(8);
	ret.emplace_back();
	size_t n = 0;
	vector<const KForm*, pool_allocator<const KForm*>> candidates;
	const KTrie* curTrie = this;
	unordered_map<uint32_t, int> spacePos;
	size_t lastSpecialEndPos = 0;
	KPOSTag chrType, lastChrType = KPOSTag::UNKNOWN;
	auto brachOut = [&](bool makeLongMatch = false)
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
					return nBegin == g.lastPos && lastSpecialEndPos == g.lastPos - (g.uform.empty() ? g.form->form.size() : g.uform.size());
				});

				// insert unknown form 
				if (nBegin > lastSpecialEndPos && !longestMatched 
					&& !(0x11A8 <= cand->form[0] && cand->form[0] < (0x11A7 + 28)) 
					&& str[nBegin - space -1] != 0x11BB) // cannot end with ¤¶
				{
					auto it2 = spacePos.find(lastSpecialEndPos - 1);
					int space2 = it2 == spacePos.end() ? 0 : it2->second;
					KGraphNode newNode{ str.substr(lastSpecialEndPos, nBegin - space - lastSpecialEndPos), (uint16_t)(nBegin - space) };
					for (auto& g : ret)
					{
						if (g.lastPos != lastSpecialEndPos - space2) continue;
						newNode.addPrev(&ret.back() + 1 - &g);
					}
					ret.emplace_back(move(newNode));
				}

				// if special character
				if (cand->candidate[0] <= this[(size_t)KPOSTag::SN].val->candidate[0])
				{
					// special character should be processed one by one chr.
					if (!alreadySpecialChrProcessed)
					{
						auto it = spacePos.find(n - 2);
						space = it == spacePos.end() ? 0 : it->second;
						KGraphNode newNode{ cand->form.substr(cand->form.size() - 1), (uint16_t)n };
						for (auto& g : ret)
						{
							if (g.lastPos != n - 1 - space) continue;
							newNode.addPrev(&ret.back() + 1 - &g);
						}
						ret.emplace_back(move(newNode));
						ret.back().form = this[(size_t)cand->candidate[0]->tag].val;
						lastSpecialEndPos = n;
						alreadySpecialChrProcessed = true;
					}
				}
				else
				{
					KGraphNode newNode{ cand, (uint16_t)n };
					for (auto& g : ret)
					{
						if (g.lastPos != nBegin - space) continue;
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
			return n == g.lastPos /*&& lastSpecialEndPos == g.lastPos - (g.uform.empty() ? g.form->form.size() : g.uform.size())*/;
		});
		if (makeLongMatch && n != lastSpecialEndPos && !duplicated)
		{
			auto it2 = spacePos.find(lastSpecialEndPos - 1);
			int space2 = it2 == spacePos.end() ? 0 : it2->second;
			KGraphNode newNode{ str.substr(lastSpecialEndPos, n - lastSpecialEndPos), (uint16_t)n };
			for (auto& g : ret)
			{
				if (g.lastPos != lastSpecialEndPos - space2) continue;
				newNode.addPrev(&ret.back() + 1 - &g);
			}
			ret.emplace_back(move(newNode));
		}
	};

	for (auto c : str)
	{
		chrType = identifySpecialChr(c);
		while (!curTrie->getNext(c)) // if curTrie has no exact next node, goto fail
		{
			if (curTrie->fail)
			{
				curTrie = curTrie->getFail();
				for (auto submatcher = curTrie; submatcher; submatcher = submatcher->getFail())
				{
					if (!submatcher->val) break;
					else if (submatcher->val != (void*)-1)
					{
						if (find(candidates.begin(), candidates.end(), submatcher->val) != candidates.end()) break;
						candidates.emplace_back(submatcher->val);
					}
				}
			}
			else
			{
				brachOut(chrType != KPOSTag::MAX);
				// the root node has no exact next node, test special chr
				// space
				if (chrType == KPOSTag::UNKNOWN)
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
				else if(chrType != KPOSTag::MAX)
				{
					// insert new node
					if (chrType != lastChrType)
					{
						auto it = spacePos.find(n - 1);
						int space = it == spacePos.end() ? 0 : it->second;
						KGraphNode newNode{ k_string{ &c, 1 }, (uint16_t)(n + 1) };
						for (auto& g : ret)
						{
							if (g.lastPos != n - space) continue;
							newNode.addPrev(&ret.back() + 1 - &g);
						}
						ret.emplace_back(move(newNode));
						ret.back().form = this[(size_t)chrType].val;
					}
					// reuse previous node
					else
					{
						ret.back().uform.push_back(c);
						ret.back().lastPos = n + 1;
					}
					lastSpecialEndPos = n + 1;
				}
				goto continueFor; 
			}
		}
		brachOut();
		// from this, curTrie has the exact next node
		curTrie = curTrie->getNext(c);
		// if it has exit node, a pattern has found
		for (auto submatcher = curTrie; submatcher; submatcher = submatcher->getFail())
		{
			if (!submatcher->val) break;
			else if (submatcher->val != (void*)-1)
			{
				if (find(candidates.begin(), candidates.end(), submatcher->val) != candidates.end()) break;
				candidates.emplace_back(submatcher->val);
			}
		}
	continueFor:
		lastChrType = chrType;
		n++;
	}
	while (curTrie->fail)
	{
		curTrie = curTrie->getFail();
		if (curTrie->val && curTrie->val != (void*)-1)
		{
			if (find(candidates.begin(), candidates.end(), curTrie->val) != candidates.end()) break;
			candidates.emplace_back(curTrie->val);
		}
	}
	brachOut(true);

	// join splitted special chr node
	for (size_t i = ret.size() ; i-- > 0; )
	{
		// if special chr
		if (!(ret[i].form && ret[i].form->candidate[0] <= this[(size_t)KPOSTag::SN].val->candidate[0]
			&& ret[i].prevs[0] && !ret[i].prevs[1])) continue;

		auto& joinedNode = ret[i];
		auto curmorph = ret[i].form->candidate[0];
		auto j = i;
		while (ret[j].prevs[0] && !ret[j].prevs[1] && ret[j - ret[j].prevs[0]].form
			&& curmorph == ret[j - ret[j].prevs[0]].form->candidate[0]
			&& joinedNode.lastPos - joinedNode.uform.size() == ret[j - ret[j].prevs[0]].lastPos)
		{
			j -= ret[j].prevs[0];
			joinedNode.uform = ret[j].uform + joinedNode.uform;
			for (size_t k = 0; k < KGraphNode::MAX_PREV; ++k)
			{
				if (!ret[j].prevs[k])
				{
					joinedNode.prevs[k] = 0;
					break;
				}
				joinedNode.prevs[k] = (&joinedNode - &ret[0]) - (j - ret[j].prevs[k]);
			}
		}
	}

	ret.emplace_back();
	ret.back().lastPos = n;
	auto it = spacePos.find(n - 1);
	int space = it == spacePos.end() ? 0 : it->second;
	for (auto& r : ret)
	{
		if (r.lastPos < n - space) continue;
		ret.back().addPrev(&ret.back() - &r);
	}
	return KGraphNode::removeUnconnected(ret);
}

const KForm * KTrie::findForm(const k_string & str) const
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

void KTrie::saveToBin(std::ostream & os, const KForm* base) const
{
	serializer::writeToBinStream<uint16_t>(os, next.size());
	for (auto& p : next)
	{
		serializer::writeToBinStream(os, p);
	}
	serializer::writeToBinStream(os, fail);
	uint32_t fVal = (val == nullptr || val == (KForm*)-1) ? (size_t)val - 1 : val - base;
	serializer::writeToBinStream(os, fVal);
}

KTrie KTrie::loadFromBin(std::istream & is, const KForm* base)
{
	KTrie t;
	uint16_t len = serializer::readFromBinStream<uint16_t>(is);
	for (size_t i = 0; i < len; ++i)
	{
		t.next.emplace(serializer::readFromBinStream<pair<char16_t, int32_t>>(is));
	}
	serializer::readFromBinStream(is, t.fail);
	uint32_t fVal = serializer::readFromBinStream<uint32_t>(is);
	t.val = (fVal == (uint32_t)-1 || fVal == (uint32_t)-2) ? (KForm*)((int32_t)fVal + 1) : fVal + base;
	return t;
}

vector<KGraphNode> KGraphNode::removeUnconnected(const vector<KGraphNode>& graph)
{
	vector<uint16_t> connectedList(graph.size()), newIndexDiff(graph.size());
	connectedList[graph.size() - 1] = true;
	connectedList[0] = true;
	// forward searching
	for (size_t i = 1; i < graph.size(); ++i)
	{
		bool connected = false;
		for (size_t j = 0; j < KGraphNode::MAX_PREV; ++j)
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
			for (size_t k = 0; k < KGraphNode::MAX_PREV; ++k)
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

	vector<KGraphNode> ret;
	ret.reserve(connectedCnt);
	for (size_t i = 0; i < graph.size(); ++i)
	{
		if (!connectedList[i]) continue;
		ret.emplace_back(graph[i]);
		auto& newNode = ret.back();
		size_t n = 0;
		for (size_t j = 0; j < MAX_PREV; ++j)
		{
			auto idx = newNode.prevs[j];
			if (!idx) break;
			if(connectedList[i - idx]) newNode.prevs[n++] = newNode.prevs[j] - (newIndexDiff[i] - newIndexDiff[i - idx]);
		}
		newNode.prevs[n] = 0;
	}
	return ret;
}
