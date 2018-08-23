#include "stdafx.h"
#include "KTrie.h"
#include "KFeatureTestor.h"
#include "KModelMgr.h"
#include "KMemory.h"
#include "Utils.h"
#include "logPoisson.h"

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
				if (nBegin != lastSpecialEndPos && !longestMatched && !(0x11A8 <= cand->form[0] && cand->form[0] < (0x11A7 + 28)))
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

	KPOSTag chrType, lastChrType = KPOSTag::UNKNOWN;
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

struct MInfo
{
	KNLangModel::WID wid;
	uint8_t combineSocket;
	KCondVowel condVowel;
	KCondPolarity condPolar;
	uint8_t ownFormId;
	MInfo(KNLangModel::WID _wid = 0, uint8_t _combineSocket = 0,
		KCondVowel _condVowel = KCondVowel::none,
		KCondPolarity _condPolar = KCondPolarity::none,
		uint8_t _ownFormId = 0)
		: wid(_wid), combineSocket(_combineSocket),
		condVowel(_condVowel), condPolar(_condPolar), ownFormId(_ownFormId)
	{}
};
#include "KMemoryTrie.h"

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

//#define DEBUG_PRINT

vector<pair<KGraphNode::pathType, float>> KGraphNode::findBestPath(const vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN)
{
	typedef KNLangModel::WID WID;
	typedef vector<MInfo, pool_allocator<void*>> MInfos;
	typedef vector<pair<MInfos, float>, pool_allocator<void*>> WordLLs;
	vector<WordLLs, pool_allocator<void*>> cache(graph.size());
	const KGraphNode* startNode = &graph.front();
	const KGraphNode* endNode = &graph.back();
	vector<k_string> ownFormList;

	vector<const KMorpheme*> unknownNodeCands, unknownNodeLCands;
	unknownNodeCands.emplace_back(morphBase + (size_t)KPOSTag::NNG + 1);
	unknownNodeCands.emplace_back(morphBase + (size_t)KPOSTag::NNP + 1);

	unknownNodeLCands.emplace_back(morphBase + (size_t)KPOSTag::NNP + 1);

	auto& evalPath = [&](const KGraphNode* node, size_t i, size_t ownFormId, const vector<const KMorpheme*>& cands, bool unknownForm)
	{
		float tMax = -INFINITY;
		for (auto& curMorph : cands)
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
			unordered_multimap<WID, pair<MInfos, float>, hash<WID>, equal_to<WID>, pool_allocator<void*>> maxWidLL;
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
						// always merge <V> <chunk> with the same socket
						if (p.first.back().combineSocket != curMorph->combineSocket || curMorph->chunks)
						{
							continue;
						}
						seq[0] = curMorph->getCombined() - morphBase;
						seq[1] = (p.first.end() - 2)->wid;
						seq[2] = (p.first.end() - 3)->wid;
					}
					else if (curMorph->combineSocket && !curMorph->chunks)
					{
						continue;
					}
					else if (p.first.size() + chSize > 2)
					{
						seq[0] = orgSeq;
						if(p.first.size() > 1) seq[chSize + 1] = (p.first.end() - 2)->wid;
					}


					if (!KFeatureTestor::isMatched(node->uform.empty() ? curMorph->kform : &node->uform, p.first.back().condVowel, p.first.back().condPolar))
					{
						continue;
					}

					if (p.first.size() + chSize > 2)
					{
						c = p.second;
						for (size_t ch = 0; ch < chSize - (p.first.size() == 1 ? 1 : 0); ++ch)
						{
							if (any_of(combSocket.begin() + ch, combSocket.end(), [](uint8_t t) { return !!t; })) continue;
							float ct;
							c += ct = knlm->evaluateLL(&seq[ch], 3);
#ifdef DEBUG_PRINT
							if (ct <= -100)
							{
								cout << knlm->evaluateLL(&seq[ch], 3);
								cout << "@Warn\t";
								cout << morphBase[seq[ch]] << '\t';
								cout << morphBase[seq[ch + 1]] << '\t';
								cout << morphBase[seq[ch + 2]] << '\t';
								cout << endl;
							}
#endif
						}
					}
					else
					{
						c = 0;
					}

					auto itp = maxWidLL.equal_range(seq[chSize + 1]);
					if (std::distance(itp.first, itp.second) < 5)
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

			// if a form of the node is unknown, calculate log poisson distribution for word-tag
			float estimatedLL = 0;
			if (unknownForm)
			{
				if (curMorph->tag == KPOSTag::NNG) estimatedLL = LogPoisson::getLL(4.622955f, node->uform.size());
				else if (curMorph->tag == KPOSTag::NNP) estimatedLL = LogPoisson::getLL(5.177622f, node->uform.size());
				else if (curMorph->tag == KPOSTag::MAG) estimatedLL = LogPoisson::getLL(4.557326f, node->uform.size());
				estimatedLL -= 16;
			}

			for (auto& p : maxWidLL)
			{
				p.second.second += estimatedLL;
				tMax = max(tMax, p.second.second);
				cache[i].emplace_back(p.second);
				if (!curMorph->combineSocket || curMorph->chunks)
				{
					cache[i].back().first.reserve(cache[i].back().first.size() + chSize);
					for (size_t ch = chSize; ch-- > 0;)
					{
						if (ch) cache[i].back().first.emplace_back(seq[ch], combSocket[ch]);
						else
						{
							cache[i].back().first.emplace_back(seq[ch], combSocket[ch], condV, condP, ownFormId);
						}
					}
				}
				else
				{
					cache[i].back().first.back() = MInfo{ seq[0] };
				}
			}
		}
		return tMax;
	};

	// end node
	cache.back().emplace_back(MInfos{ MInfo(1u) }, 0.f);

	// middle nodes
	for (size_t i = graph.size() - 2; i > 0; --i)
	{
		auto* node = &graph[i];
		size_t ownFormId = 0;
		if (!node->uform.empty())
		{
			ownFormList.emplace_back(node->uform);
			ownFormId = ownFormList.size();
		}
		float tMax = -INFINITY;
		
		if (node->form)
		{
			tMax = evalPath(node, i, ownFormId, node->form->candidate, false);
			if (all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const KMorpheme* m)
			{
				return m->combineSocket || m->chunks;
			})) 
			{
				ownFormList.emplace_back(node->form->form);
				ownFormId = ownFormList.size();
				tMax = max(tMax, evalPath(node, i, ownFormId, unknownNodeLCands, true));
			};
		}
		else
		{
			tMax = evalPath(node, i, ownFormId, unknownNodeCands, true);
		}

		// heuristically removing lower ll to speed up
		WordLLs reduced;
		for (auto& c : cache[i])
		{
			if (c.second > tMax - 20.f) reduced.emplace_back(move(c));
		}
		cache[i] = move(reduced);
		sort(cache[i].begin(), cache[i].end(), [](const pair<MInfos, float>& a, const pair<MInfos, float>& b)
		{
			return a.second > b.second;
		});
		size_t remainCnt = max((node->form ? node->form->candidate.size() : 3u) * topN * 2, (size_t)10);
		if (remainCnt < cache[i].size()) cache[i].erase(cache[i].begin() + remainCnt, cache[i].end());

#ifdef DEBUG_PRINT
		for (auto& tt : cache[i])
		{
			cout << tt.second << '\t';
			for (auto it = tt.first.rbegin(); it != tt.first.rend(); ++it)
			{
				cout << morphBase[it->wid] << '\t';
			}
			cout << endl;
		}
		cout << "========" << endl;
#endif
	}

	// start node
	WID seq[3] = { 0, };
	for (size_t i = 0; i < MAX_NEXT; ++i)
	{
		auto* next = startNode->getNext(i);
		if (!next) break;
		for (auto&& p : cache[next - startNode])
		{
			if (p.first.back().combineSocket) continue;
			if (!KFeatureTestor::isMatched(nullptr, p.first.back().condVowel)) continue;
			seq[1] = p.first.back().wid;
			seq[2] = (p.first.end() - 2)->wid;
			float ct;
			float c = (ct = knlm->evaluateLL(seq, 3) + knlm->evaluateLL(seq, 2)) + p.second;
#ifdef DEBUG_PRINT
			if (ct <= -100)
			{
				cout << "@Warn\t";
				cout << morphBase[seq[0]] << '\t';
				cout << morphBase[seq[1]] << '\t';
				cout << morphBase[seq[2]] << '\t';
				cout << endl;
			}
#endif
			cache[0].emplace_back(p.first, c);
		}
	}

	auto& cand = cache[0];
	sort(cand.begin(), cand.end(), [](const pair<MInfos, float>& a, const pair<MInfos, float>& b) { return a.second > b.second; });

#ifdef DEBUG_PRINT
	for (auto& tt : cache[0])
	{
		cout << tt.second << '\t';
		for (auto it = tt.first.rbegin(); it != tt.first.rend(); ++it)
		{
			cout << morphBase[it->wid] << '\t';
		}
		cout << endl;
	}
	cout << "========" << endl;

#endif

	vector<pair<pathType, float>> ret;
	for (size_t i = 0; i < min(topN, cand.size()); ++i)
	{
		pathType mv(cand[i].first.size() - 1);
		transform(cand[i].first.rbegin(), cand[i].first.rend() - 1, mv.begin(), [morphBase, &ownFormList](const MInfo& m)
		{
			if(m.ownFormId)	return make_pair(morphBase + m.wid, ownFormList[m.ownFormId - 1]);
			else return make_pair(morphBase + m.wid, k_string{});
		});
		ret.emplace_back(mv, cand[i].second);
	}
	return ret;
}
