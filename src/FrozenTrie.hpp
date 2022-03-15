#pragma once

#include <kiwi/FrozenTrie.h>
#include <kiwi/Utils.h>
#include "search.h"

namespace kiwi
{
	namespace utils
	{
		template<class _Key, class _Value, class _Diff>
		template<ArchType arch>
		auto FrozenTrie<_Key, _Value, _Diff>::Node::nextOpt(const FrozenTrie<_Key, _Value, _Diff>& ft, Key c) const -> const Node*
		{
			_Diff v;
			if (!nst::search<arch>(&ft.nextKeys[nextOffset], &ft.nextDiffs[nextOffset], numNexts, c, v))
			{
				return nullptr;
			}
			return this + v;
		}

		template<class _Key, class _Value, class _Diff>
		auto FrozenTrie<_Key, _Value, _Diff>::Node::fail() const -> const Node*
		{
			if (!lower) return nullptr;
			return this + lower;
		}

		template<class _Key, class _Value, class _Diff>
		template<ArchType arch>
		auto FrozenTrie<_Key, _Value, _Diff>::Node::findFail(const FrozenTrie& ft, Key c) const -> const Node*
		{
			if (!lower) return this;
			auto* lowerNode = this + lower;
			_Diff v;

			if (!nst::search<arch>(
				&ft.nextKeys[lowerNode->nextOffset],
				&ft.nextDiffs[lowerNode->nextOffset],
				lowerNode->numNexts, c, v
			))
			{
				// `c` node doesn't exist
				return lowerNode->template findFail<arch>(ft, c);
			}
			else
			{
				return lowerNode + v;
			}
		}

		template<class _Key, class _Value, class _Diff>
		auto FrozenTrie<_Key, _Value, _Diff>::Node::val(const FrozenTrie<_Key, _Value, _Diff>& ft) const -> const Value&
		{
			return ft.values[this - ft.nodes.get()];
		}

		template<class _Key, class _Value, class _Diff>
		FrozenTrie<_Key, _Value, _Diff>::FrozenTrie(const FrozenTrie& o)
			: numNodes{ o.numNodes }, numNexts{ o.numNexts }
		{
			nodes = make_unique<Node[]>(numNodes);
			values = make_unique<Value[]>(numNodes);
			nextKeys = make_unique<Key[]>(numNexts);
			nextDiffs = make_unique<Diff[]>(numNexts);

			std::copy(o.nodes.get(), o.nodes.get() + numNodes, nodes.get());
			std::copy(o.values.get(), o.values.get() + numNodes, values.get());
			std::copy(o.nextKeys.get(), o.nextKeys.get() + numNexts, nextKeys.get());
			std::copy(o.nextDiffs.get(), o.nextDiffs.get() + numNexts, nextDiffs.get());
		}

		template<class _Key, class _Value, class _Diff>
		auto FrozenTrie<_Key, _Value, _Diff>::operator=(const FrozenTrie& o) -> FrozenTrie&
		{
			numNodes = o.numNodes;
			numNexts = o.numNexts;

			nodes = make_unique<Node[]>(numNodes);
			values = make_unique<Value[]>(numNodes);
			nextKeys = make_unique<Key[]>(numNexts);
			nextDiffs = make_unique<Diff[]>(numNexts);

			std::copy(o.nodes.get(), o.nodes.get() + numNodes, nodes.get());
			std::copy(o.values.get(), o.values.get() + numNodes, values.get());
			std::copy(o.nextKeys.get(), o.nextKeys.get() + numNexts, nextKeys.get());
			std::copy(o.nextDiffs.get(), o.nextDiffs.get() + numNexts, nextDiffs.get());
			return *this;
		}

		template<class _Key, class _Value, class _Diff>
		template<class TrieNode, ArchType archType>
		FrozenTrie<_Key, _Value, _Diff>::FrozenTrie(const ContinuousTrie<TrieNode>& trie, ArchTypeHolder<archType>)
		{
			numNodes = trie.size();
			nodes = make_unique<Node[]>(numNodes);
			values = make_unique<Value[]>(numNodes);

			for (size_t i = 0; i < trie.size(); ++i)
			{
				numNexts += trie[i].next.size();
			}

			nextKeys = make_unique<Key[]>(numNexts);
			nextDiffs = make_unique<Diff[]>(numNexts);

			size_t ptr = 0;
			Vector<uint8_t> tempBuf;
			for (size_t i = 0; i < trie.size(); ++i)
			{
				auto& o = trie[i];
				nodes[i].numNexts = o.next.size();
				values[i] = o.val;
				nodes[i].nextOffset = ptr;

				std::vector<std::pair<Key, Diff>> pairs{ o.next.begin(), o.next.end() };
				std::sort(pairs.begin(), pairs.end());
				for (auto& p : pairs)
				{
					nextKeys[ptr] = p.first;
					nextDiffs[ptr] = p.second;
					++ptr;
				}
				nst::prepare<archType>(&nextKeys[nodes[i].nextOffset], &nextDiffs[nodes[i].nextOffset], pairs.size(), tempBuf);
			}

			Deque<Node*> dq;
			for (dq.emplace_back(&nodes[0]); !dq.empty(); dq.pop_front())
			{
				auto p = dq.front();
				for (size_t i = 0; i < p->numNexts; ++i)
				{
					auto k = nextKeys[p->nextOffset + i];
					auto v = nextDiffs[p->nextOffset + i];
					if (v <= 0) continue;
					auto* child = &p[v];
					child->lower = p->template findFail<archType>(*this, k) - child;
					dq.emplace_back(child);
				}

				if (!p->val(*this))
				{
					for (auto n = p; n->lower; n = const_cast<Node*>(n->fail()))
					{
						if (!n->val(*this)) continue;
						values[p - nodes.get()] = (_Value)this->hasSubmatch;
						break;
					}
				}
			}
		}

	}
}
