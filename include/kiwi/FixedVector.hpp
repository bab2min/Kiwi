﻿#pragma once
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

		FixedVector(FixedVector&& o)
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

		FixedVector& operator=(FixedVector&& o)
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
}