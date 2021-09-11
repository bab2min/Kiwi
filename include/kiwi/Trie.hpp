#pragma once
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <iterator>

namespace kiwi
{
	namespace utils
	{
		template<class _Map>
		class ConstAccess : public _Map
		{
		public:
			auto operator[](typename _Map::key_type key) const -> typename _Map::mapped_type
			{
				auto it = this->find(key);
				if (it == this->end()) return {};
				else return it->second;
			}

			auto operator[](typename _Map::key_type key) -> typename _Map::mapped_type&
			{
				auto it = this->find(key);
				if (it == this->end()) return this->emplace(key, typename _Map::mapped_type{}).first->second;
				else return it->second;
			}
		};

		template<class _Map, class _Node>
		class TrieIterator : public _Map::const_iterator
		{
			using Base = typename _Map::const_iterator;
			using Key = typename _Map::key_type;
			const _Node* base = nullptr;
		public:

			TrieIterator(const Base& it, const _Node* _base)
				: Base(it), base(_base)
			{
			}

			std::pair<const Key, const _Node*> operator*() const
			{
				auto p = Base::operator*();
				return std::make_pair(p.first, base + p.second);
			}
		};

		template<class _Key, class _Value, class _KeyStore = ConstAccess<std::unordered_map<_Key, int32_t>>, class _Trie = void>
		struct TrieNode
		{
			using Node = typename std::conditional<std::is_same<_Trie, void>::value, TrieNode, _Trie>::type;
			using Key = _Key;
			using Value = _Value;
			using KeyStore = _KeyStore;
			using iterator = TrieIterator<_KeyStore, Node>;
			_KeyStore next = {};
			_Value val = {};
			int32_t fail = 0;
			uint32_t depth = 0;

			TrieNode() {}
			~TrieNode() {}

			Node* getNext(_Key i) const
			{
				return next[i] ? (Node*)this + next[i] : nullptr;
			}

			Node* getFail() const
			{
				return fail ? (Node*)this + fail : nullptr;
			}

			iterator begin() const
			{
				return { next.begin(), (const Node*)this };
			}

			iterator end() const
			{
				return { next.end(), (const Node*)this };
			}

			template<typename _TyIter, typename _FnAlloc>
			Node* build(_TyIter first, _TyIter last, const _Value& _val, _FnAlloc&& alloc)
			{
				if (first == last)
				{
					if (!val) val = _val;
					return (Node*)this;
				}

				auto v = *first;
				if (!getNext(v))
				{
					next[v] = alloc() - (Node*)this;
					getNext(v)->depth = depth + 1;
				}
				return getNext(v)->build(++first, last, _val, alloc);
			}

			template<typename _TyIter>
			Node* findNode(_TyIter begin, _TyIter end)
			{
				if (begin == end) return (Node*)this;
				auto n = getNext(*begin);
				if (n) return n->findNode(++begin, end);
				return nullptr;
			}

			template<class _Func>
			void traverse(_Func func)
			{
				if (val)
				{
					if (func(val)) return;
				}
				for (auto& p : next)
				{
					if (getNext(p.first))
					{
						getNext(p.first)->traverse(func);
					}
				}
				return;
			}

			template<typename _Fn, typename _CKey>
			void traverseWithKeys(_Fn&& fn, std::vector<_CKey>& rkeys, size_t maxDepth = -1, bool ignoreNegative = false) const
			{
				fn((Node*)this, rkeys);

				if (rkeys.size() >= maxDepth) return;

				for (auto& p : next)
				{
					if (ignoreNegative ? (p.second > 0) : (p.second))
					{
						rkeys.emplace_back(p.first);
						getNext(p.first)->traverseWithKeys(fn, rkeys, maxDepth, ignoreNegative);
						rkeys.pop_back();
					}
				}
			}

			template<class _Iterator>
			std::pair<Node*, size_t> findMaximumMatch(_Iterator begin, _Iterator end, size_t idxCnt = 0) const
			{
				if (begin == end) return std::make_pair((Node*)this, idxCnt);
				auto n = getNext(*begin);
				if (n)
				{
					auto v = n->findMaximumMatch(++begin, end, idxCnt + 1);
					if (v.first->val) return v;
				}
				return std::make_pair((Node*)this, idxCnt);
			}

			Node* findFail(_Key i) const
			{
				if (!fail) // if this is Root
				{
					return (Node*)this;
				}
				else
				{
					if (getFail()->getNext(i)) // if 'i' node exists
					{
						return getFail()->getNext(i);
					}
					else // or loop for failure of this
					{
						return getFail()->findFail(i);
					}
				}
			}

			void fillFail(bool ignoreNegative = false)
			{
				std::deque<Node*> dq;
				for (dq.emplace_back((Node*)this); !dq.empty(); dq.pop_front())
				{
					auto p = dq.front();
					for (auto&& kv : p->next)
					{
						auto i = kv.first;
						if (ignoreNegative && kv.second < 0) continue;
						if (!p->getNext(i)) continue;
						p->getNext(i)->fail = p->findFail(i) - p->getNext(i);
						dq.emplace_back(p->getNext(i));

						if (!p->val)
						{
							for (auto n = p; n->fail; n = n->getFail())
							{
								if (!n->val) continue;
								p->val = (_Value)-1;
								break;
							}
						}
					}
				}
			}
		};

		template<class _Key, class _Value, class _KeyStore = ConstAccess<std::map<_Key, int32_t>>>
		struct TrieNodeEx : public TrieNode<_Key, _Value, _KeyStore, TrieNodeEx<_Key, _Value, _KeyStore>>
		{
			int32_t parent = 0;

			template<typename _TyIter, typename _FnAlloc>
			TrieNodeEx* build(_TyIter first, _TyIter last, const _Value& _val, _FnAlloc&& alloc)
			{
				if (first == last)
				{
					if (!this->val) this->val = _val;
					return this;
				}

				auto v = *first;
				if (!this->getNext(v))
				{
					this->next[v] = alloc() - this;
					this->getNext(v)->parent = -this->next[v];
				}
				return this->getNext(v)->build(++first, last, _val, alloc);
			}

			template<typename _FnAlloc>
			TrieNodeEx* makeNext(const _Key& k, _FnAlloc&& alloc)
			{
				if (!this->next[k])
				{
					this->next[k] = alloc() - this;
					this->getNext(k)->parent = -this->next[k];
					auto f = this->getFail();
					if (f)
					{
						f = f->makeNext(k, std::forward<_FnAlloc>(alloc));
						this->getNext(k)->fail = f - this->getNext(k);
					}
					else
					{
						this->getNext(k)->fail = this - this->getNext(k);
					}
				}
				return this + this->next[k];
			}

			TrieNodeEx* getParent() const
			{
				if (!parent) return nullptr;
				return (TrieNodeEx*)this + parent;
			}
		};

		template<class _TrieNode>
		class ContinuousTrie
		{
			std::vector<_TrieNode> nodes;

		public:
			using Node = _TrieNode;
			//using Key = typename Node::Key;
			//using Value = typename Node::Value;

			ContinuousTrie() = default;
			ContinuousTrie(size_t initSize) : nodes(initSize) {}
			ContinuousTrie(size_t initSize, size_t initReserve) 
			{
				nodes.reserve(initReserve);
				nodes.resize(initSize);
			}

			ContinuousTrie(const ContinuousTrie&) = default;
			ContinuousTrie(ContinuousTrie&&) = default;

			ContinuousTrie& operator=(const ContinuousTrie&) = default;
			ContinuousTrie& operator=(ContinuousTrie&&) = default;

			bool empty() const { return nodes.empty(); }
			size_t size() const { return nodes.size(); }

			auto begin() -> decltype(nodes.begin()) { return nodes.begin(); }
			auto begin() const -> decltype(nodes.begin()) { return nodes.begin(); }
			auto end() -> decltype(nodes.end()) { return nodes.end(); }
			auto end() const -> decltype(nodes.end()) { return nodes.end(); }

			void reserveMore(size_t n)
			{
				if (nodes.capacity() < nodes.size() + n)
				{
					nodes.reserve(std::max(nodes.size() + n, nodes.capacity() + nodes.capacity() / 2));
				}
			}

			Node& operator[](size_t idx) { return nodes[idx]; }
			const Node& operator[](size_t idx) const { return nodes[idx]; }

			Node& root() { return nodes[0]; }
			const Node& root() const { return nodes[0]; }

			Node* newNode()
			{
				nodes.emplace_back();
				return &nodes.back();
			}

			template<class Iter, class Value>
			Node* build(Iter first, Iter last, Value&& val)
			{
				size_t insertSize = std::distance(first, last);
				reserveMore(insertSize);

				return nodes[0].build(first, last, val, [&]() { return newNode(); });
			}

			void fillFail(bool ignoreNegative = false)
			{
				return nodes[0].fillFail(ignoreNegative);
			}

			template<typename _Fn, typename _CKey>
			void traverseWithKeys(_Fn&& fn, std::vector<_CKey>& rkeys, size_t maxDepth = -1, bool ignoreNegative = false) const
			{
				return nodes[0].traverseWithKeys(std::forward<_Fn>(fn), rkeys, maxDepth, ignoreNegative);
			}
		};
	}
}
