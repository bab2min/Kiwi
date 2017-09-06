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

#ifdef CUSTOM_ALLOC
#include "KMemoryKChunk.h"
typedef vector<KChunk, pool_allocator<KChunk>> k_vchunk;
#else 
typedef vector<KChunk> k_vchunk;
#endif

class KModelMgr;

struct KMorphemeNode
{
	const KMorpheme* morpheme;
	vector<KMorphemeNode*> nexts;
	k_vpcf* optimaCache = nullptr;
	KMorphemeNode(const KMorpheme* _morpheme = nullptr) : morpheme(_morpheme) {}
	~KMorphemeNode() { if (optimaCache) delete optimaCache; }
	void setAcceptableFinal() { nexts.emplace_back(nullptr); }
	bool isAcceptableFinal() const { return nexts.size() == 1 && nexts[0] == nullptr; }
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
	KTrie* getFail() const
	{
		return fail;
	}
	const KForm* search(const char* begin, const char* end) const;
	vector<pair<const KForm*, int>> searchAllPatterns(const k_string& str) const;
	vector<k_vchunk> split(const k_string& str, bool hasPrefix = false) const;
	shared_ptr<KMorphemeNode> splitGM(const k_string& str, vector<KMorpheme>& tmpMorph, const KModelMgr* mdl, bool hasPrefix = false) const;
};