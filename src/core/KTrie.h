#pragma once

#include <array>
#include "BakedMap.hpp"
#include <kiwi/Trie.hpp>
#include "KForm.h"
#include <kiwi/PatternMatcher.h>
#include <kiwi/FrozenTrie.h>

#ifdef USE_BTREE
#include <btree/map.h>
#else
#endif

namespace kiwi
{
	class KModelMgr;
	class KNLangModel;

#ifdef USE_BTREE
	template<class K, class V> using map = btree::map<K, V>;
#else
	template<class K, class V> using map = std::map<K, V>;
#endif

	struct KGraphNode
	{
		enum { max_prev = 16 };
		const Form* form = nullptr;
		KString uform;
		uint16_t lastPos;
		std::array<uint16_t, max_prev> prevs = { { 0, } };

		KGraphNode(const Form* _form = nullptr, uint16_t _lastPos = 0) : form(_form), lastPos(_lastPos) {}
		KGraphNode(const KString& _uform, uint16_t _lastPos) : uform(_uform), lastPos(_lastPos) {}

		KGraphNode* getPrev(size_t idx) const { return prevs[idx] ? (KGraphNode*)this - prevs[idx] : nullptr; }

		static Vector<KGraphNode> removeUnconnected(const Vector<KGraphNode>& graph);

		void addPrev(size_t distance)
		{
			for (size_t i = 0; i < max_prev; ++i)
			{
				if (prevs[i]) continue;
				prevs[i] = distance;
				return;
			}
			throw std::runtime_error{ "'prevs' is overflowed" };
		}
	};

	Vector<KGraphNode> splitByTrie(const utils::FrozenTrie<kchar_t, const Form*>& trie, const KString& str, const PatternMatcher* pm, Match matchOptions);

	struct KTrie : public utils::TrieNode<char16_t, const Form*, utils::ConstAccess<map<char16_t, int32_t>>, KTrie>
	{
		Vector<KGraphNode> split(const KString& str, const PatternMatcher* pm, Match matchOptions) const;
		const Form* findForm(const KString& str) const;

		void saveToBin(std::ostream& str, const Form* base) const;
		static KTrie loadFromBin(std::istream& str, const Form* base);
	};
}
