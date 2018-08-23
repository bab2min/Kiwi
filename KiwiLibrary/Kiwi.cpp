#include "stdafx.h"
#include "Utils.h"
#include "Kiwi.h"
#include "KFeatureTestor.h"
#include "KModelMgr.h"
#include "logPoisson.h"

using namespace std;

Kiwi::Kiwi(const char * modelPath, size_t _maxCache, size_t _numThread) : maxCache(_maxCache),
	numThread(_numThread ? _numThread : thread::hardware_concurrency()), 
	workerPool(_numThread ? _numThread : thread::hardware_concurrency())
{
	mdl = make_shared<KModelMgr>(modelPath);
}

int Kiwi::addUserWord(const u16string & str, KPOSTag tag)
{
	mdl->addUserWord(normalizeHangul({ str.begin(), str.end() }), tag);
	return 0;
}

int Kiwi::addUserRule(const u16string & str, const vector<pair<u16string, KPOSTag>>& morph)
{
	vector<pair<k_string, KPOSTag>> jmMorph;
	jmMorph.reserve(morph.size());
	for (auto& m : morph)
	{
		jmMorph.emplace_back(normalizeHangul({ m.first.begin(), m.first.end() }), m.second);
	}
	mdl->addUserRule(normalizeHangul({ str.begin(), str.end() }), jmMorph);
	return 0;
}

int Kiwi::loadUserDictionary(const char * userDictPath)
{
	FILE* file = nullptr;
	if (fopen_s(&file, userDictPath, "r")) return -1;
	char buf[4096];
	while (fgets(buf, 4096, file))
	{
		if (buf[0] == '#') continue;
		auto wstr = utf8_to_utf16(buf);
		auto chunks = split(wstr, u'\t');
		if (chunks.size() < 2) continue;
		if (!chunks[1].empty()) 
		{
			auto pos = makePOSTag(chunks[1]);
			if (pos != KPOSTag::MAX)
			{
				addUserWord(chunks[0], pos);
				continue;
			}
		}
		
		vector<pair<u16string, KPOSTag>> morphs;
		for (size_t i = 1; i < chunks.size(); i++) 
		{
			auto cc = split(chunks[i], u'/');
			if (cc.size() != 2) goto loopContinue;
			auto pos = makePOSTag(cc[1]);
			if (pos == KPOSTag::MAX) goto loopContinue;
			morphs.emplace_back(cc[0], pos);
		}
		addUserRule(chunks[0], morphs);
	loopContinue:;
	}
	fclose(file);
	return 0;
}

int Kiwi::prepare()
{
	mdl->solidify();
	kt = mdl->getTrie();
	return 0;
}

//#define DEBUG_PRINT

vector<pair<Kiwi::pathType, float>> Kiwi::findBestPath(const vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN) const
{
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

	typedef KNLangModel::WID WID;
	typedef vector<MInfo, pool_allocator<MInfo>> MInfos;
	typedef vector<pair<MInfos, float>, pool_allocator<pair<MInfos, float>>> WordLLs;
	vector<WordLLs, pool_allocator<WordLLs>> cache(graph.size());
	const KGraphNode* startNode = &graph.front();
	const KGraphNode* endNode = &graph.back();
	vector<k_string> ownFormList;

	vector<const KMorpheme*> unknownNodeCands, unknownNodeLCands;
	unknownNodeCands.emplace_back(morphBase + (size_t)KPOSTag::NNG + 1);
	unknownNodeCands.emplace_back(morphBase + (size_t)KPOSTag::NNP + 1);

	unknownNodeLCands.emplace_back(morphBase + (size_t)KPOSTag::NNP + 1);

	const auto& evalCandidate = [&](const KGraphNode* node, size_t ownFormId, const KMorpheme* curMorph, bool unknownForm)
	{
		float tMax = -INFINITY;
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

		unordered_multimap<WID, pair<const MInfos*, float>, hash<WID>, equal_to<WID>, pool_allocator<void*>> maxWidLL;
		for (size_t i = 0; i < KGraphNode::MAX_NEXT; ++i)
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
					if (p.first.size() > 1) seq[chSize + 1] = (p.first.end() - 2)->wid;
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
					maxWidLL.emplace(seq[chSize + 1], make_pair(&p.first, c));
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
					if (itm->second.second < c) itm->second = make_pair(&p.first, c);
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
		}

		WordLLs nCache;
		for (auto& p : maxWidLL)
		{
			if (p.second.second <= tMax - 10) continue;
			nCache.emplace_back(MInfos{}, p.second.second);
			auto& wids = nCache.back().first;
			wids.reserve(p.second.first->size() + chSize);
			wids = *p.second.first;
			if (!curMorph->combineSocket || curMorph->chunks)
			{
				for (size_t ch = chSize; ch-- > 0;)
				{
					if (ch) wids.emplace_back(seq[ch], combSocket[ch]);
					else wids.emplace_back(seq[ch], combSocket[ch], condV, condP, ownFormId);
				}
			}
			else
			{
				wids.back() = MInfo{ (WID)(curMorph->getCombined() - morphBase) };
			}
		}
		return make_pair(move(nCache), tMax);
	};

	const auto& evalPath = [&, this](const KGraphNode* node, size_t i, size_t ownFormId, const vector<const KMorpheme*>& cands, bool unknownForm)
	{
		float tMax = -INFINITY;
		size_t stride = workerPool.getNumWorkers();
		if (stride > 1 && cands.size() >= stride * 2)
		{
			vector<future<pair<WordLLs, float>>> futures(stride);
			for (size_t w = 0; w < stride; ++w)
			{
				futures[w] = workerPool.enqueue([&](size_t, size_t id)
				{
					WordLLs nCache;
					for (size_t idx = id; idx < cands.size(); idx += stride)
					{
						auto& p = evalCandidate(node, ownFormId, cands[idx], unknownForm);
						tMax = max(tMax, p.second);
						nCache.insert(nCache.end(), make_move_iterator(p.first.begin()), make_move_iterator(p.first.end()));
					}
					return make_pair(move(nCache), tMax);
				}, w);
			}

			for (auto& f : futures)
			{
				auto& p = f.get();
				tMax = max(tMax, p.second);
				cache[i].insert(cache[i].end(), make_move_iterator(p.first.begin()), make_move_iterator(p.first.end()));
			}
		}
		else
		{
			for (auto& curMorph : cands)
			{
				auto& p = evalCandidate(node, ownFormId, curMorph, unknownForm);
				tMax = max(tMax, p.second);
				cache[i].insert(cache[i].end(), make_move_iterator(p.first.begin()), make_move_iterator(p.first.end()));
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
			if (c.second > tMax - 10) reduced.emplace_back(move(c));
		}
		cache[i] = move(reduced);

		/*
		sort(cache[i].begin(), cache[i].end(), [](const pair<MInfos, float>& a, const pair<MInfos, float>& b)
		{
		return a.second > b.second;
		});
		size_t remainCnt = max((node->form ? node->form->candidate.size() : 2u) * topN * 2, (size_t)10);
		if (remainCnt < cache[i].size()) cache[i].erase(cache[i].begin() + remainCnt, cache[i].end());
		*/
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
	for (size_t i = 0; i < KGraphNode::MAX_NEXT; ++i)
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
			if (m.ownFormId)	return make_pair(morphBase + m.wid, ownFormList[m.ownFormId - 1]);
			else return make_pair(morphBase + m.wid, k_string{});
		});
		ret.emplace_back(mv, cand[i].second);
	}
	return ret;
}


KResult Kiwi::analyze(const u16string & str) const
{
	return analyze(str, 1)[0];
}

KResult Kiwi::analyze(const string & str) const
{
	return analyze(utf8_to_utf16(str));
}

vector<KResult> Kiwi::analyze(const string & str, size_t topN) const
{
	return analyze(utf8_to_utf16(str), topN);
}

vector<KResult> Kiwi::analyze(const u16string & str, size_t topN) const
{
	auto nodes = kt->split(normalizeHangul(str));
	auto res = findBestPath(nodes, mdl->getLangModel(), mdl->getMorphemes(), topN);
	vector<KResult> ret;
	for (auto&& r : res)
	{
		vector<KWordPair> rarr;
		for (auto&& s : r.first)
		{
			rarr.emplace_back(joinHangul(s.second.empty() ? *s.first->kform : s.second), s.first->tag, 0, 0);
		}
		ret.emplace_back(rarr, r.second);
	}
	if (ret.empty()) ret.emplace_back();
	return ret;
}

void Kiwi::clearCache()
{

}

int Kiwi::getVersion()
{
	return 50;
}

std::ostream & operator<<(std::ostream & os, const KWordPair & kp)
{
	return os << utf16_to_utf8({ kp.str().begin(), kp.str().end() }) << '/' << tagToString(kp.tag());
}
