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
#endif
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

void KTrie::fillFail() // 너비우선탐색이 안되고, 깊이우선탐색이 되어서 fail노드를 잘못 생성하는 버그가 있음.
{
	for (int i = 0; i < 51; i++)
	{
		if (!next[i]) continue;
		next[i]->fail = findFail(i);
		
		/*auto n = this;
		while (!n->exit && n->fail)
		{
			n = n->fail;
		}
		exit = n->exit;*/
	}
	for (auto p : next)
	{
		if (p)
		{
			p->fillFail();
		}
	}
}

vector<pair<const KForm*, int>> KTrie::searchAllPatterns(const vector<char>& str) const
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

vector<vector<KChunk>> KTrie::split(const vector<char>& str) const
{
	auto curTrie = this;
	size_t n = 0;
	vector<pair<vector<pair<const KForm*, size_t>>, float>> branches;
	branches.emplace_back();
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
	auto brachOut = [&]()
	{
		if (!candidates.empty())
		{
			size_t base = branches.size();
			for (size_t i = 0; i < base; i++)
			{
				for (auto cand : candidates)
				{
					auto& pl = branches[i].first;

					if (!pl.empty() && pl.back().second > n - cand->form.size()) continue;
					size_t bEnd = n - cand->form.size();
					size_t bBegin = pl.empty() ? 0 : pl.back().second;
					if (bBegin == bEnd && !pl.empty()) bBegin -= pl.back().first->form.size();

					//if ((size_t)cand->vowel && 
					//	!vowelFunc[(size_t)cand->vowel - 1](&str[0] + bBegin, &str[0] + bEnd)) continue;
					//if ((size_t)cand->polar &&
					//	!polarFunc[(size_t)cand->polar - 1](&str[0] + bBegin, &str[0] + bEnd)) continue;
					//if (!cand->hasFirstV && !KFeatureTestor::isCorrectEnd(&str[0] + bBegin, &str[0] + bEnd)) continue;
					//if (!KFeatureTestor::isCorrectStart(&str[0] + n, &str[0] + str.size())) continue;
					branches.push_back(branches[i]);
					branches.back().first.emplace_back(cand, n);
					branches.back().second += cand->maxP;
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
					if(submatcher->exit) candidates.emplace_back(submatcher->exit);
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
		if (curTrie->exit) // if it has exit node, a pattern has found
		{
			for (auto submatcher = curTrie; submatcher; submatcher = submatcher->fail)
			{
				if (submatcher->exit) candidates.emplace_back(submatcher->exit);
			}
			//candidates.emplace_back(curTrie->exit);
		}
	continueFor:
		n++;
	}
	while (curTrie->fail)
	{
		curTrie = curTrie->fail;
		if (curTrie->exit)
		{
			candidates.emplace_back(curTrie->exit);
		}
	}
	//n++;
	brachOut();

	vector<vector<KChunk>> ret;
	for (auto branch : branches)
	{
		ret.emplace_back();
		size_t c = 0;
		//printf("%g\n", branch.second);
		for (auto p : branch.first)
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

