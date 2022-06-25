#pragma once
#include <memory>

namespace kiwi
{
	template<class Ty>
	class FixedVector
	{
		void* _data = nullptr;
	public:
		FixedVector(size_t s = 0)
		{
			if (s)
			{
				_data = std::malloc(sizeof(Ty) * s + sizeof(size_t));
				*(size_t*)_data = s;
				for (size_t i = 0; i < s; ++i)
				{
					new (&operator[](i)) Ty;
				}
			}
			else _data = nullptr;
		}

		FixedVector(const FixedVector& o)
		{
			if (!o.empty())
			{
				_data = std::malloc(sizeof(Ty) * o.size() + sizeof(size_t));
				*(size_t*)_data = o.size();
				for (size_t i = 0; i < o.size(); ++i)
				{
					new (&operator[](i)) Ty{ o[i] };
				}
			}
		}

		FixedVector(FixedVector&& o) noexcept
		{
			std::swap(_data, o._data);
		}

		~FixedVector()
		{
			if (!_data) return;
			for (auto& p : *this) p.~Ty();
			std::free(_data);
		}

		FixedVector& operator=(const FixedVector& o)
		{
			this->~FixedVector();
			new (this) FixedVector(o);
			return *this;
		}

		FixedVector& operator=(FixedVector&& o) noexcept
		{
			std::swap(_data, o._data);
			return *this;
		}

		size_t size() const { return _data ? *(const size_t*)_data : 0; }
		bool empty() const { return !size(); }

		Ty* data() { return _data ? (Ty*)((size_t*)_data + 1) : nullptr; }
		const Ty* data() const { return _data ? (const Ty*)((const size_t*)_data + 1) : nullptr; }

		Ty* begin() { return data(); }
		Ty* end() { return data() + size(); }
		const Ty* begin() const { return data(); }
		const Ty* end() const { return data() + size(); }

		Ty& operator[](size_t i) { return data()[i]; }
		const Ty& operator[](size_t i) const { return data()[i]; }
	};

	template<class Ty1, class Ty2>
	class FixedPairVector
	{
		void* _data = nullptr;
	public:
		FixedPairVector(size_t s = 0)
		{
			if (s)
			{
				_data = std::malloc((sizeof(Ty1) + sizeof(Ty2)) * s + sizeof(size_t));
				*(size_t*)_data = s;
				for (size_t i = 0; i < s; ++i)
				{
					new (&operator[](i)) Ty1;
				}
				for (size_t i = 0; i < s; ++i)
				{
					new (&getSecond(i)) Ty2;
				}
			}
			else _data = nullptr;
		}

		FixedPairVector(const FixedPairVector& o)
		{
			if (!o.empty())
			{
				_data = std::malloc((sizeof(Ty1) + sizeof(Ty2)) * o.size() + sizeof(size_t));
				*(size_t*)_data = o.size();
				for (size_t i = 0; i < o.size(); ++i)
				{
					new (&operator[](i)) Ty1{ o[i] };
				}
				for (size_t i = 0; i < o.size(); ++i)
				{
					new (&getSecond(i)) Ty2{ o.getSecond(i) };
				}
			}
		}

		FixedPairVector(FixedPairVector&& o) noexcept
		{
			std::swap(_data, o._data);
		}

		~FixedPairVector()
		{
			if (!_data) return;
			for (auto& p : *this) p.~Ty1();
			for (size_t i = 0; i < size(); ++i)
			{
				getSecond(i).~Ty2();
			}
			std::free(_data);
		}

		FixedPairVector& operator=(const FixedPairVector& o)
		{
			this->~FixedPairVector();
			new (this) FixedPairVector(o);
			return *this;
		}

		FixedPairVector& operator=(FixedPairVector&& o) noexcept
		{
			std::swap(_data, o._data);
			return *this;
		}

		size_t size() const { return _data ? *(const size_t*)_data : 0; }
		bool empty() const { return !size(); }

		Ty1* data() { return _data ? (Ty1*)((size_t*)_data + 1) : nullptr; }
		const Ty1* data() const { return _data ? (const Ty1*)((const size_t*)_data + 1) : nullptr; }

		Ty1* begin() { return data(); }
		Ty1* end() { return data() + size(); }
		const Ty1* begin() const { return data(); }
		const Ty1* end() const { return data() + size(); }

		Ty1& operator[](size_t i) { return data()[i]; }
		const Ty1& operator[](size_t i) const { return data()[i]; }

		Ty2* dataSecond() { return _data ? (Ty2*)((Ty1*)((size_t*)_data + 1) + size()) : nullptr; }
		const Ty2* dataSecond() const { return _data ? (Ty2*)((Ty1*)((size_t*)_data + 1) + size()) : nullptr; }

		Ty2& getSecond(size_t i) { return dataSecond()[i]; }
		const Ty2& getSecond(size_t i) const { return dataSecond()[i]; }
	};
}

