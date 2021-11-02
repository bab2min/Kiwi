#pragma once

#include <array>
#include <kiwi/Trie.hpp>
#include <kiwi/Form.h>
#include <kiwi/PatternMatcher.h>
#include <kiwi/FrozenTrie.h>

#ifdef KIWI_USE_BTREE

#ifdef _WIN32
using ssize_t = ptrdiff_t;
#else
#include <sys/types.h>
#endif

#include <btree/map.h>
#else
#endif

namespace kiwi
{
	class KModelMgr;
	class KnLangModel;

#ifdef KIWI_USE_BTREE
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

		void addPrev(uint16_t distance)
		{
			for (size_t i = 0; i < max_prev; ++i)
			{
				if (prevs[i]) continue;
				prevs[i] = distance;
				return;
			}
			throw std::runtime_error{ "`prevs` is overflowed" };
		}
	};

	template<ArchType arch>
	Vector<KGraphNode> splitByTrie(const utils::FrozenTrie<kchar_t, const Form*>& trie, const KString& str, Match matchOptions);
	
	using FnSplitByTrie = decltype(&splitByTrie<ArchType::default_>);
	FnSplitByTrie getSplitByTrieFn(ArchType arch);

	struct KTrie : public utils::TrieNode<char16_t, const Form*, utils::ConstAccess<map<char16_t, int32_t>>, KTrie>
	{
		const Form* findForm(const KString& str) const;
	};
}
