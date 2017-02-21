#include "stdafx.h"
#include "KTrie.h"

#ifdef _DEBUG
int KTrie::rootID = 0;
#endif // _DEBUG

KTrie::KTrie(int _depth) : depth(_depth)
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

void KTrie::build(const char * str)
{
	assert(str);
	assert(str[0] < 52);
	if (!str[0])
	{
		exit = this;
		return;
	}
	int idx = str[0] - 1;
	if (!next[idx]) next[idx] = new KTrie(depth + 1);
	next[idx]->build(str + 1);
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
		
		auto n = this;
		while (!n->exit && n->fail)
		{
			n = n->fail;
		}
		exit = n->exit;
	}
	for (auto p : next)
	{
		if (p)
		{
			p->fillFail();
		}
	}
}

vector<pair<int, int>> KTrie::searchAllPatterns(const vector<char>& str) const
{
	vector<pair<int, int>> found;
	auto curTrie = this;
	int n = 0;
	for (auto c : str)
	{
		n++;
		assert(c < 52);

		while (!curTrie->next[c - 1]) // if curTrie has no exact node, goto fail
		{
			if(curTrie->fail) curTrie = curTrie->fail;
			else goto continueFor; // root node has no exact node, continue
		}
		// from this, curTrie has exact node
		curTrie = curTrie->next[c - 1];
		if (curTrie->exit) // if it has exit node, a pattern has found
		{
			found.emplace_back(n - curTrie->exit->depth, n);
		}
	continueFor:;
	}
	return found;
}

vector<vector<char>> KTrie::split(const vector<char>& str) const
{
	return vector<vector<char>>();
}

