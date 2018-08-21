#pragma once

#include "BakedMap.hpp"
#include "Trie.hpp"
#include "KForm.h"

class KModelMgr;
class KNLangModel;

struct KGraphNode
{
	enum { MAX_NEXT = 16 };
	const KForm* form = nullptr;
	k_string uform;
	uint16_t lastPos;
	std::array<uint16_t, MAX_NEXT> nexts = { 0, };

	KGraphNode(const KForm* _form = nullptr, uint16_t _lastPos = 0) : form(_form), lastPos(_lastPos) {}
	KGraphNode(const k_string& _uform, uint16_t _lastPos) : uform(_uform), lastPos(_lastPos) {}

	KGraphNode* getNext(size_t idx) const { return nexts[idx] ? (KGraphNode*)this + nexts[idx] : nullptr; }
	
	typedef std::vector<std::pair<const KMorpheme*, k_string>> pathType;

	static std::vector<KGraphNode> removeUnconnected(const std::vector<KGraphNode>& graph);

	static std::vector<std::pair<pathType, float>> findBestPath(
		const std::vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN);
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