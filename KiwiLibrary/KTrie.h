#pragma once

struct KForm;
struct KMorpheme;
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
	const KMorpheme* getMorpheme(size_t idx, KMorpheme* tmp) const;
	size_t getCandSize() const;
	bool isStr() const { return _ == 0xFFFF; }
};

struct KTrie
{
#ifdef  _DEBUG
	static int rootID;
	int id;
	char currentChar = 0;
#endif //  _DEBUG

#ifdef TRIE_ALLOC_ARRAY
	int next[51] = { 0, };
	//int fail;
	KTrie* fail = nullptr;
#else
	KTrie* next[51] = {nullptr,};
	KTrie* fail = nullptr;
#endif
	const KForm* exit = nullptr;
	KTrie();
	~KTrie();
#ifdef TRIE_ALLOC_ARRAY
	void build(const char* str, const KForm* form, const function<KTrie*()>& alloc);
#else
	void build(const char* str, const KForm* form);
#endif
	KTrie* findFail(char i) const;
	void fillFail();
	KTrie* getNext(int i) const 
	{
#ifdef TRIE_ALLOC_ARRAY
		return next[i] ? (KTrie*)this + next[i] : nullptr;
#else
		return next[i];
#endif
	}
	const KForm* search(const char* begin, const char* end) const;
	vector<pair<const KForm*, int>> searchAllPatterns(const k_string& str) const;
	vector<vector<KChunk>> split(const k_string& str, bool hasPrefix = false) const;
};

