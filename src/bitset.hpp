#pragma once

#include <utility>
#include <memory>
#include <cstring>

#include <kiwi/BitUtils.h>

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
			constexpr size_t getSizeOfBits(size_t i)
			{
				return i ? (getSizeOfBits(i >> 1) + 1) : 0;
			}
		}

		class Bitset
		{
			union {
				struct {
					size_t* _data;
					size_t _size;
				};

				size_t _fields[2];
			};
		
			static constexpr size_t uintSize = sizeof(size_t) * 8;
			static constexpr size_t fieldSize = uintSize * 2;
			static constexpr size_t prefixSize = detail::getSizeOfBits(fieldSize - 1);
			static constexpr size_t realFieldSize = fieldSize - prefixSize;

			size_t getPrefix() const
			{
				return _size >> (uintSize - prefixSize);
			}

			size_t sizeInUint() const
			{
				return (_size + uintSize - 1) / uintSize;
			}

		public:
			Bitset(size_t size = 0)
			{
				if (size == 0)
				{
					_fields[0] = 0;
					_fields[1] = 0;
				}
				else if (size < realFieldSize)
				{
					_fields[0] = 0;
					_fields[1] = size << (uintSize - prefixSize);
				}
				else
				{
					_size = size;
					_data = new size_t[(size + uintSize - 1) / uintSize];
					std::memset(_data, 0, sizeof(size_t) * (size + uintSize - 1) / uintSize);
				}
			}

			~Bitset()
			{
				if (!getPrefix())
				{
					delete[] _data;
					_data = nullptr;
					_size = 0;
				}
			}

			Bitset(const Bitset& o)
			{
				if (o.getPrefix())
				{
					_fields[0] = o._fields[0];
					_fields[1] = o._fields[1];
				}
				else
				{
					_size = o._size;
					_data = new size_t[o.sizeInUint()];
					std::memcpy(_data, o._data, o.sizeInUint() * sizeof(size_t));
				}
			}

			Bitset(Bitset&& o) noexcept
			{
				_fields[0] = 0;
				_fields[1] = 0;
				std::swap(_fields, o._fields);
			}

			Bitset& operator=(const Bitset& o)
			{
				if (!getPrefix()) delete[] _data;
				new (this) Bitset(o);
				return *this;
			}

			Bitset& operator=(Bitset&& o) noexcept
			{
				std::swap(_fields, o._fields);
				return *this;
			}

			size_t* data()
			{
				if (getPrefix()) return reinterpret_cast<size_t*>(this);
				return _data;
			}

			const size_t* data() const
			{
				if (getPrefix()) return reinterpret_cast<const size_t*>(this);
				return _data;
			}

			size_t size() const
			{
				size_t s = getPrefix();
				if (s) return s;
				return _size;
			}

			bool operator[](size_t i) const
			{
				return get(i);
			}

			bool get(size_t i) const
			{
				size_t e = i / uintSize;
				size_t b = i % uintSize;
				return !!(data()[e] & ((size_t)1 << b));
			}

			void set(size_t i, bool on = true)
			{
				size_t e = i / uintSize;
				size_t b = i % uintSize;
				if (on)
				{
					data()[e] |= (size_t)1 << b;
				}
				else
				{
					data()[e] &= ~((size_t)1 << b);
				}
			}

			bool any() const
			{
				if (getPrefix())
				{
					return _fields[0] || (_fields[1] & (((size_t)1 << (uintSize - prefixSize)) - 1));
				}
				else
				{
					for (size_t i = 0; i < sizeInUint(); ++i)
					{
						if (data()[i]) return true;
					}
					return false;
				}
			}

			Bitset& operator&=(const Bitset& o)
			{
				if (getPrefix())
				{
					_fields[0] &= o._fields[0];
					_fields[1] &= o._fields[1] | ~(((size_t)1 << (uintSize - prefixSize)) - 1);
				}
				else
				{
					for (size_t i = 0; i < sizeInUint(); ++i)
					{
						data()[i] &= o.data()[i];
					}
				}
				return *this;
			}

			Bitset& operator|=(const Bitset& o)
			{
				if (getPrefix())
				{
					_fields[0] |= o._fields[0];
					_fields[1] |= o._fields[1] & (((size_t)1 << (uintSize - prefixSize)) - 1);
				}
				else
				{
					for (size_t i = 0; i < sizeInUint(); ++i)
					{
						data()[i] |= o.data()[i];
					}
				}
				return *this;
			}

			Bitset& operator^=(const Bitset& o)
			{
				if (getPrefix())
				{
					_fields[0] ^= o._fields[0];
					_fields[1] ^= o._fields[1] & (((size_t)1 << (uintSize - prefixSize)) - 1);
				}
				else
				{
					for (size_t i = 0; i < sizeInUint(); ++i)
					{
						data()[i] ^= o.data()[i];
					}
				}
				return *this;
			}

			Bitset& operator-=(const Bitset& o)
			{
				if (getPrefix())
				{
					_fields[0] &= ~o._fields[0];
					_fields[1] &= ~o._fields[1] | ~(((size_t)1 << (uintSize - prefixSize)) - 1);
				}
				else
				{
					for (size_t i = 0; i < sizeInUint(); ++i)
					{
						data()[i] &= ~o.data()[i];
					}
				}
				return *this;
			}

			template<class Fn>
			void visit(Fn&& fn) const
			{
				size_t s = (size() + (uintSize - 1)) / uintSize;
				auto* ptr = data();
				for (size_t i = 0; i < s - 1; ++i)
				{
					auto v = ptr[i];
					while (v)
					{
						auto j = countTrailingZeroes(v);
						fn(i * uintSize + j);
						v ^= ((size_t)1 << j);
					}
				}
				size_t i = s - 1;
				auto v = ptr[i];
				while (v)
				{
					auto j = countTrailingZeroes(v);
					if (i * uintSize + j >= size()) break;
					fn(i * uintSize + j);
					v ^= ((size_t)1 << j);
				}
			}
		};
	}
}
