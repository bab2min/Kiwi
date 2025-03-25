#pragma once
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <iterator>
#include <kiwi/Types.h>

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
				return this->emplace(key, typename _Map::mapped_type{}).first->second;
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

			template<class _FnAlloc>
			Node* buildNext(const _Key& k, _FnAlloc&& alloc)
			{
				auto* n = getNext(k);
				if (n) return n;
				n = alloc();
				next[k] = n - (Node*)this;
				n->depth = depth + 1;
				return n;
			}

			template<typename _TyIter, typename _FnAlloc>
			Node* build(_TyIter first, _TyIter last, const _Value& _val, _FnAlloc&& alloc)
			{
				Node* node = (Node*)this;
				for (; first != last; ++first)
				{
					node = node->buildNext(*first, alloc);
				}
				if (!node->val) node->val = _val;
				return node;
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
			void traverse(_Fn&& fn, std::vector<_CKey>& rkeys, size_t maxDepth = -1, bool ignoreNegative = false) const
			{
				fn(this->val, rkeys);

				if (rkeys.size() >= maxDepth) return;

				for (auto& p : next)
				{
					if (ignoreNegative ? (p.second > 0) : (p.second))
					{
						rkeys.emplace_back(p.first);
						getNext(p.first)->traverse(fn, rkeys, maxDepth, ignoreNegative);
						rkeys.pop_back();
					}
				}
			}

			template<typename _Fn, typename _CKey, typename _Alloc>
			void traverseWithKeys(_Fn&& fn, std::vector<_CKey, _Alloc>& rkeys, size_t maxDepth = -1, bool ignoreNegative = false) const
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

			template<class _FnAlloc>
			TrieNodeEx* buildNext(const _Key& k, _FnAlloc&& alloc)
			{
				auto* n = this->getNext(k);
				if (n) return n;
				n = alloc();
				this->next[k] = n - this;
				n->parent = this - n;
				return n;
			}

			template<typename _TyIter, typename _FnAlloc>
			TrieNodeEx* build(_TyIter first, _TyIter last, const _Value& _val, _FnAlloc&& alloc)
			{
				TrieNodeEx* node = (TrieNodeEx*)this;
				for (; first != last; ++first)
				{
					node = node->buildNext(*first, alloc);
				}
				if (!node->val) node->val = _val;
				return node;
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

			template<typename _FnAlloc, typename _HistoryTx>
			TrieNodeEx* makeNext(const _Key& k, _FnAlloc&& alloc, _HistoryTx&& htx)
			{
				if (!this->next[k])
				{
					this->next[k] = alloc() - this;
					this->getNext(k)->parent = -this->next[k];
					auto f = this->getFail();
					if (f)
					{
						if (f->fail)
						{
							f = f->makeNext(k, std::forward<_FnAlloc>(alloc), std::forward<_HistoryTx>(htx));
							this->getNext(k)->fail = f - this->getNext(k);
						}
						else // the fail node of this is a root node
						{
							f = f->makeNext(htx(k), std::forward<_FnAlloc>(alloc));
							this->getNext(k)->fail = f - this->getNext(k);
						}
					}
					else // this node is a root node
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

			using TrieNode<_Key, _Value, _KeyStore, TrieNodeEx<_Key, _Value, _KeyStore>>::fillFail;

			template<class HistoryTx>
			void fillFail(HistoryTx&& htx, bool ignoreNegative = false)
			{
				std::deque<TrieNodeEx*> dq;
				for (dq.emplace_back(this); !dq.empty(); dq.pop_front())
				{
					auto p = dq.front();
					for (auto&& kv : p->next)
					{
						auto i = kv.first;
						if (ignoreNegative && kv.second < 0) continue;
						if (!p->getNext(i)) continue;
						
						if (p->getParent() == this)
						{
							p->getNext(i)->fail = this->getNext(htx(i)) - p->getNext(i);
						}
						else
						{
							p->getNext(i)->fail = p->findFail(i) - p->getNext(i);
						}
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

		template<class _TrieNode>
		class ContinuousTrie
		{
			Vector<_TrieNode> nodes;

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
			ContinuousTrie(ContinuousTrie&&) noexcept = default;

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
					nodes.reserve(std::max(nodes.size() + n, nodes.capacity() * 2));
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

				return nodes[0].build(first, last, std::forward<Value>(val), [&]() { return newNode(); });
			}

			template<class Iter>
			Node* find(Iter first, Iter last)
			{
				return nodes[0].findNode(first, last);
			}

			template<class Cont>
			struct CacheStore
			{
				Cont cont;
				std::vector<size_t> ptrs;

				operator bool() const { return !cont.empty(); }
				const Cont& operator*() const { return cont; }
				void set(const Cont& _cont) { cont = _cont; }
			};

			template<class Cont>
			struct CacheStore<Cont*>
			{
				Cont* cont = nullptr;
				std::vector<size_t> ptrs;

				operator bool() const { return cont; }
				const Cont& operator*() const { return *cont; }
				void set(const Cont& _cont) { cont = &_cont; }
			};

			template<class Cont, class Value, class CacheCont>
			Node* buildWithCaching(Cont&& cont, Value&& val, CacheStore<CacheCont>& cache)
			{
				static_assert(std::is_pointer<CacheCont>::value ? std::is_reference<Cont>::value && !std::is_rvalue_reference<Cont>::value : true,
					"Cont should reference type if using pointer type CacheStore.");
				auto allocNode = [&]() { return newNode(); };
				//reserveMore(cont.size());
				
				size_t commonPrefix = 0;
				if (!!cache)
				{
					while (commonPrefix < std::min((*cache).size(), cont.size())
						&& cont[commonPrefix] == (*cache)[commonPrefix]
					) ++commonPrefix;
				}

				cache.ptrs.resize(cont.size());

				auto* node = &nodes[commonPrefix ? cache.ptrs[commonPrefix - 1] : 0];
				for (size_t i = commonPrefix; i < cont.size(); ++i)
				{
					node = node->buildNext(cont[i], allocNode);
					cache.ptrs[i] = node - nodes.data();
				}
				if (!node->val) node->val = val;
				cache.set(cont);
				return node;
			}

			void fillFail(bool ignoreNegative = false)
			{
				return nodes[0].fillFail(ignoreNegative);
			}

			template<class HistoryTx>
			void fillFail(HistoryTx&& htx, bool ignoreNegative = false)
			{
				return nodes[0].fillFail(std::forward<HistoryTx>(htx), ignoreNegative);
			}

			template<typename _Fn>
			void traverse(_Fn&& fn, size_t maxDepth = -1, bool ignoreNegative = false) const
			{
				std::vector<typename Node::Key> rkeys;
				return nodes[0].traverse(std::forward<_Fn>(fn), rkeys, maxDepth, ignoreNegative);
			}

			template<typename _Fn, typename _CKey, typename _Alloc>
			void traverseWithKeys(_Fn&& fn, std::vector<_CKey, _Alloc>& rkeys, size_t maxDepth = -1, bool ignoreNegative = false) const
			{
				return nodes[0].traverseWithKeys(std::forward<_Fn>(fn), rkeys, maxDepth, ignoreNegative);
			}
		};
	}
}
