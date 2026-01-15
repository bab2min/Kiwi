/**
 * @file FrozenTrie.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 메모리 효율적인 불변(immutable) Trie 자료구조 정의
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * 읽기 전용 Trie 자료구조로, 빠른 문자열 검색과 패턴 매칭을 지원합니다.
 * Aho-Corasick 알고리즘을 위한 실패 링크(fail link)를 포함합니다.
 * 형태소 사전 검색 등에 사용됩니다.
 */

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
			/**
			 * @brief 값이 부분 매칭을 가지는지 확인하는 헬퍼 구조체
			 * @tparam Value 값 타입
			 */
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

		/**
		 * @brief 메모리 효율적인 불변(frozen) Trie 자료구조
		 * 
		 * 빌드 후 수정할 수 없는 Trie로, 메모리 사용량이 최적화되어 있습니다.
		 * Aho-Corasick 알고리즘을 위한 실패 함수(fail function)를 포함하여
		 * 다중 패턴 매칭을 효율적으로 수행할 수 있습니다.
		 * 
		 * @tparam _Key 키(문자) 타입
		 * @tparam _Value 값 타입
		 * @tparam _Diff diff 값의 타입
		 * @tparam _HasSubmatch 부분 매칭 검사 헬퍼
		 */
		template<class _Key, class _Value, class _Diff = int32_t, class _HasSubmatch = detail::HasSubmatch<_Value>>
		class FrozenTrie : public _HasSubmatch
		{
		public:
			using Key = _Key;
			using Value = _Value;
			using Diff = _Diff;

			/**
			 * @brief Trie의 노드 구조체
			 */
			struct Node
			{
				Key numNexts = 0;        /**< 자식 노드의 개수 */
				Diff lower = 0;          /**< 하위 노드로의 오프셋 */
				uint32_t nextOffset = 0; /**< 다음 노드들의 시작 오프셋 */

				/**
				 * @brief 다음 문자에 해당하는 노드를 찾습니다.
				 * @tparam arch 아키텍처 타입 (최적화를 위한)
				 * @param ft FrozenTrie 참조
				 * @param c 다음 문자
				 * @return 찾은 노드 포인터, 없으면 nullptr
				 */
				template<ArchType arch>
				const Node* nextOpt(const FrozenTrie& ft, Key c) const;

				/**
				 * @brief 실패 링크를 따라 다음 노드를 찾습니다.
				 * @tparam arch 아키텍처 타입
				 * @param ft FrozenTrie 참조
				 * @param c 다음 문자
				 * @return 찾은 노드 포인터
				 */
				template<ArchType arch>
				const Node* findFail(const FrozenTrie& ft, Key c) const;

				/**
				 * @brief 실패 링크를 반환합니다.
				 * @return 실패 노드 포인터
				 */
				const Node* fail() const;
				
				/**
				 * @brief 노드의 값을 반환합니다.
				 * @param ft FrozenTrie 참조
				 * @return 노드의 값에 대한 const 참조
				 */
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

			const Node* firstChild(const Node* node) const
			{
				if (node->numNexts == 0) return nullptr;
				auto* keys = &nextKeys[node->nextOffset];
				auto* diffs = &nextDiffs[node->nextOffset];
				auto* first = std::min_element(keys, keys + node->numNexts);
				
				return node + diffs[first - keys];
			}
		};
	}
}
