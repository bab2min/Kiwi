#include "stdafx.h"
#include "KTrie.h"
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


vector<pair<vector<const KMorpheme*>, float>> KGraphNode::findBestPath(const vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN)
{
	typedef KNLangModel::WID WID;
	struct MInfo
	{
		WID wid;
		uint8_t combineSocket;
		KCondVowel condVowel;
		KCondPolarity condPolar;
		MInfo(WID _wid = 0, uint8_t _combineSocket = 0, 
			KCondVowel _condVowel = KCondVowel::none, 
			KCondPolarity _condPolar = KCondPolarity::none) 
			: wid(_wid), combineSocket(_combineSocket),
			condVowel(_condVowel), condPolar(_condPolar)
		{}
	};
	typedef vector<MInfo, pool_allocator<void*>> MInfos;
	typedef vector<pair<MInfos, float>> WordLLs;
	vector<WordLLs, pool_allocator<void*>> cache(graph.size());
	const KGraphNode* startNode = &graph.front();
	const KGraphNode* endNode = &graph.back();

	// end node
	cache.back().emplace_back(MInfos{ MInfo(1u) }, 0.f);

	// middle nodes
	for (size_t i = graph.size() - 2; i > 0; --i)
	{
		auto* node = &graph[i];
		float tMin = INFINITY, tMax = -INFINITY;
		for (auto& curMorph : node->form->candidate)
		{
			array<WID, 5> seq = { 0, };
			array<uint8_t, 3> combSocket = { 0, };
			KCondVowel condV = KCondVowel::none;
			KCondPolarity condP = KCondPolarity::none;
			size_t chSize = 1;
			WID orgSeq = 0;
			// if the morpheme is chunk set
			if (curMorph->chunks)
			{
				chSize = curMorph->chunks->size();
				for (size_t i = 0; i < chSize; ++i)
				{
					seq[i] = (*curMorph->chunks)[i] - morphBase;
				}
				combSocket[0] = curMorph->combineSocket;
			}
			else
			{
				seq[0] = curMorph - morphBase;
			}
			orgSeq = seq[0];
			condV = curMorph->vowel;
			condP = curMorph->polar;
			multimap<WID, pair<MInfos, float>> maxWidLL;
			for (size_t i = 0; i < MAX_NEXT; ++i)
			{
				auto* next = node->getNext(i);
				if (!next) break;
				for (auto&& p : cache[next - startNode])
				{
					float c;
					seq[chSize] = p.first.back().wid;
					if (p.first.back().combineSocket)
					{
						if (p.first.back().combineSocket != curMorph->combineSocket)
						{
							continue;
						}
						seq[0] = curMorph->getCombined() - morphBase;
						seq[1] = (p.first.end() - 2)->wid;
						seq[2] = (p.first.end() - 3)->wid;
					}
					else if(p.first.size() > 1)
					{
						seq[0] = orgSeq;
						seq[chSize + 1] = (p.first.end() - 2)->wid;
					}
					
					
					if (!KFeatureTestor::isMatched(curMorph->kform, p.first.back().condVowel, p.first.back().condPolar))
					{
						continue;
					}

					if (p.first.size() > 1)
					{
						c = p.second;
						for (size_t ch = 0; ch < chSize; ++ch)
						{
							if (any_of(combSocket.begin() + ch, combSocket.end(), [](uint8_t t) { return !!t; })) continue;
							c += knlm->evaluateLL(&seq[ch], 3);
						}
					}
					else
					{
						c = 0;
					}

					auto itp = maxWidLL.equal_range(seq[chSize + 1]);
					if (std::distance(itp.first, itp.second) < 3)
					{
						maxWidLL.emplace(seq[chSize + 1], make_pair(p.first, c));
					}
					else
					{
						auto itm = itp.first;
						++itp.first;
						for (; itp.first != itp.second; ++itp.first)
						{
							if (itp.first->second.second < itm->second.second)
							{
								itm = itp.first;
							}
						}
						if (itm->second.second < c) itm->second = make_pair(p.first, c);
					}
				}
			}
			for (auto& p : maxWidLL)
			{
				tMin = min(tMin, p.second.second);
				tMax = max(tMax, p.second.second);
				cache[i].emplace_back(p.second);
				if (!curMorph->combineSocket || curMorph->chunks)
				{
					cache[i].back().first.reserve(cache[i].back().first.size() + chSize);
					for (size_t ch = chSize; ch-- > 0;)
					{
						if(ch) cache[i].back().first.emplace_back(seq[ch], combSocket[ch]);
						else cache[i].back().first.emplace_back(seq[ch], combSocket[ch], condV, condP);
					}
				}
				else
				{
					cache[i].back().first.back() = MInfo{ seq[0] };
				}
			}
		}
		// heuristically removing lower ll to speed up
		WordLLs reduced;
		for (auto& c : cache[i])
		{
			if (c.second > tMax - 10.f) reduced.emplace_back(move(c));
		}
		cache[i] = move(reduced);
	}

	// start node
	WID seq[3] = { 0, };
	for (size_t i = 0; i < MAX_NEXT; ++i)
	{
		auto* next = startNode->getNext(i);
		if (!next) break;
		for (auto&& p : cache[next - startNode])
		{
			seq[1] = p.first.back().wid;
			seq[2] = (p.first.end() - 2)->wid;
			float c = knlm->evaluateLL(seq, 3) + knlm->evaluateLL(seq, 2) + p.second;
			cache[0].emplace_back(p.first, c);
		}
	}

	auto& cand = cache[0];
	sort(cand.begin(), cand.end(), [](const pair<MInfos, float>& a, const pair<MInfos, float>& b) { return a.second > b.second; });
	vector<pair<vector<const KMorpheme*>, float>> ret;
	for (size_t i = 0; i < min(topN, cand.size()); ++i)
	{
		vector<const KMorpheme*> mv(cand[i].first.size() - 1);
		transform(cand[i].first.rbegin(), cand[i].first.rend() - 1, mv.begin(), [morphBase](const MInfo& m)
		{
			return morphBase + m.wid;
		});
		ret.emplace_back(mv, cand[i].second);
	}
	return ret;
}
