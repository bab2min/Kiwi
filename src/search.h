#pragma once
#include <algorithm>
#include <immintrin.h>
#include <kiwi/BitUtils.h>

namespace kiwi
{
	namespace utils
	{
		template<class KeyIt, class ValueIt, class Key, class Out>
		bool bsearchStd(KeyIt keys, ValueIt values, size_t size, Key target, Out& ret)
		{
			if (target < keys[0] || keys[size - 1] < target) return false;
			auto it = std::lower_bound(keys, keys + size, target);
			if (it == keys + size || *it != target) return false;
			ret = values[it - keys];
			return true;
		}

		template<class IntTy, class ValueIt, class Out>
		bool bsearchMixed(const IntTy* keys, ValueIt values, size_t size, IntTy target, Out& ret)
		{
			if (target < keys[0] || keys[size - 1] < target) return false;
			if (size <= 16)
			{
				for (size_t i = 0; i < size; ++i)
				{
					if (keys[i] == target) return ret = values[i], true;
					if (keys[i] > target) return false;
				}
				return false;
			}
			else
			{
				auto it = std::lower_bound(keys, keys + size, target);
				if (it == keys + size || *it != target) return false;
				ret = values[it - keys];
				return true;
			}
		}

		template<class IntTy, class ValueIt, class Out>
		bool bsearch(const IntTy* keys, ValueIt values, size_t size, IntTy target, Out& ret)
		{
			static constexpr size_t cacheLineSize = 64;
#ifdef __AVX2__
			static constexpr size_t min_h = 4;
#else
			static constexpr size_t min_h = 0;
#endif
			size_t height = ceilLog2(size + 1);
			size_t dist = 1 << (height - 1);
			size_t mid = size - dist;
			dist >>= 1;
			size_t left1 = 0, left2 = mid + 1;
			while (height-- > min_h)
			{
				if (dist >= cacheLineSize / sizeof(IntTy))
				{
					_mm_prefetch((const char*)&keys[left1 + dist - 1], _MM_HINT_T0);
					_mm_prefetch((const char*)&keys[left2 + dist - 1], _MM_HINT_T0);
				}
				if (target > keys[mid]) left1 = left2;
				left2 = left1 + dist;
				mid = left1 + dist - 1;
				dist >>= 1;
			}
#ifdef __AVX2__
			uint32_t mask;
			__m256i ptarget = _mm256_set1_epi16(target);
			mask = _mm256_movemask_epi8(_mm256_cmpeq_epi16(
				_mm256_loadu_si256((const __m256i*)&keys[left1]),
				ptarget
			));
			size_t i = countTrailingZeroes(mask);
			if (mask && left1 + (i >> 1) < size)
			{
				ret = values[left1 + (i >> 1)];
				return true;
			}
			return false;
#else
			if (left1 == size || keys[left1] != target) return false;
			ret = values[left1];
			return true;
#endif
		}
	}
}