#pragma once
#include <kiwi/ArchUtils.h>
#include <kiwi/Types.h>

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_AMD64))) || defined(__INTEL_COMPILER) || MM_PREFETCH 
#include <xmmintrin.h> 
#define PREFETCH_T0(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0) 
#elif defined(__GNUC__) 
#define PREFETCH_T0(addr) __builtin_prefetch(reinterpret_cast<const char*>(addr), 0, 3) 
#else 
#define PREFETCH_T0(addr) do {} while (0) 
#endif 

namespace kiwi
{
	namespace nst
	{
		namespace detail
		{
			template<ArchType arch, class IntTy>
			bool searchImpl(const IntTy* keys, size_t size, IntTy target, size_t& ret);

			template<ArchType arch, class IntTy>
			Vector<size_t> reorderImpl(const IntTy* keys, size_t size);
		}

		template<ArchType arch, class IntTy, class Value>
		void prepare(IntTy* keys, Value* values, size_t size, Vector<uint8_t>& tempBuf)
		{
			if (size <= 1) return;
			auto order = detail::reorderImpl<arch>(keys, size);
			if (order.empty()) return;

			if (tempBuf.size() < std::max(sizeof(IntTy), sizeof(Value)) * size)
			{
				tempBuf.resize(std::max(sizeof(IntTy), sizeof(Value)) * size);
			}
			auto tempKeys = (IntTy*)tempBuf.data();
			auto tempValues = (Value*)tempBuf.data();
			std::copy(keys, keys + size, tempKeys);
			for (size_t i = 0; i < size; ++i)
			{
				keys[i] = tempKeys[order[i]];
			}

			std::copy(values, values + size, tempValues);
			for (size_t i = 0; i < size; ++i)
			{
				values[i] = tempValues[order[i]];
			}
		}

		template<ArchType arch, class IntTy, class Value, class Out>
		bool search(const IntTy* keys, const Value* values, size_t size, IntTy target, Out& ret)
		{
			size_t idx;
			if (detail::searchImpl<arch>(keys, size, target, idx))
			{
				ret = values[idx];
				return true;
			}
			else return false;
		}

		template<ArchType arch, class IntTy, class Out>
		bool search(const IntTy* keys, size_t size, IntTy target, Out& ret)
		{
			size_t idx;
			if (detail::searchImpl<arch>(keys, size, target, idx))
			{
				ret = idx;
				return true;
			}
			else return false;
		}
	}
}
