#include "stdafx.h"
#include "KTrie.h"
#include "KForm.h"
#include "KFeatureTestor.h"
#include "KMemoryKMorphemeNode.h"
#include "KModelMgr.h"
#include "KMemory.h"
#include "Utils.h"

using namespace std;

const KMorpheme * KChunk::getMorpheme(size_t idx, KMorpheme * tmp) const
{
	if (!isStr()) return form->candidate[idx];
	if (!begin && !end) return nullptr;
	tmp->kform = nullptr;
	tmp->tag = KPOSTag::UNKNOWN;
	//tmp->p = (end - begin) * -1.5f - 6.f;
	return tmp;
}

size_t KChunk::getCandSize() const
{
	if (isStr()) return 1;
	return form->candidate.size();
}

KMorphemeNode::~KMorphemeNode()
{
	if (optimaCache) DELETE_IN_POOL(k_vpcf, optimaCache);
}

void KMorphemeNode::makeNewCache()
{
	optimaCache = NEW_IN_POOL(k_vpcf);
}

vector<KGraphNode> KTrie::split(const k_string& str) const
{
	vector<KGraphNode> ret;
	ret.reserve(8);
	ret.emplace_back();
	size_t n = 0;
	vector<const KForm*, pool_allocator<void*>> candidates;
	const KTrie* curTrie = this;
	unordered_map<uint32_t, int> spacePos;
	auto brachOut = [&]()
	{
		if (candidates.empty()) return;
		for (auto cand : candidates)
		{
			size_t nBegin = n - cand->form.size();
			auto it = spacePos.find(nBegin - 1);
			int space = it == spacePos.end() ? 0 : it->second;
			bool isMatched = false;
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
		candidates.clear();
	};

	KPOSTag chrType, lastChrType = KPOSTag::UNKNOWN;
	for (auto c : str)
	{
		// space or special character
		if ((chrType = identifySpecialChr(c)) != KPOSTag::MAX)
		{
			while (curTrie->fail)
			{
				curTrie = curTrie->getFail();
				if (curTrie->val && curTrie->val != (void*)-1)
				{
					if (find(candidates.begin(), candidates.end(), curTrie->val) != candidates.end()) break;
					candidates.emplace_back(curTrie->val);
				}
			}
			brachOut();
			curTrie = this;
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
			}
			// not space
			else
			{
				// insert new node
				if (chrType != lastChrType)
				{
					for (auto& g : ret)
					{
						if (g.lastPos != n) continue;
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
			}
			goto continueFor;
		}
		// normal character
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
				brachOut();
				goto continueFor; // root node has no exact next node, continue
			}
		}
		brachOut();
		// from this, curTrie has exact node
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
	brachOut();
	ret.emplace_back();
	ret.back().lastPos = n;
	for (auto& r : ret)
	{
		if (r.lastPos < n) continue;
		r.addNext(&ret.back());
	}
	return ret;
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
