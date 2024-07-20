#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>
#include <kiwi/ArchUtils.h>
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
				static constexpr bool isNull(const Value& v)
				{
					return !v;
				}

				static void setHasSubmatch(Value& v)
				{
					v = (Value)-1;
				}

				static constexpr bool hasSubmatch(Value v)
				{
					return v == (Value)-1;
				}
			};

			template<class Value>
			struct HasSubmatch<Value, typename std::enable_if<std::is_pointer<Value>::value>::type>
			{
				static constexpr bool isNull(const Value& v)
				{
					return !v;
				}

				static void setHasSubmatch(Value& v)
				{
					v = (Value)-1;
				}

				static constexpr bool hasSubmatch(Value v)
				{
					return v == (Value)-1;
				}
			};

			struct NodeToVal
			{
				template<class T>
				constexpr auto operator()(const T& t) const noexcept -> decltype(t.val)
				{
					return t.val;
				}
			};
		}

		template<class _Key, class _Value, class _Diff = int32_t, class _HasSubmatch = detail::HasSubmatch<_Value>>
		class FrozenTrie : public _HasSubmatch
		{
		public:
			using Key = _Key;
			using Value = _Value;
			using Diff = _Diff;

			struct Node
			{
				Key numNexts = 0;
				Diff lower = 0;
				uint32_t nextOffset = 0;

				template<ArchType arch>
				const Node* nextOpt(const FrozenTrie& ft, Key c) const;

				template<ArchType arch>
				const Node* findFail(const FrozenTrie& ft, Key c) const;

				const Node* fail() const;
				const Value& val(const FrozenTrie& ft) const;
			};
		private:
			size_t numNodes = 0;
			size_t numNexts = 0;
			std::unique_ptr<Node[]> nodes;
			std::unique_ptr<Value[]> values;
			std::unique_ptr<Key[]> nextKeys;
			std::unique_ptr<Diff[]> nextDiffs;

			template<class Fn>
			void traverse(Fn&& visitor, const Node* node, std::vector<Key>& prefix, size_t maxDepth) const
			{
				auto* keys = &nextKeys[node->nextOffset];
				auto* diffs = &nextDiffs[node->nextOffset];
				for (size_t i = 0; i < node->numNexts; ++i)
				{
					const auto* child = node + diffs[i];
					const auto val = child->val(*this);
					if (!hasMatch(val)) continue;
					prefix.emplace_back(keys[i]);
					visitor(val, prefix);
					if (prefix.size() < maxDepth)
					{
						traverse(visitor, child, prefix, maxDepth);
					}
					prefix.pop_back();
				}
			}

		public:

			FrozenTrie() = default;

			template<class TrieNode, ArchType archType, class Xform = detail::NodeToVal>
			FrozenTrie(const ContinuousTrie<TrieNode>& trie, ArchTypeHolder<archType>, Xform xform = {});

			FrozenTrie(const FrozenTrie& o);
			FrozenTrie(FrozenTrie&&) noexcept = default;

			FrozenTrie& operator=(const FrozenTrie& o);
			FrozenTrie& operator=(FrozenTrie&& o) noexcept = default;

			bool empty() const { return !numNodes; }
			size_t size() const { return numNodes; }
			const Node* root() const { return nodes.get(); }

			const Value& value(size_t idx) const { return values[idx]; };

			bool hasMatch(_Value v) const { return !this->isNull(v) && !this->hasSubmatch(v); }

			template<class Fn>
			void traverse(Fn&& visitor, size_t maxDepth = -1) const
			{
				std::vector<Key> prefix;
				traverse(std::forward<Fn>(visitor), root(), prefix, maxDepth);
			}
		};
	}
}
