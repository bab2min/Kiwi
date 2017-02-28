#pragma once

struct KForm;

struct KChunk
{
	union 
	{
		const KForm* form;
		struct
		{
			unsigned short _;
			unsigned char begin;
			unsigned char end;
		};
	};

	KChunk(const KForm* _form) : form(_form) {  }
	KChunk(unsigned char _begin, unsigned char _end) : _(0xFFFF), begin(_begin), end(_end) {}
	bool isStr() const { return _ == 0xFFFF; }
};

struct KTrie
{
#ifdef  _DEBUG
	static int rootID;
	int id;
	char currentChar = 0;
#endif //  _DEBUG

	KTrie* next[51] = {nullptr,};
	KTrie* fail = nullptr;
	const KForm* exit = nullptr;
	KTrie();
	~KTrie();
	void build(const char* str, const KForm* form);
	KTrie* findFail(char i) const;
	void fillFail();
	vector<pair<const KForm*, int>> searchAllPatterns(const vector<char>& str) const;
	vector<vector<KChunk>> split(const vector<char>& str) const;
};

