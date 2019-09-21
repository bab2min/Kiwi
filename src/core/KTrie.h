#pragma once

#include "BakedMap.hpp"
#include "Trie.hpp"
#include "KForm.h"

namespace kiwi
{
	class KModelMgr;
	class KNLangModel;

	struct KGraphNode
	{
		enum { MAX_PREV = 16 };
		const KForm* form = nullptr;
		k_string uform;
		uint16_t lastPos;
		std::array<uint16_t, MAX_PREV> prevs = { { 0, } };

		KGraphNode(const KForm* _form = nullptr, uint16_t _lastPos = 0) : form(_form), lastPos(_lastPos) {}
		KGraphNode(const k_string& _uform, uint16_t _lastPos) : uform(_uform), lastPos(_lastPos) {}

		KGraphNode* getPrev(size_t idx) const { return prevs[idx] ? (KGraphNode*)this - prevs[idx] : nullptr; }

		static std::vector<KGraphNode> removeUnconnected(const std::vector<KGraphNode>& graph);

		void addPrev(size_t distance)
		{
			for (size_t i = 0; i < MAX_PREV; ++i)
			{
				if (prevs[i]) continue;
				prevs[i] = distance;
				return;
			}
			throw std::runtime_error{ "'prevs' is overflowed" };
		}
	};

	struct KTrie : public Trie<char16_t, const KForm*, OverriddenMap<std::map<char16_t, int32_t>>>
	{
		std::vector<KGraphNode> split(const k_string& str) const;
		const KForm* findForm(const k_string& str) const;
		KTrie* getNext(k_char i) const { return (KTrie*)Trie::getNext(i); }
		KTrie* getFail() const { return (KTrie*)Trie::getFail(); }

		void saveToBin(std::ostream& str, const KForm* base) const;
		static KTrie loadFromBin(std::istream& str, const KForm* base);
	};
}