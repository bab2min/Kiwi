#include <algorithm>
#ifdef KIWI_USE_CPUINFO
	#include <cpuinfo.h>
#endif
#include <kiwi/BitUtils.h>
#include "search.h"

#define INSTANTIATE_IMPL(arch) \
	template bool bsearchImpl<arch>(const uint8_t*, size_t, uint8_t, size_t&);\
	template bool bsearchImpl<arch>(const uint16_t*, size_t, uint16_t, size_t&);\
	template bool bsearchImpl<arch>(const uint32_t*, size_t, uint32_t, size_t&);\
	template bool bsearchImpl<arch>(const uint64_t*, size_t, uint64_t, size_t&);\
	template bool bsearchImpl<arch>(const char16_t*, size_t, char16_t, size_t&)

#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64
#include <immintrin.h>
#elif CPUINFO_ARCH_ARM || CPUINFO_ARCH_ARM64
#include <arm_neon.h>
#endif

#ifdef __GNUC__
#define ARCH_TARGET(x) __attribute__((target(x)))
#else
#define ARCH_TARGET(x)
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
#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64
	_mm_prefetch((const char*)ptr, _MM_HINT_T0);
#elif defined(__GNUC__)
	__builtin_prefetch(ptr);
#endif
}

static constexpr int getBitSize(size_t n)
{
	return n <= 1 ? 0 : (getBitSize(n >> 1) + 1);
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
				static constexpr int minH = getBitSize(bs.packetBytes / sizeof(IntTy));
				static constexpr int realMinH = minH > 2 ? minH : 0;

				int height = ceilLog2(size + 1);
				size_t dist = (size_t)1 << (size_t)(height - 1);
				size_t mid = size - dist;
				dist >>= 1;
				size_t left1 = 0, left2 = mid + 1;
				while (height-- > realMinH)
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
#ifdef KIWI_USE_CPUINFO
#if CPUINFO_ARCH_X86_64
				case ArchType::sse2:
				case ArchType::sse4_1:
				case ArchType::avx2:
				case ArchType::avx512bw:
#elif CPUINFO_ARCH_X86
				case ArchType::sse2:
#elif CPUINFO_ARCH_ARM || CPUINFO_ARCH_ARM64
				case ArchType::neon:
#endif
#endif
					return bsearchBalanced<arch>(keys, size, target, ret);
				}
				return false;
			}

			INSTANTIATE_IMPL(ArchType::none);
			INSTANTIATE_IMPL(ArchType::balanced);
			INSTANTIATE_IMPL(ArchType::sse2);
			INSTANTIATE_IMPL(ArchType::sse4_1);
			INSTANTIATE_IMPL(ArchType::avx2);
			INSTANTIATE_IMPL(ArchType::avx512bw);
			INSTANTIATE_IMPL(ArchType::neon);
		}
	}
}

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
#ifdef KIWI_USE_CPUINFO
#if CPUINFO_ARCH_X86_64
			template<>
			struct BalancedSearcher<ArchType::avx512bw>
			{
				static constexpr size_t packetBytes = 64;

				template<class IntTy>
				ARCH_TARGET("avx512bw")
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
				ARCH_TARGET("avx2")
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
				ARCH_TARGET("sse4.1")
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
#if CPUINFO_ARCH_X86
			template<>
			struct BalancedSearcher<ArchType::sse2>
			{
				static constexpr size_t packetBytes = 16;

				template<class IntTy>
				ARCH_TARGET("sse2")
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
#if CPUINFO_ARCH_ARM64
			template<>
			struct BalancedSearcher<ArchType::neon>
			{
				static constexpr size_t packetBytes = 16;

				template<class IntTy>
				ARCH_TARGET("arch=armv8-a")
				bool lookup(const IntTy* keys, size_t size, size_t left, IntTy target, size_t& ret)
				{
					size_t found;
					if (sizeof(IntTy) == 1)
					{
						static const uint8_t __attribute__((aligned(16))) idx[16][16] = { 
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
							{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
						};
						int8x16_t ptarget = vdupq_n_s8((int8_t)target);
						uint8x16_t selected = vandq_u8(vceqq_s8(
							vld1q_s8((const int8_t*)&keys[left]),
							ptarget
						), vld1q_u8(idx[16 - std::min(size - left, (size_t)16)]));
						found = vaddvq_u8(selected);
					}
					else if (sizeof(IntTy) == 2)
					{
						static const uint16_t __attribute__((aligned(16))) idx[8][8] = {
							{ 1, 2, 3, 4, 5, 6, 7, 8 },
							{ 1, 2, 3, 4, 5, 6, 7, 0 },
							{ 1, 2, 3, 4, 5, 6, 0, 0 },
							{ 1, 2, 3, 4, 5, 0, 0, 0 },
							{ 1, 2, 3, 4, 0, 0, 0, 0 },
							{ 1, 2, 3, 0, 0, 0, 0, 0 },
							{ 1, 2, 0, 0, 0, 0, 0, 0 },
							{ 1, 0, 0, 0, 0, 0, 0, 0 },
						};
						int16x8_t ptarget = vdupq_n_s16((int16_t)target);
						uint16x8_t selected = vandq_u16(vceqq_s16(
							vld1q_s16((const int16_t*)&keys[left]),
							ptarget
						), vld1q_u16(idx[8 - std::min(size - left, (size_t)8)]));
						found = vaddvq_u16(selected);
					}
					else if (sizeof(IntTy) == 4)
					{
						static const uint32_t __attribute__((aligned(16))) idx[4][4] = {
							{ 1, 2, 3, 4 },
							{ 1, 2, 3, 0 },
							{ 1, 2, 0, 0 },
							{ 1, 0, 0, 0 },
						};
						int32x4_t ptarget = vdupq_n_s32((int32_t)target);
						uint32x4_t selected = vandq_u32(vceqq_s32(
							vld1q_s32((const int32_t*)&keys[left]),
							ptarget
						), vld1q_u32(idx[4 - std::min(size - left, (size_t)4)]));
						found = vaddvq_u32(selected);
					}
					else
					{
						static const uint64_t __attribute__((aligned(16))) idx[2][2] = {
							{ 1, 2 },
							{ 1, 0 },
						};
						int64x2_t ptarget = vdupq_n_s64((int64_t)target);
						uint64x2_t selected = vandq_u64(vceqq_s64(
							vld1q_s64((const int64_t*)&keys[left]),
							ptarget
						), vld1q_u64(idx[2 - std::min(size - left, (size_t)2)]));
						found = vaddvq_u64(selected);
					}

					if (found && left + found - 1 < size)
					{
						ret = left + found - 1;
						return true;
					}
					return false;
				}
			};

#endif
#endif
		}
	}
}
