#pragma once
#include <iterator>
#include <kiwi/Types.h>
#include <kiwi/Mmap.h>

namespace kiwi
{
	template<class It>
	class Range : std::pair<It, It>
	{
	public:
		using std::pair<It, It>::pair;
		using Reference = decltype(*std::declval<It>());

		It begin() const
		{
			return this->first;
		}

		It end() const
		{
			return this->second;
		}

		It begin()
		{
			return this->first;
		}

		It end()
		{
			return this->second;
		}

		std::reverse_iterator<It> rbegin() const
		{
			return std::reverse_iterator<It>{ this->second };
		}

		std::reverse_iterator<It> rend() const
		{
			return std::reverse_iterator<It>{ this->first };
		}

		std::reverse_iterator<It> rbegin()
		{
			return std::reverse_iterator<It>{ this->second };
		}

		std::reverse_iterator<It> rend()
		{
			return std::reverse_iterator<It>{ this->first };
		}

		size_t size() const { return this->second - this->first; }

		const Reference operator[](size_t idx) const
		{
			return this->first[idx];
		}

		Reference operator[](size_t idx)
		{
			return this->first[idx];
		}
	};

	template<class ValueTy>
	class RaggedVector
	{
		Vector<ValueTy> data;
		Vector<size_t> ptrs;
	public:

		class Iterator
		{
			RaggedVector& rv;
			size_t i;
		public:
			Iterator(RaggedVector& _rv, size_t _i = 0)
				: rv(_rv), i{_i} // `rv{ _rv }` generates a compilation error at gcc <= 4.8
			{
			}

			bool operator==(const Iterator& o) const
			{
				return i == o.i;
			}

			bool operator!=(const Iterator& o) const
			{
				return i != o.i;
			}

			Iterator& operator++()
			{
				++i;
				return *this;
			}

			auto operator*() const -> decltype(rv[i])
			{
				return rv[i];
			}
		};

		size_t size() const { return ptrs.size(); };

		void resize(size_t i) { data.resize(ptrs[i]); ptrs.resize(i); }

		auto operator[](size_t idx) const -> Range<decltype(data.begin())>
		{
			size_t b = idx < ptrs.size() ? ptrs[idx] : data.size();
			size_t e = idx + 1 < ptrs.size() ? ptrs[idx + 1] : data.size();
			return { data.begin() + b, data.begin() + e };
		}

		auto operator[](size_t idx) -> Range<decltype(data.begin())>
		{
			size_t b = idx < ptrs.size() ? ptrs[idx] : data.size();
			size_t e = idx + 1 < ptrs.size() ? ptrs[idx + 1] : data.size();
			return { data.begin() + b, data.begin() + e };
		}

		void emplace_back()
		{
			ptrs.emplace_back(data.size());
		}

		template<class... Args>
		void add_data(Args&&... args)
		{
			data.emplace_back(std::forward<Args>(args)...);
		}

		template<class It>
		void insert_data(It first, It last)
		{
			data.insert(data.end(), first, last);
		}

		Iterator begin()
		{
			return { *this, 0 };
		}

		Iterator end()
		{
			return { *this, size() };
		}

		utils::MemoryObject toMemory() const
		{
			utils::MemoryOwner ret{ sizeof(size_t) * 2 + sizeof(ValueTy) * data.size() + sizeof(size_t) * ptrs.size() };
			utils::omstream ostr{ (char*)ret.get(), (ptrdiff_t)ret.size()};
			size_t s;
			s = data.size();
			ostr.write((const char*)&s, sizeof(size_t));
			s = ptrs.size();
			ostr.write((const char*)&s, sizeof(size_t));
			ostr.write((const char*)data.data(), sizeof(ValueTy) * data.size());
			ostr.write((const char*)ptrs.data(), sizeof(size_t) * ptrs.size());
			return ret;
		}

		static RaggedVector fromMemory(std::istream& istr)
		{
			RaggedVector ret;
			size_t s;
			istr.read((char*)&s, sizeof(size_t));
			ret.data.resize(s);
			istr.read((char*)&s, sizeof(size_t));
			ret.ptrs.resize(s);
			istr.read((char*)ret.data.data(), sizeof(ValueTy) * ret.data.size());
			istr.read((char*)ret.ptrs.data(), sizeof(size_t) * ret.ptrs.size());
			return ret;
		}
	};
}