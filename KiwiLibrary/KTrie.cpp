#include "stdafx.h"
#include "KTrie.h"
#include "KForm.h"
#include "KFeatureTestor.h"
#include "KMemoryKMorphemeNode.h"
#include "KModelMgr.h"
#include "KMemory.h"

#ifdef _DEBUG
int KTrie::rootID = 0;
#endif // _DEBUG

KTrie::KTrie()
{
#ifdef _DEBUG
	id = rootID++;
#endif // DEBUG

}


KTrie::~KTrie()
{
#ifdef TRIE_ALLOC_ARRAY
#else
	for (auto p : next)
	{
		if(p) delete p;
	}
#endif
}
#ifdef TRIE_ALLOC_ARRAY
void KTrie::build(const char * str, const KForm * _form, const function<KTrie*()>& alloc)
#else
void KTrie::build(const char * str, const KForm * _form)
#endif
{
	assert(str);
	assert(str[0] < 52);
	if (!str[0])
	{
		if(!exit) exit = _form;
		return;
	}
	int idx = str[0] - 1;
	if (!getNext(idx))
	{
#ifdef TRIE_ALLOC_ARRAY
		next[idx] = alloc() - this;
#else
		next[idx] = new KTrie();
#endif
#ifdef _DEBUG
		getNext(idx)->currentChar = str[0];
#endif // _DEBUG

	}

#ifdef TRIE_ALLOC_ARRAY
	getNext(idx)->build(str + 1, _form, alloc);
#else
	next[idx]->build(str + 1, _form);
#endif
}

KTrie * KTrie::findFail(char i) const
{
	assert(i < 51);
	if (!fail) // if this is Root
	{
		return (KTrie*)this;
	}
	else
	{
		if (getFail()->getNext(i)) // if 'i' node exists
		{
			return getFail()->getNext(i);
		}
		else // or loop for failure of this
		{
			return getFail()->findFail(i);
		}
	}
}

void KTrie::fillFail() // 
{
	deque<KTrie*> dq;
	for (dq.emplace_back(this); !dq.empty(); dq.pop_front())
	{
		auto p = dq.front();
		for (int i = 0; i < 51; i++)
		{
			if (!p->getNext(i)) continue;
			p->getNext(i)->fail = p->findFail(i);
			dq.emplace_back(p->getNext(i));

			if (!p->exit)
			{
				for (auto n = p; n->getFail(); n = n->getFail())
				{
					if (n->exit)
					{
						p->exit = (KForm*)-1;
						break;
					}
				}
			}
		}
	}
}

const KForm * KTrie::search(const char* begin, const char* end) const
{
	if(begin == end) return exit;
	if (getNext(*begin - 1)) return getNext(*begin - 1)->search(begin + 1, end);
	return nullptr;
}

vector<pair<const KForm*, int>> KTrie::searchAllPatterns(const k_string& str) const
{
	vector<pair<const KForm*, int>> found;
	auto curTrie = this;
	int n = 0;
	for (auto c : str)
	{
		assert(c < 52);
		while (!curTrie->getNext(c - 1)) // if curTrie has no exact next node, goto fail
		{
			if (curTrie->fail)
			{
				curTrie = curTrie->getFail();
				if(curTrie->exit) found.emplace_back(curTrie->exit, n);
			}
			else goto continueFor; // root node has no exact next node, continue
		}
		n++;
		// from this, curTrie has exact node
		curTrie = curTrie->getNext(c - 1);
		if (curTrie->exit) // if it has exit node, a pattern has found
		{
			found.emplace_back(curTrie->exit, n);
		}
	continueFor:;
	}
	while (curTrie->fail)
	{
		curTrie = curTrie->getFail();
		if (curTrie->exit) found.emplace_back(curTrie->exit, n);
	}
	return found;
}

vector<k_vchunk> KTrie::split(const k_string& str, bool hasPrefix) const
{
	struct ChunkInfo
	{
		vector<pair<const KForm*, size_t>> chunks;
		//float p = 0;
		size_t count = 0;
		bitset<64> splitter;
	};

	auto curTrie = this;
	size_t n = 0;
	vector<ChunkInfo> branches;
	branches.emplace_back();

	unordered_map<bitset<64>, size_t> branchMap;
	branchMap.emplace(0, 0);

	vector<const KForm*> candidates;
	static bool (*vowelFunc[])(const char*, const char*) = {
		KFeatureTestor::isPostposition,
		KFeatureTestor::isVowel,
		KFeatureTestor::isVocalic,
		KFeatureTestor::isVocalicH,
		KFeatureTestor::notVowel,
		KFeatureTestor::notVocalic,
		KFeatureTestor::notVocalicH,
	};
	static bool(*polarFunc[])(const char*, const char*) = {
		KFeatureTestor::isPositive,
		KFeatureTestor::isNegative
	};
	size_t maxChunk = (str.size() + 9) / 4;
	auto brachOut = [&]()
	{
		if (!candidates.empty())
		{
			size_t base = branches.size();
			for (auto cand : candidates)
			{
				for (size_t i = 0; i < base; i++)
				{
					auto& pl = branches[i].chunks;
					// if there are intersected chunk, pass
					if (!pl.empty() && pl.back().second > n - cand->form.size()) continue;
					// if current chunk is precombined, test next character
					if (!cand->suffix.empty() && n + 1 < str.size()
						&& cand->suffix.find(str[n]) == cand->suffix.end())
					{
						continue;
					}

					if (branches[i].count >= maxChunk) continue;
					if (branches[i].chunks.size() >= 2
						&& cand->form.size() == 1
						&& (branches[i].chunks.end() - 1)->first->form.size() == 1
						&& (branches[i].chunks.end() - 2)->first->form.size() == 1) continue;

					size_t bEnd = n - cand->form.size();
					size_t bBegin = pl.empty() ? 0 : pl.back().second;
					bool beforeMatched;
					if (beforeMatched = (bBegin == bEnd && !pl.empty())) bBegin -= pl.back().first->form.size();

					if (bEnd == 0 && !hasPrefix)
					{
						// if the form has constraints, do test
						if ((size_t)cand->vowel &&
							!vowelFunc[(size_t)cand->vowel - 1](&str[0] + bBegin, &str[0] + bEnd)) continue;
						if ((size_t)cand->polar &&
							!polarFunc[(size_t)cand->polar - 1](&str[0] + bBegin, &str[0] + bEnd)) continue;
					}
					// if former ends with ¤· and next begin vowel, pass
					if (!beforeMatched && bEnd && str[bEnd - 1] == 23 && cand->form[0] > 30) continue;

					if (!beforeMatched && !cand->hasFirstV && !KFeatureTestor::isCorrectEnd(&str[0] + bBegin, &str[0] + bEnd)) continue;
					if (!beforeMatched && !KFeatureTestor::isCorrectStart(&str[0] + bBegin, &str[0] + bEnd)) continue;
					//if (!KFeatureTestor::isCorrectStart(&str[0] + n, &str[0] + str.size())) continue;
					
					// find whether the same splitter appears before
					auto tSplitter = branches[i].splitter;
					if (n < str.size()) tSplitter.set(n - 1);
					if (n - cand->form.size() > 0) tSplitter.set(n - cand->form.size() - 1);
					auto it = branchMap.find(tSplitter);
					size_t idxRepl = -1;
					if (it != branchMap.end())
					{
						const auto& b = branches[it->second];
						// if the same splitter exists and has fewer matches, update
						if (b.chunks.size() < branches[i].chunks.size() + 1)
						{
							idxRepl = it->second;
						}
						// or pass
						else continue;
					}

					if (idxRepl == -1)
					{
						branchMap[tSplitter] = branches.size();
						branches.push_back(branches[i]);
					}
					else branches[idxRepl] = branches[i];
					auto& bInsertor = idxRepl == -1 ? branches.back() : branches[idxRepl];
					bInsertor.chunks.emplace_back(cand, n);
					//bInsertor.p += cand->maxP;
					bInsertor.splitter = tSplitter;
					bInsertor.count += (beforeMatched || bBegin == bEnd) ? 1 : 2;
					//if (pl.empty() || pl.back().second < bEnd) branches.back().second += mdl.;
				}
			}
			candidates.clear();
		}
	};

	for (auto c : str)
	{
		assert(c < 52);
		
		while (!curTrie->getNext(c - 1)) // if curTrie has no exact next node, goto fail
		{
			if (curTrie->fail)
			{
				curTrie = curTrie->getFail();
				for (auto submatcher = curTrie; submatcher; submatcher = submatcher->getFail())
				{
					if (!submatcher->exit) break;
					else if (submatcher->exit != (void*)-1 
						&& (candidates.empty() || candidates.back() != submatcher->exit))
					{
						candidates.emplace_back(submatcher->exit);
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
		curTrie = curTrie->getNext(c - 1);
		// if it has exit node, a pattern has found
		for (auto submatcher = curTrie; submatcher; submatcher = submatcher->getFail())
		{
			if (!submatcher->exit) break;
			else if (submatcher->exit != (void*)-1 &&
				(candidates.empty() || candidates.back() != submatcher->exit))
			{
				candidates.emplace_back(submatcher->exit);
			}
		}
	continueFor:
		n++;
	}
	while (curTrie->fail)
	{
		curTrie = curTrie->getFail();
		if (curTrie->exit && curTrie->exit != (void*)-1)
		{
			candidates.emplace_back(curTrie->exit);
		}
	}
	brachOut();

	vector<k_vchunk> ret;
	ret.reserve(branches.size());
	for (auto branch : branches)
	{
		if (!branch.chunks.empty() && branch.chunks.back().second < str.size())
		{
			if (!KFeatureTestor::isCorrectStart(&str[0] + branch.chunks.back().second, &str[0] + str.size())) continue;
		}
		ret.emplace_back();
		size_t c = 0;
		//printf("%g\n", branch.p);
		//printf("%d\n", branch.count);
		for (auto p : branch.chunks)
		{
			size_t s = p.second - p.first->form.size();
			if (c < s)
			{
				ret.back().emplace_back(c, s);
			}
			ret.back().emplace_back(p.first);
			c = p.second;
		}
		if (c < str.size())
		{
			ret.back().emplace_back(c, str.size());
		}
	}
	return ret;
}

KMorphemeNode* KTrie::splitGM(const k_string & str, vector<KMorpheme>& tmpMorph, vector<KMorphemeNode>& ret, const KModelMgr* mdl, bool hasPrefix) const
{
	static bool(*vowelFunc[])(const char*, const char*) = {
		KFeatureTestor::isPostposition,
		KFeatureTestor::isVowel,
		KFeatureTestor::isVocalic,
		KFeatureTestor::isVocalicH,
		KFeatureTestor::notVowel,
		KFeatureTestor::notVocalic,
		KFeatureTestor::notVocalicH,
	};
	static bool(*polarFunc[])(const char*, const char*) = {
		KFeatureTestor::isPositive,
		KFeatureTestor::isNegative
	};

	size_t n = 0;
	auto curTrie = this;
#ifdef CUSTOM_ALLOC
	vector<const KForm*, pool_allocator<void*>> candidates;
	vector<KMorphemeNode, pool_allocator<KMorphemeNode>> nodes;
	vector<uint16_t, pool_allocator<void*>> nodeEndPos;
	multimap<uint16_t, uint16_t, less<uint16_t>, pool_allocator<void*>> nodeAtNthEnd;
	map<pair<uint16_t, uint16_t>, uint16_t, less<pair<uint16_t, uint16_t>>, pool_allocator<void*>> unknownNodes;
	unordered_set<uint16_t, hash<uint16_t>, equal_to<uint16_t>, pool_allocator<void*>> nodeToRepairMorph;
#else
	vector<const KForm*> candidates;
	vector<KMorphemeNode> nodes;
	vector<uint16_t> nodeEndPos;
	multimap<uint16_t, uint16_t> nodeAtNthEnd;
	map<pair<uint16_t, uint16_t>, uint16_t> unknownNodes;
	unordered_set<uint16_t> nodeToRepairMorph;
#endif
	nodes.reserve(16);
	nodes.emplace_back();
	nodeAtNthEnd.emplace(0, 0);
	nodeEndPos.emplace_back(0);
	auto makeTmpMorph = [&tmpMorph](const k_string& form)
	{
		tmpMorph.emplace_back();
		tmpMorph.back().kform = NEW_IN_POOL(k_string){ form }; // to be released
		tmpMorph.back().p = form.size() * -1.5f - 6.f;
		tmpMorph.back().tag = KPOSTag::NNP; // consider unknown morpheme as NNP
		return tmpMorph.size() - 1;
	};

	auto makeNewNode = [&nodes, &nodeAtNthEnd, &nodeEndPos](const KMorpheme* morph, size_t endPos) -> size_t
	{
		size_t nid = nodes.size();
		nodes.emplace_back(morph);
		nodeAtNthEnd.emplace(endPos, nid);
		nodeEndPos.emplace_back(endPos);
		return nid;
	};

	auto brachOut = [&]()
	{
		if (candidates.empty()) return;
		for (auto cand : candidates)
		{
			// if current chunk is precombined, test next character
			if (!cand->suffix.empty() && n + 1 < str.size()
				&& cand->suffix.find(str[n]) == cand->suffix.end())
			{
				continue;
			}
			size_t nBegin = n - cand->form.size();
			// if former ends with ¤· and next begin vowel, pass
			//if (!beforeMatched && bEnd && str[bEnd - 1] == 23 && cand->form[0] > 30) continue;

			vector<pair<const KMorpheme*, size_t>> newMorphs;
			newMorphs.reserve(cand->candidate.size());
			bool unknownFormAvailable = false;
			for (auto morph : cand->candidate)
			{
				// test if current morph satisfies restrictions
				if (!(nBegin == 0 && hasPrefix))
				{
					if ((size_t)morph->vowel &&
						!vowelFunc[(size_t)morph->vowel - 1](&str[0], &str[0] + nBegin)) continue;
					if ((size_t)morph->polar &&
						!polarFunc[(size_t)morph->polar - 1](&str[0], &str[0] + nBegin)) continue;
				}
				if (!(morph->combineSocket && morph->tag == KPOSTag::UNKNOWN)) unknownFormAvailable = true;
				newMorphs.emplace_back(morph, 0);
			}
			// if none of morphs were matched, pass
			if (newMorphs.empty()) continue;

			if (unknownFormAvailable)
			{
				size_t startPos = -1;
				size_t newNodeId = -1;
				for (auto it = nodeAtNthEnd.lower_bound(0); it != nodeAtNthEnd.lower_bound(nBegin); it++)
				{
					// if former matched with morpheme
					if (it->second && !nodeToRepairMorph.count(it->second))
					{
						auto& beforeMorph = nodes[it->second].morpheme;
						if (beforeMorph->combineSocket && beforeMorph->tag != KPOSTag::UNKNOWN) continue;
					}
					if ((!cand->hasFirstV && !KFeatureTestor::isCorrectEnd(&str[0] + it->first, &str[0] + nBegin))
						|| !KFeatureTestor::isCorrectStart(&str[0] + it->first, &str[0] + nBegin)
						|| nBegin - it->first == 1)
					{
						continue;
					}
					if (startPos != it->first)
					{
						auto nt = unknownNodes.find(make_pair(it->first, nBegin));
						if (nt == unknownNodes.end())
						{
							const KMorpheme* morph = (KMorpheme*)makeTmpMorph(str.substr(it->first, nBegin - it->first)); // unknown form
							newNodeId = makeNewNode(morph, nBegin);
							nodeToRepairMorph.emplace(newNodeId);
							unknownNodes.emplace(make_pair(it->first, nBegin), newNodeId);
						}
						else
						{
							// if there is already unknown node, pass
							if (!nt->second) continue;
							newNodeId = nt->second;
						}
						startPos = it->first;
					}
					nodes[it->second].nexts.emplace_back((KMorphemeNode*)newNodeId);
				}
			}

			for (auto it = nodeAtNthEnd.lower_bound(nBegin); it != nodeAtNthEnd.upper_bound(nBegin); it++)
			{
				// if former matched with morpheme
				if (it->second && !nodeToRepairMorph.count(it->second))
				{
					auto beforeMorph = nodes[it->second].morpheme;
					for (auto& p : newMorphs)
					{
						auto& morph = p.first;
						if (mdl->getTransitionP(beforeMorph, morph) <= P_MIN) continue;
						if (beforeMorph->combineSocket && beforeMorph->tag != KPOSTag::UNKNOWN
							&& (beforeMorph->combineSocket == morph->combineSocket && morph->tag == KPOSTag::UNKNOWN))
						{
						}
						else if (beforeMorph->combineSocket && beforeMorph->tag != KPOSTag::UNKNOWN) continue;
						else if (morph->combineSocket && morph->tag == KPOSTag::UNKNOWN) continue;

						if(!p.second) p.second = makeNewNode(p.first, n);
						nodes[it->second].nexts.emplace_back((KMorphemeNode*)p.second);
						unknownNodes.emplace(make_pair(it->first, nBegin), 0);
					}
				}
				else
				{
					for (auto& p : newMorphs)
					{
						auto& morph = p.first;
						//if (morph->getForm().size() < 2) continue;
						if (morph->combineSocket && morph->tag == KPOSTag::UNKNOWN) continue;
						if (!p.second) p.second = makeNewNode(p.first, n);
						nodes[it->second].nexts.emplace_back((KMorphemeNode*)p.second);
						unknownNodes.emplace(make_pair(it->first, nBegin), 0);
					}
				}
			}
		}
		candidates.clear();
	};

	for (auto c : str)
	{
		assert(c < 52);

		while (!curTrie->getNext(c - 1)) // if curTrie has no exact next node, goto fail
		{
			if (curTrie->fail)
			{
				curTrie = curTrie->getFail();
				for (auto submatcher = curTrie; submatcher; submatcher = submatcher->getFail())
				{
					if (!submatcher->exit) break;
					else if (submatcher->exit != (void*)-1)
					{
						if (find(candidates.begin(), candidates.end(), submatcher->exit) != candidates.end()) break;
						candidates.emplace_back(submatcher->exit);
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
		curTrie = curTrie->getNext(c - 1);
		// if it has exit node, a pattern has found
		for (auto submatcher = curTrie; submatcher; submatcher = submatcher->getFail())
		{
			if (!submatcher->exit) break;
			else if (submatcher->exit != (void*)-1)
			{
				if (find(candidates.begin(), candidates.end(), submatcher->exit) != candidates.end()) break;
				candidates.emplace_back(submatcher->exit);
			}
		}
	continueFor:
		n++;
	}
	while (curTrie->fail)
	{
		curTrie = curTrie->getFail();
		if (curTrie->exit && curTrie->exit != (void*)-1)
		{
			if (find(candidates.begin(), candidates.end(), curTrie->exit) != candidates.end()) break;
			candidates.emplace_back(curTrie->exit);
		}
	}
	brachOut();

	// mark acceptable final
	for (auto it = nodeAtNthEnd.lower_bound(n); it != nodeAtNthEnd.upper_bound(n); it++)
	{
		if (it->second && !nodeToRepairMorph.count(it->second))
		{
			auto& morph = nodes[it->second].morpheme;
			if (morph->combineSocket && morph->tag != KPOSTag::UNKNOWN)
			{
				nodeEndPos[it->second] = 0;
				continue;
			}
			nodes[it->second].nexts.emplace_back(nullptr);
		}
	}

	// repair morph pointer in nodes
	for (auto id : nodeToRepairMorph)
	{
		nodes[id].morpheme = &tmpMorph[(size_t)nodes[id].morpheme];
	}

	unordered_map<size_t, size_t> realAddress;
	for (size_t i = 0; i < nodes.size(); i++)
	{
		if (nodeEndPos[i] < n && nodes[i].nexts.empty()) continue;
		realAddress.emplace(i, realAddress.size());
	}

	// if no match
	if (realAddress.empty())
	{
		return nullptr;
	}

	ret.resize(realAddress.size());

	// repair node pointer in all nodes
	for (size_t i = 0; i < nodes.size(); i++)
	{
		auto it = realAddress.find(i);
		if (it == realAddress.end()) continue;
		auto& aNode = ret[it->second];
		sort(nodes[i].nexts.begin(), nodes[i].nexts.end());
		KMorphemeNode* beforePtr = nullptr;
		for (auto ptr : nodes[i].nexts)
		{
			if (ptr == nullptr)
			{
				// mark acceptable final node
				aNode.setAcceptableFinal();
				break;
			}
			if (beforePtr == ptr) continue;
			beforePtr = ptr;
			auto jt = realAddress.find((size_t)ptr);
			if (jt == realAddress.end()) continue;
			aNode.nexts.emplace_back(&ret[jt->second]);
		}
		aNode.morpheme = nodes[i].morpheme;
		if (aNode.morpheme && aNode.morpheme->combined) aNode.morpheme = aNode.morpheme->getCombined();
	}
	return &ret[0];
}

const KMorpheme * KChunk::getMorpheme(size_t idx, KMorpheme * tmp) const
{
	if (!isStr()) return form->candidate[idx];
	if (!begin && !end) return nullptr;
	tmp->kform = nullptr;
	tmp->tag = KPOSTag::UNKNOWN;
	tmp->p = (end - begin) * -1.5f - 6.f;
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
