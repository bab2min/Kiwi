#include "stdafx.h"
#include "KTrie.h"
#include "KForm.h"
#include "KFeatureTestor.h"
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
	for (auto p : next)
	{
		if(p) delete p;
	}
}

void KTrie::build(const char * str, const KForm * _form)
{
	assert(str);
	assert(str[0] < 52);
	if (!str[0])
	{
		if(!exit) exit = _form;
		return;
	}
	int idx = str[0] - 1;
	if (!next[idx])
	{
		next[idx] = new KTrie();
#ifdef _DEBUG
		next[idx]->currentChar = str[0];
#endif // _DEBUG

	}
	next[idx]->build(str + 1, _form);
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
		if (fail->next[i]) // if 'i' node exists
		{
			return fail->next[i];
		}
		else // or loop for failure of this
		{
			return fail->findFail(i);
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
			if (!p->next[i]) continue;
			p->next[i]->fail = p->findFail(i);
			dq.emplace_back(p->next[i]);

			if (!p->exit)
			{
				for (auto n = p; n->fail; n = n->fail)
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
	if (next[*begin - 1]) return next[*begin - 1]->search(begin + 1, end);
	return nullptr;
}

vector<pair<const KForm*, int>> KTrie::searchAllPatterns(const string& str) const
{
	vector<pair<const KForm*, int>> found;
	auto curTrie = this;
	int n = 0;
	for (auto c : str)
	{
		assert(c < 52);
		while (!curTrie->next[c - 1]) // if curTrie has no exact next node, goto fail
		{
			if (curTrie->fail)
			{
				curTrie = curTrie->fail;
				if(curTrie->exit) found.emplace_back(curTrie->exit, n);
			}
			else goto continueFor; // root node has no exact next node, continue
		}
		n++;
		// from this, curTrie has exact node
		curTrie = curTrie->next[c - 1];
		if (curTrie->exit) // if it has exit node, a pattern has found
		{
			found.emplace_back(curTrie->exit, n);
		}
	continueFor:;
	}
	while (curTrie->fail)
	{
		curTrie = curTrie->fail;
		if (curTrie->exit) found.emplace_back(curTrie->exit, n);
	}
	return found;
}

vector<vector<KChunk>> KTrie::split(const string& str, bool hasPrefix) const
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
		KFeatureTestor::notPositive
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
		
		while (!curTrie->next[c - 1]) // if curTrie has no exact next node, goto fail
		{
			if (curTrie->fail)
			{
				curTrie = curTrie->fail;
				for (auto submatcher = curTrie; submatcher; submatcher = submatcher->fail)
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
		curTrie = curTrie->next[c - 1];
		// if it has exit node, a pattern has found
		for (auto submatcher = curTrie; submatcher; submatcher = submatcher->fail)
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
		curTrie = curTrie->fail;
		if (curTrie->exit && curTrie->exit != (void*)-1)
		{
			candidates.emplace_back(curTrie->exit);
		}
	}
	brachOut();

	vector<vector<KChunk>> ret;
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

const KMorpheme * KChunk::getMorpheme(size_t idx, KMorpheme * tmp, const char * ostr) const
{
	if (!isStr()) return form->candidate[idx];
	if (!begin && !end) return nullptr;
	*tmp = KMorpheme{ string{ ostr + begin, ostr + end } };
	tmp->p = (end - begin) * -1.5f - 6.f;
	return tmp;
}

size_t KChunk::getCandSize() const
{
	if (isStr()) return 1;
	return form->candidate.size();
}