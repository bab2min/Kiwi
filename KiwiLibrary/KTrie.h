#pragma once

#include "BakedMap.hpp"
#include "Trie.hpp"
#include "KForm.h"

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
typedef std::vector<KChunk, pool_allocator<KChunk>> k_vchunk;
#else 
typedef std::vector<KChunk> k_vchunk;
#endif

class KModelMgr;

struct KMorphemeNode
{
	const KMorpheme* morpheme;
#ifdef CUSTOM_ALLOC
	std::vector<KMorphemeNode*, pool_allocator<void*>> nexts;
#else
	std::vector<KMorphemeNode*> nexts;
#endif
	k_vpcf* optimaCache = nullptr;
	KMorphemeNode(const KMorpheme* _morpheme = nullptr) : morpheme(_morpheme) {}
	~KMorphemeNode();
	void makeNewCache();
	void setAcceptableFinal() { nexts.emplace_back(nullptr); }
	bool isAcceptableFinal() const { return nexts.size() == 1 && nexts[0] == nullptr; }
};

class KNLangModel;

struct KGraphNode
{
	enum { MAX_NEXT = 16 };
	const KForm* form = nullptr;
	k_string uform;
	uint16_t lastPos;
	uint16_t nexts[MAX_NEXT] = { 0, };

	KGraphNode(const KForm* _form = nullptr, uint16_t _lastPos = 0) : form(_form), lastPos(_lastPos) {}
	KGraphNode(const k_string& _uform, uint16_t _lastPos) : uform(_uform), lastPos(_lastPos) {}

	KGraphNode* getNext(size_t idx) const { return nexts[idx] ? (KGraphNode*)this + nexts[idx] : nullptr; }
	
	static std::vector<std::pair<std::vector<const KMorpheme*>, float>> findBestPath(const std::vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN);

	void addNext(KGraphNode* next)
	{
		size_t i = 0;
		while(i < MAX_NEXT && nexts[i]) ++i;
		nexts[i] = next - this;
	}
};

struct KTrie : public Trie<char16_t, const KForm*, OverriddenMap<std::map<char16_t, int32_t>>>
{
	std::vector<KGraphNode> split(const k_string& str) const;
	KTrie* getNext(k_char i) const { return (KTrie*)Trie::getNext(i); }
	KTrie* getFail() const { return (KTrie*)Trie::getFail(); }

	void saveToBin(std::ostream& str, const KForm* base) const;
	static KTrie loadFromBin(std::istream& str, const KForm* base);
};