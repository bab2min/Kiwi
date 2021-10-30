#include <algorithm>
#include <cpuinfo.h>
#include <kiwi/BitUtils.h>
#include "search.h"

#define INSTANTIATE_IMPL(arch) \
	template bool bsearchImpl<arch>(const uint8_t*, size_t, uint8_t, size_t&);\
	template bool bsearchImpl<arch>(const uint16_t*, size_t, uint16_t, size_t&);\
	template bool bsearchImpl<arch>(const uint32_t*, size_t, uint32_t, size_t&);\
	template bool bsearchImpl<arch>(const uint64_t*, size_t, uint64_t, size_t&);\
	template bool bsearchImpl<arch>(const char16_t*, size_t, char16_t, size_t&)

#if defined(CPUINFO_ARCH_X86) || defined(CPUINFO_ARCH_X86_64)
#include <immintrin.h>
#elif defined(CPUINFO_ARCH_ARM64)
#include <arm_neon.h>
#endif

template<class IntTy>
static bool bsearchStd(const IntTy* keys, size_t size, IntTy target, size_t& ret)
{
	if (target < keys[0] || keys[size - 1] < target) return false;
	auto it = std::lower_bound(keys, keys + size, target);
	if (it == keys + size || *it != target) return false;
	ret = it - keys;
	return true;
}

static inline void prefetch(const void* ptr)
{
#if defined(CPUINFO_ARCH_X86) || defined(CPUINFO_ARCH_X86_64)
	_mm_prefetch((const char*)ptr, _MM_HINT_T0);
#elif defined(__GNUC__)
	__builtin_prefetch(ptr);
#endif
}

inline constexpr size_t getBitSize(size_t n)
{
	size_t ret = 0;
	for (; n > 1; n >>= 1) ++ret;
	return ret;
}

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
			template<ArchType arch>
			struct BalancedSearcher
			{
				static constexpr size_t packetBytes = 0;

				template<class IntTy>
				bool lookup(const IntTy* keys, size_t size, size_t left, IntTy target, size_t& ret)
				{
					if (left == size || keys[left] != target) return false;
					ret = left;
					return true;
				}
			};

			template<ArchType arch, class IntTy>
			static bool bsearchBalanced(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				BalancedSearcher<arch> bs;
				static constexpr size_t cacheLineSize = 64 / sizeof(IntTy);
				static constexpr size_t minH = getBitSize(bs.packetBytes / sizeof(IntTy));

				size_t height = ceilLog2(size + 1);
				size_t dist = (size_t)1 << (size_t)(height - 1);
				size_t mid = size - dist;
				dist >>= 1;
				size_t left1 = 0, left2 = mid + 1;
				while (height-- > minH)
				{
					if (dist >= cacheLineSize / sizeof(IntTy))
					{
						prefetch(&keys[left1 + dist - 1]);
						prefetch(&keys[left2 + dist - 1]);
					}
					if (target > keys[mid]) left1 = left2;
					left2 = left1 + dist;
					mid = left1 + dist - 1;
					dist >>= 1;
				}

				return bs.lookup(keys, size, left1, target, ret);
			}

			template<ArchType arch, class IntTy>
			bool bsearchImpl(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				switch (arch)
				{
				case ArchType::none:
					return bsearchStd(keys, size, target, ret);
				case ArchType::balanced:
#if defined(CPUINFO_ARCH_X86_64)
				case ArchType::sse2:
				case ArchType::sse4_1:
				case ArchType::avx2:
				case ArchType::avx512bw:
#elif defined(CPUINFO_ARCH_X86)
				case ArchType::sse2:
#elif defined(CPUINFO_ARCH_ARM64)
				case ArchType::neon:
#endif
					return bsearchBalanced<arch>(keys, size, target, ret);
				}
				return false;
			}

			INSTANTIATE_IMPL(ArchType::none);
			INSTANTIATE_IMPL(ArchType::balanced);

#if defined(CPUINFO_ARCH_X86) || defined(CPUINFO_ARCH_X86_64)
			INSTANTIATE_IMPL(ArchType::sse2);
#endif

#if defined(CPUINFO_ARCH_X86_64)
			INSTANTIATE_IMPL(ArchType::sse4_1);
			INSTANTIATE_IMPL(ArchType::avx2);
			INSTANTIATE_IMPL(ArchType::avx512bw);
#endif

#if defined(CPUINFO_ARCH_ARM64)
			INSTANTIATE_IMPL(ArchType::neon);
#endif
		}
	}
}

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
#if defined(CPUINFO_ARCH_X86_64)
			template<>
			struct BalancedSearcher<ArchType::avx512bw>
			{
				static constexpr size_t packetBytes = 64;

				template<class IntTy>
				bool lookup(const IntTy* keys, size_t size, size_t left, IntTy target, size_t& ret)
				{
					uint64_t mask;
					if (sizeof(IntTy) == 1)
					{
						__m512i ptarget = _mm512_set1_epi8((int8_t)target);
						mask = _mm512_cmpeq_epi8_mask(
							_mm512_loadu_si512((const __m512i*)&keys[left]),
							ptarget
						);
					}
					else if (sizeof(IntTy) == 2)
					{
						__m512i ptarget = _mm512_set1_epi16((int16_t)target);
						mask = _mm512_cmpeq_epi16_mask(
							_mm512_loadu_si512((const __m512i*)&keys[left]),
							ptarget
						);
					}
					else if (sizeof(IntTy) == 4)
					{
						__m512i ptarget = _mm512_set1_epi32((int32_t)target);
						mask = _mm512_cmpeq_epi32_mask(
							_mm512_loadu_si512((const __m512i*)&keys[left]),
							ptarget
						);
					}
					else
					{
						__m512i ptarget = _mm512_set1_epi64((int64_t)target);
						mask = _mm512_cmpeq_epi64_mask(
							_mm512_loadu_si512((const __m512i*)&keys[left]),
							ptarget
						);
					}

					size_t i = countTrailingZeroes(mask);
					if (mask && left + i < size)
					{
						ret = left + i;
						return true;
					}
					return false;
				}
			};

			template<>
			struct BalancedSearcher<ArchType::avx2>
			{
				static constexpr size_t packetBytes = 32;

				template<class IntTy>
				bool lookup(const IntTy* keys, size_t size, size_t left, IntTy target, size_t& ret)
				{
					uint32_t mask;
					if (sizeof(IntTy) == 1)
					{
						__m256i ptarget = _mm256_set1_epi8((int8_t)target);
						mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(
							_mm256_loadu_si256((const __m256i*)&keys[left]),
							ptarget
						));
					}
					else if (sizeof(IntTy) == 2)
					{
						__m256i ptarget = _mm256_set1_epi16((int16_t)target);
						mask = _mm256_movemask_epi8(_mm256_cmpeq_epi16(
							_mm256_loadu_si256((const __m256i*)&keys[left]),
							ptarget
						));
					}
					else if (sizeof(IntTy) == 4)
					{
						__m256i ptarget = _mm256_set1_epi32((int32_t)target);
						mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(
							_mm256_loadu_si256((const __m256i*)&keys[left]),
							ptarget
						));
					}
					else
					{
						__m256i ptarget = _mm256_set1_epi64x((int64_t)target);
						mask = _mm256_movemask_epi8(_mm256_cmpeq_epi64(
							_mm256_loadu_si256((const __m256i*)&keys[left]),
							ptarget
						));
					}

					size_t i = countTrailingZeroes(mask);
					if (mask && left + (i / sizeof(IntTy)) < size)
					{
						ret = left + (i / sizeof(IntTy));
						return true;
					}
					return false;
				}
			};

			template<>
			struct BalancedSearcher<ArchType::sse4_1>
			{
				static constexpr size_t packetBytes = 16;

				template<class IntTy>
				bool lookup(const IntTy* keys, size_t size, size_t left, IntTy target, size_t& ret)
				{
					uint32_t mask;
					if (sizeof(IntTy) == 1)
					{
						__m128i ptarget = _mm_set1_epi8((int8_t)target);
						mask = _mm_movemask_epi8(_mm_cmpeq_epi8(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						));
					}
					else if (sizeof(IntTy) == 2)
					{
						__m128i ptarget = _mm_set1_epi16((int16_t)target);
						mask = _mm_movemask_epi8(_mm_cmpeq_epi16(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						));
					}
					else if (sizeof(IntTy) == 4)
					{
						__m128i ptarget = _mm_set1_epi32((int32_t)target);
						mask = _mm_movemask_epi8(_mm_cmpeq_epi32(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						));
					}
					else
					{
						__m128i ptarget = _mm_set1_epi64x((int64_t)target);
						mask = _mm_movemask_epi8(_mm_cmpeq_epi64(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						));
					}

					size_t i = countTrailingZeroes(mask);
					if (mask && left + (i / sizeof(IntTy)) < size)
					{
						ret = left + (i / sizeof(IntTy));
						return true;
					}
					return false;
				}
			};
#endif
#if defined(CPUINFO_ARCH_X86)
			template<>
			struct BalancedSearcher<ArchType::sse2>
			{
				static constexpr size_t packetBytes = 16;

				template<class IntTy>
				bool lookup(const IntTy* keys, size_t size, size_t left, IntTy target, size_t& ret)
				{
					uint32_t mask;
					if (sizeof(IntTy) == 1)
					{
						__m128i ptarget = _mm_set1_epi8((int8_t)target);
						mask = _mm_movemask_epi8(_mm_cmpeq_epi8(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						));
					}
					else if (sizeof(IntTy) == 2)
					{
						__m128i ptarget = _mm_set1_epi16((int16_t)target);
						mask = _mm_movemask_epi8(_mm_cmpeq_epi16(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						));
					}
					else if (sizeof(IntTy) == 4)
					{
						__m128i ptarget = _mm_set1_epi32((int32_t)target);
						mask = _mm_movemask_epi8(_mm_cmpeq_epi32(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						));
					}
					else
					{
						// sse2 does not support long long comparison
						__m128i ptarget = _mm_set1_epi64x((int64_t)target);
						__m128i r = _mm_cmpeq_epi32(
							_mm_loadu_si128((const __m128i*)&keys[left]),
							ptarget
						);
						r = _mm_and_si128(r, _mm_shuffle_epi32(r, _MM_SHUFFLE(2, 3, 0, 1)));
						mask = _mm_movemask_epi8(r);
					}

					size_t i = countTrailingZeroes(mask);
					if (mask && left + (i / sizeof(IntTy)) < size)
					{
						ret = left + (i / sizeof(IntTy));
						return true;
					}
					return false;
				}
			};
#endif
#if defined(CPUINFO_ARCH_ARM64)


#endif
		}
	}
}
