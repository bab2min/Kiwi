#include "stdafx.h"
#include "KTrie.h"
#include "KForm.h"
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
	if (!next[idx]) next[idx] = new KTrie();
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

void KTrie::fillFail()
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
	vector<vector<pair<const KForm*, size_t>>> branches;
	branches.emplace_back();
	vector<const KForm*> candidates;

	auto brachOut = [&]()
	{
		if (!candidates.empty())
		{
			size_t base = branches.size();
			for (size_t i = 0; i < base; i++)
			{
				for (auto cand : candidates)
				{
					if (!branches[i].empty() && branches[i].back().second > n - cand->form.size()) continue;
					branches.push_back(branches[i]);
					branches.back().emplace_back(cand, n);
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
				if (curTrie->exit)
				{
					candidates.emplace_back(curTrie->exit);
				}
			}
			else goto continueFor; // root node has no exact next node, continue
		}
		brachOut();
		n++;
		// from this, curTrie has exact node
		curTrie = curTrie->next[c - 1];
		if (curTrie->exit) // if it has exit node, a pattern has found
		{
			candidates.emplace_back(curTrie->exit);
		}
	continueFor:;
	}
	while (curTrie->fail)
	{
		curTrie = curTrie->fail;
		if (curTrie->exit)
		{
			candidates.emplace_back(curTrie->exit);
		}
	}
	brachOut();

	vector<vector<KChunk>> ret;
	for (auto branch : branches)
	{
		ret.emplace_back();
		size_t c = 0;
		for (auto p : branch)
		{
			size_t s = p.second - p.first->form.size();
			if (c < s)
			{
				ret.back().emplace_back(&str[0] + c, &str[0] + s);
			}
			ret.back().emplace_back(p.first);
			c = p.second;
		}
		if (c < str.size())
		{
			ret.back().emplace_back(&str[c], &str[0] + str.size());
		}
	}
	return ret;
}

