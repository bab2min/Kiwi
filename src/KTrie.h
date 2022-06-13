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
		KString uform;
		const Form* form = nullptr;
		uint32_t prev = 0, sibling = 0;
		uint16_t startPos = 0, endPos = 0;
		float typoCost = 0;
		uint32_t typoFormId = 0;

		KGraphNode(const Form* _form = nullptr, uint16_t _endPos = 0, float _typoCost = 0) : form(_form), endPos(_endPos), typoCost(_typoCost) {}
		KGraphNode(const KString& _uform, uint16_t _endPos, float _typoCost = 0) : uform(_uform), endPos(_endPos), typoCost(_typoCost) {}

		KGraphNode* getPrev() { return prev ? this - prev : nullptr; }
		const KGraphNode* getPrev() const { return prev ? this - prev : nullptr; }

		KGraphNode* getSibling() { return sibling ? this + sibling : nullptr; }
		const KGraphNode* getSibling() const { return sibling ? this + sibling : nullptr; }

		static Vector<KGraphNode> removeUnconnected(const Vector<KGraphNode>& graph);
	};

	template<ArchType arch, bool typoTolerant = false>
	Vector<KGraphNode> splitByTrie(
		const Form* formBase,
		const size_t* typoPtrs,
		const utils::FrozenTrie<kchar_t, const Form*>& trie, 
		const KString& str, 
		Match matchOptions, 
		size_t maxUnkFormSize, 
		size_t spaceTolerance,
		float typoCostWeight
	);
	
	using FnSplitByTrie = decltype(&splitByTrie<ArchType::default_>);
	FnSplitByTrie getSplitByTrieFn(ArchType arch, bool typoTolerant);

	struct KTrie : public utils::TrieNode<char16_t, const Form*, utils::ConstAccess<map<char16_t, int32_t>>, KTrie>
	{
		const Form* findForm(const KString& str) const;
	};
}
