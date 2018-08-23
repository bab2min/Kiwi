#pragma once

#include <map>
#include <algorithm>

#ifdef USE_UNORDERED_MAP
#include <unordered_map>

template<class Key, class Value>
class BakedMap : public std::unordered_map<Key, Value>
{
public:
	using std::unordered_map<Key, Value>::unordered_map;
};
#else

template<class Key, class Value>
class BakedMap
{
	typedef std::pair<const Key, Value>* iterator;
protected:
	std::pair<Key, Value>* elems = nullptr;
	size_t length = 0;
public:
	BakedMap() {}

	template<class Input>
	BakedMap(Input begin, Input end) : length(std::distance(begin, end))
	{
		if (length)
		{
			elems = new std::pair<Key, Value>[length];

			size_t i = 0;
			for (; begin != end; ++begin)
			{
				elems[i] = *begin;
				i++;
			}
		}
	}

	BakedMap(const std::map<Key, Value>& m) : length(m.size())
	{
		if (length)
		{
			elems = new std::pair<Key, Value>[length];

			size_t i = 0;
			for (auto& p : m)
			{
				elems[i] = p;
				i++;
			}
		}
	}

	BakedMap(BakedMap&& o)
	{
		swap(o);
	}

	~BakedMap()
	{
		if (elems)
		{
			delete[] elems;
			elems = nullptr;
		}
	}

	BakedMap& operator= (BakedMap&& o)
	{
		swap(o);
		return *this;
	}

	void swap(BakedMap& o)
	{
		std::swap(o.elems, elems);
		std::swap(o.length, length);
	}

	iterator find(const Key& key) const
	{
		auto ret = (iterator)std::lower_bound(elems, elems + length, key, [](const std::pair<Key, Value>& p, const Key& k)
		{
			return p.first < k;
		});
		if (ret == end() || ret->first == key) return ret;
		return end();
	}

	Value operator[](const Key& key) const
	{
		auto pos = find(key);
		if (pos == (size_t)-1) return {};
		return elems[pos].second;
	}

	size_t size() const { return length; }

	iterator begin() { return (iterator)elems; }
	iterator end() { return (iterator)elems + length; }

	const iterator begin() const { return (iterator)elems; }
	const iterator end() const { return (iterator)elems + length; }
};

#endif
