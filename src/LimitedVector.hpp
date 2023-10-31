#pragma once

#include <array>

namespace kiwi
{
	namespace utils
	{
		template<class Ty, size_t maxSize>
		class LimitedVector
		{
			size_t length = 0;
			std::array<uint8_t, maxSize * sizeof(Ty)> bytes;

			void destruct()
			{
				for (size_t i = 0; i < length; ++i)
				{
					operator[](i).~Ty();
				}
				length = 0;
			}

		public:
			LimitedVector(size_t size = 0, const Ty& value = {})
				: length{ size }
			{
				for (size_t i = 0; i < length; ++i)
				{
					new (&operator[](i)) Ty{ value };
				}
			}

			LimitedVector(const LimitedVector& v)
				: length{ v.length }
			{
				for (size_t i = 0; i < length; ++i)
				{
					new (&operator[](i)) Ty{ v[i] };
				}
			}

			LimitedVector(LimitedVector&& v)
				: length{ v.length }
			{
				for (size_t i = 0; i < length; ++i)
				{
					new (&operator[](i)) Ty{ std::move(v[i]) };
				}
			}

			~LimitedVector()
			{
				destruct();
			}

			LimitedVector& operator=(const LimitedVector& v)
			{
				destruct();
				new (this) LimitedVector{ v };
				return *this;
			}

			LimitedVector& operator=(LimitedVector&& v)
			{
				destruct();
				new (this) LimitedVector{ std::move(v) };
				return *this;
			}

			size_t size() const 
			{
				return length;
			}

			Ty* data()
			{
				return reinterpret_cast<Ty*>(bytes.data());
			}

			const Ty* data() const
			{
				return reinterpret_cast<Ty*>(bytes.data());
			}

			Ty& operator[](size_t i)
			{
				return data()[i];
			}

			const Ty& operator[](size_t i) const
			{
				return data()[i];
			}

			Ty* begin()
			{
				return data();
			}

			Ty* end()
			{
				return data() + size();
			}

			const Ty* begin() const
			{
				return data();
			}

			const Ty* end() const
			{
				return data() + size();
			}

			template<class... Args>
			void emplace_back(Args&&... args)
			{
				new (&operator[](length)) Ty{ std::forward<Args>(args)... };
				++length;
			}

			void pop_back()
			{
				operator[](length - 1).~Ty();
				--length;
			}

			void reserve(size_t)
			{
				// no op
			}

			Ty& front()
			{
				return operator[](0);
			}

			const Ty& front() const
			{
				return operator[](0);
			}

			Ty& back()
			{
				return operator[](length - 1);
			}

			const Ty& back() const
			{
				return operator[](length - 1);
			}
		};
	}
}
