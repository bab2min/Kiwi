#pragma once
#include <iterator>
#include <kiwi/Types.h>
#include <kiwi/Mmap.h>
#include <kiwi/Utils.h>

namespace kiwi
{
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

		class ConstIterator
		{
			const RaggedVector& rv;
			size_t i;
		public:
			ConstIterator(const RaggedVector& _rv, size_t _i = 0)
				: rv(_rv), i{ _i } // `rv{ _rv }` generates a compilation error at gcc <= 4.8
			{
			}

			bool operator==(const ConstIterator& o) const
			{
				return i == o.i;
			}

			bool operator!=(const ConstIterator& o) const
			{
				return i != o.i;
			}

			ConstIterator& operator++()
			{
				++i;
				return *this;
			}

			auto operator*() const -> decltype(rv[i])
			{
				return rv[i];
			}
		};

		size_t size() const { return ptrs.size(); }

		size_t dataSize() const { return data.size(); }

		const Vector<ValueTy>& raw() const { return data; }

		void resize(size_t i) { data.resize(ptrs[i]); ptrs.resize(i); }

		auto operator[](size_t idx) const -> Range<decltype(data.begin())>
		{
			size_t b = idx < ptrs.size() ? ptrs[idx] : data.size();
			size_t e = idx + 1 < ptrs.size() ? ptrs[idx + 1] : data.size();
			return { data.begin() + b, data.begin() + e };
		}

		auto operator[](size_t idx) -> Range<decltype(data.begin())>
		{
			const size_t b = idx < ptrs.size() ? ptrs[idx] : data.size();
			const size_t e = idx + 1 < ptrs.size() ? ptrs[idx + 1] : data.size();
			return { data.begin() + b, data.begin() + e };
		}

		void emplace_back()
		{
			ptrs.emplace_back(data.size());
		}

		void pop_back()
		{
			data.resize(ptrs.back());
			ptrs.pop_back();
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

		ConstIterator begin() const
		{
			return { *this, 0 };
		}

		ConstIterator end() const
		{
			return { *this, size() };
		}

		utils::MemoryObject to_memory() const
		{
			utils::MemoryOwner ret{ 4 + sizeof(uint64_t) * 2 + sizeof(ValueTy) * data.size() + sizeof(uint64_t) * ptrs.size() };
			utils::omstream ostr{ (char*)ret.get(), (ptrdiff_t)ret.size()};
			write_to_memory(ostr);
			return ret;
		}

		void write_to_memory(std::ostream& ostr) const
		{
			if (!ostr.write("KIRV", 4))
			{
				throw std::runtime_error("Failed to write RaggedVector memory object");
			}
			uint64_t s;
			s = data.size();
			if (!ostr.write((const char*)&s, sizeof(uint64_t)))
			{
				throw std::runtime_error("Failed to write RaggedVector memory object");
			}

			s = ptrs.size();
			if (!ostr.write((const char*)&s, sizeof(uint64_t)))
			{
				throw std::runtime_error("Failed to write RaggedVector memory object");
			}

			if (!ostr.write((const char*)data.data(), sizeof(ValueTy) * data.size()))
			{
				throw std::runtime_error("Failed to write RaggedVector memory object");
			}

			if (!ostr.write((const char*)ptrs.data(), sizeof(uint64_t) * ptrs.size()))
			{
				throw std::runtime_error("Failed to write RaggedVector memory object");
			}
		}

		static RaggedVector from_memory(std::istream& istr)
		{
			RaggedVector ret;
			char buf[4];
			if (!istr.read(buf, 4))
			{
				throw std::runtime_error("Invalid RaggedVector memory object");
			}

			if (memcmp(buf, "KIRV", 4) != 0)
			{
				throw std::runtime_error("Invalid RaggedVector memory object");
			}

			uint64_t s;
			if (!istr.read((char*)&s, sizeof(uint64_t)))
			{
				throw std::runtime_error("Invalid RaggedVector memory object");
			}
			ret.data.resize(s);

			if (!istr.read((char*)&s, sizeof(uint64_t)))
			{
				throw std::runtime_error("Invalid RaggedVector memory object");
			}
			ret.ptrs.resize(s);

			if (!istr.read((char*)ret.data.data(), sizeof(ValueTy) * ret.data.size()))
			{
				throw std::runtime_error("Invalid RaggedVector memory object");
			}

			if (!istr.read((char*)ret.ptrs.data(), sizeof(uint64_t) * ret.ptrs.size()))
			{
				throw std::runtime_error("Invalid RaggedVector memory object");
			}
			return ret;
		}
	};
}
