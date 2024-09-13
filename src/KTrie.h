#pragma once

#include <array>
#include <kiwi/Trie.hpp>
#include <kiwi/Form.h>
#include <kiwi/PatternMatcher.h>
#include <kiwi/FrozenTrie.h>

#include "StrUtils.h"

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

	struct PretokenizedSpanGroup
	{
		struct Span
		{
			uint32_t begin = 0, end = 0;
			const Form* form = nullptr;
		};

		Vector<Span> spans;
		Vector<KString> formStrs;
		Vector<Form> forms;
		Vector<Morpheme> morphemes;

		void clear()
		{
			spans.clear();
			formStrs.clear();
			forms.clear();
			morphemes.clear();
		}
	};

	struct KGraphNode
	{
		U16StringView uform;
		const Form* form = nullptr;
		uint32_t prev = 0, sibling = 0;
		uint32_t startPos = 0, endPos = 0;
		float typoCost = 0;
		uint32_t typoFormId = 0;
		uint32_t spaceErrors = 0;

		KGraphNode(uint16_t _startPos = 0, uint16_t _endPos = 0, const Form* _form = nullptr, float _typoCost = 0) 
			: form(_form), startPos(_startPos), endPos(_endPos), typoCost(_typoCost) {}
		KGraphNode(uint16_t _startPos, uint16_t _endPos, U16StringView _uform, float _typoCost = 0) 
			: uform(_uform), startPos(_startPos), endPos(_endPos), typoCost(_typoCost) {}

		KGraphNode* getPrev() { return prev ? this - prev : nullptr; }
		const KGraphNode* getPrev() const { return prev ? this - prev : nullptr; }

		KGraphNode* getSibling() { return sibling ? this + sibling : nullptr; }
		const KGraphNode* getSibling() const { return sibling ? this + sibling : nullptr; }
	};

	/**
	* @brief string을 분할하여 Form으로 구성된 그래프를 생성한다.
	* @tparam arch Trie탐색에 사용할 CPU 아키텍처 타입
	* @tparam typoTolerant 오타가 포함된 형태를 탐색할지 여부
	* @tparam continualTypoTolerant 연철된 오타를 탐색할지 여부
	* @tparam lengtheningTypoTolerant 여러 음절로 늘려진 오타를 탐색할지 여부
	*/
	template<ArchType arch, 
		bool typoTolerant = false, 
		bool continualTypoTolerant = false,
		bool lengtheningTypoTolerant = false
	>
	size_t splitByTrie(
		Vector<KGraphNode>& out,
		const Form* formBase,
		const size_t* typoPtrs,
		const utils::FrozenTrie<kchar_t, const Form*>& trie, 
		U16StringView str, 
		size_t startOffset,
		Match matchOptions, 
		size_t maxUnkFormSize, 
		size_t spaceTolerance,
		float continualTypoCost,
		float lengtheningTypoCost,
		const PretokenizedSpanGroup::Span*& pretokenizedFirst,
		const PretokenizedSpanGroup::Span* pretokenizedLast
	);

	template<ArchType arch>
	const Form* findForm(
		const utils::FrozenTrie<kchar_t, const Form*>& trie,
		const KString& str
	);
	
	using FnSplitByTrie = decltype(&splitByTrie<ArchType::default_>);
	FnSplitByTrie getSplitByTrieFn(ArchType arch, bool typoTolerant, bool continualTypoTolerant, bool lengtheningTypoTolerant);

	using FnFindForm = decltype(&findForm<ArchType::default_>);
	FnFindForm getFindFormFn(ArchType arch);

	struct KTrie : public utils::TrieNode<char16_t, const Form*, utils::ConstAccess<map<char16_t, int32_t>>, KTrie>
	{
	};
}
