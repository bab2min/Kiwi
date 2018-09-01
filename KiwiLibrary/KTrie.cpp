#include "stdafx.h"
#include "KTrie.h"
#include "KFeatureTestor.h"
#include "KModelMgr.h"
#include "KMemory.h"
#include "Utils.h"

using namespace std;

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
			for (auto& cand : candidates)
			{
				size_t nBegin = n - cand->form.size();
				auto it = spacePos.find(nBegin - 1);
				int space = it == spacePos.end() ? 0 : it->second;
				bool isMatched = false;
				bool longestMatched = any_of(ret.begin() + 1, ret.end(), [lastSpecialEndPos, nBegin](const KGraphNode& g)
				{
					return nBegin == g.lastPos && lastSpecialEndPos == g.lastPos - (g.uform.empty() ? g.form->form.size() : g.uform.size());
				});

				// inserting unknown form 
				if (nBegin > lastSpecialEndPos && !longestMatched && !(0x11A8 <= cand->form[0] && cand->form[0] < (0x11A7 + 28)))
				{
					auto it2 = spacePos.find(lastSpecialEndPos - 1);
					int space2 = it2 == spacePos.end() ? 0 : it2->second;
					for (auto& g : ret)
					{
						if (g.lastPos != lastSpecialEndPos - space2) continue;
						g.addNext(&ret.back() + 1);
					}
					ret.emplace_back(str.substr(lastSpecialEndPos, nBegin - space - lastSpecialEndPos), nBegin - space);
				}

				// if special character
				if (cand->candidate[0] <= this[(size_t)KPOSTag::SN].val->candidate[0])
				{
					// reuse previous node
					if (ret.back().lastPos == n - 1 && ret.back().form && ret.back().form->candidate[0]->tag == cand->candidate[0]->tag)
					{
						ret.back().uform.push_back(cand->form.back());
						ret.back().lastPos = n;
						lastSpecialEndPos = n;
					}
					else
					{
						for (auto& g : ret)
						{
							if (g.lastPos != n - 1 - space) continue;
							g.addNext(&ret.back() + 1);
						}
						ret.emplace_back(cand->form.substr(cand->form.size() - 1), n);
						ret.back().form = this[(size_t)cand->candidate[0]->tag].val;
						lastSpecialEndPos = n;
					}
				}
				else
				{
					for (auto& g : ret)
					{
						if (g.lastPos != nBegin - space) continue;
						isMatched = true;
						g.addNext(&ret.back() + 1);
					}
					if (isMatched)
					{
						ret.emplace_back(cand, n);
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
			for (auto& g : ret)
			{
				if (g.lastPos != lastSpecialEndPos - space2) continue;
				g.addNext(&ret.back() + 1);
			}
			ret.emplace_back(str.substr(lastSpecialEndPos, n - lastSpecialEndPos), n);
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
						for (auto& g : ret)
						{
							if (g.lastPos != n - space) continue;
							g.addNext(&ret.back() + 1);
						}
						ret.emplace_back(k_string{ &c, 1 }, n + 1);
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
	ret.emplace_back();
	ret.back().lastPos = n;
	auto it = spacePos.find(n - 1);
	int space = it == spacePos.end() ? 0 : it->second;
	for (auto& r : ret)
	{
		if (r.lastPos < n - space) continue;
		r.addNext(&ret.back());
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
	writeToBinStream<uint16_t>(os, next.size());
	for (auto& p : next)
	{
		writeToBinStream(os, p);
	}
	writeToBinStream(os, fail);
	uint32_t fVal = (val == nullptr || val == (KForm*)-1) ? (size_t)val - 1 : val - base;
	writeToBinStream(os, fVal);
}

KTrie KTrie::loadFromBin(std::istream & is, const KForm* base)
{
	KTrie t;
	uint16_t len = readFromBinStream<uint16_t>(is);
	for (size_t i = 0; i < len; ++i)
	{
		t.next.emplace(readFromBinStream<char16_t, int32_t>(is));
	}
	readFromBinStream(is, t.fail);
	uint32_t fVal = readFromBinStream<uint32_t>(is);
	t.val = (fVal == (uint32_t)-1 || fVal == (uint32_t)-2) ? (KForm*)((int32_t)fVal + 1) : fVal + base;
	return t;
}

vector<KGraphNode> KGraphNode::removeUnconnected(const vector<KGraphNode>& graph)
{
	vector<uint16_t> connectedList(graph.size()), newIndexDiff(graph.size());
	connectedList[graph.size() - 1] = true;
	size_t connectedCnt = 1;
	for (size_t i = graph.size() - 1; i-- > 0; )
	{
		bool connected = false;
		for (size_t j = 0; j < KGraphNode::MAX_NEXT; ++j)
		{
			if (!graph[i].nexts[j]) break;
			if (connectedList[i + graph[i].nexts[j]])
			{
				connected = true;
				++connectedCnt;
				break;
			}
		}
		connectedList[i] = connected;
	}
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
		for (size_t j = 0; j < MAX_NEXT; ++j)
		{
			auto idx = newNode.nexts[j];
			if (!idx) break;
			if(connectedList[i + idx]) newNode.nexts[n++] = newNode.nexts[j] - (newIndexDiff[i + idx] - newIndexDiff[i]);
		}
		newNode.nexts[n] = 0;
	}
	return ret;
}
