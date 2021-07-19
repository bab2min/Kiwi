#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>
#include <kiwi/Trie.hpp>

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
			template<class Value, class = void>
			struct HasSubmatch {};

			template<class Value>
			struct HasSubmatch<Value, typename std::enable_if<std::is_integral<Value>::value>::type>
			{
				static constexpr Value hasSubmatch = (Value)-1;
			};

			template<class Value>
			struct HasSubmatch<Value, typename std::enable_if<std::is_pointer<Value>::value>::type>
			{
				static constexpr ptrdiff_t hasSubmatch = -1;
			};
		}

		template<class _Key, class _Value, class _Diff = int32_t>
		class FrozenTrie : public detail::HasSubmatch<_Value>
		{
		public:
			using Key = _Key;
			using Value = _Value;
			using Diff = _Diff;

			struct Node
			{
				Key numNexts;
				Diff lower;
				uint32_t nextOffset;

				const Node* next(const FrozenTrie& ft, Key c) const;
				const Node* fail() const;
				const Node* findFail(const FrozenTrie& ft, Key c) const;
				const Value& val(const FrozenTrie& ft) const;
			};
		private:
			size_t numNodes = 0;
			size_t numNexts = 0;
			std::unique_ptr<Node[]> nodes;
			std::unique_ptr<Value[]> values;
			std::unique_ptr<Key[]> nextKeys;
			std::unique_ptr<Diff[]> nextDiffs;

		public:

			FrozenTrie() = default;

			template<class TrieNode>
			FrozenTrie(const ContinuousTrie<TrieNode>& trie);

			FrozenTrie(const FrozenTrie& o);
			FrozenTrie(FrozenTrie&&) = default;

			FrozenTrie& operator=(const FrozenTrie& o);
			FrozenTrie& operator=(FrozenTrie&& o) = default;

			bool empty() const { return !numNodes; }
			size_t size() const { return numNodes; }
			const Node* root() const { return nodes.get(); }

			const Value& value(size_t idx) const { return values[idx]; };
		};
	}
}
