#pragma once

struct KTrie
{
#ifdef  _DEBUG
	static int rootID;
	int id;
#endif //  _DEBUG

	KTrie* next[51] = {nullptr,};
	KTrie* fail = nullptr;
	KTrie* exit = nullptr;
	int depth;
	KTrie(int depth = 0);
	~KTrie();
	void build(const char* str);
	KTrie* findFail(char i) const;
	void fillFail();
	vector<pair<int, int>> searchAllPatterns(const vector<char>& str) const;
	vector<vector<char>> split(const vector<char>& str) const;
};

