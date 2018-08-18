#pragma once
#include <unordered_map>
#include <deque>
#include <functional>
#include <iterator>

template<class _BaseType>
struct EnumerateAdaptor : public _BaseType
{
	typedef decltype(std::begin(_BaseType{})) _BaseIterator;

	class IteratorWithIndex
	{
	private:
		size_t _size = 0;
		_BaseIterator _begin;
	public:
		IteratorWithIndex(_BaseIterator b) : _begin(b) {}
		bool operator!=(const IteratorWithIndex& e) const
		{
			return _begin != e._begin;
		}

		void operator++()
		{
			++_begin;
			++_size;
		}

		auto operator*() const
			-> std::pair<std::size_t, decltype(*_begin)>
		{
			return { _size, *_begin };
		}
	};

	IteratorWithIndex begin() const
	{
		return _BaseType::begin();
	}

	IteratorWithIndex end() const
	{
		return _BaseType::end();
	}
};

template<class _Map>
class OverriddenMap : public _Map
{
public:
	auto operator[](typename _Map::key_type key) const -> typename _Map::mapped_type
	{
		auto it = find(key);
		if (it == end()) return {};
		else return it->second;
	}

	auto operator[](typename _Map::key_type key) -> typename _Map::mapped_type&
	{
		auto it = find(key);
		if (it == end()) return emplace(key, _Map::mapped_type{}).first->second;
		else return it->second;
	}
};

template<class _Key, class _Value, class _KeyStore = std::unordered_map<_Key, int32_t>>
struct Trie
{
	_KeyStore next = {};
	int32_t fail = 0;
	_Value val = {};

	Trie() {}
	~Trie() {}

	Trie* getNext(_Key i) const
	{
		return next[i] ? (Trie*)this + next[i] : nullptr;
	}

	Trie* getFail() const
	{
		return fail ? (Trie*)this + fail : nullptr;
	}

	void build(const _Key* keys, size_t len, const _Value& _val, const std::function<Trie*()>& alloc)
	{
		if (!len)
		{
			if (!val) val = _val;
			return;
		}

		if (!getNext(*keys))
		{
			next[*keys] = alloc() - this;
		}

		getNext(*keys)->build(keys + 1, len - 1, _val, alloc);
	}

	Trie* findFail(_Key i) const
	{
		if (!fail) // if this is Root
		{
			return (Trie*)this;
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

	void fillFail()
	{
		std::deque<Trie*> dq;
		for (dq.emplace_back(this); !dq.empty(); dq.pop_front())
		{
			auto p = dq.front();
			for (auto&& kv : p->next)
			{
				auto i = kv.first;
				if (!p->getNext(i)) continue;
				p->getNext(i)->fail = p->findFail(i) - p->getNext(i);
				dq.emplace_back(p->getNext(i));

				if (!p->val)
				{
					for (auto n = p; n->fail; n = n->getFail())
					{
						if (n->val)
						{
							p->val = (_Value)-1;
							break;
						}
					}
				}
			}
		}
	}

};