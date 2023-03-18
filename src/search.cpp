
#include <algorithm>
#include <numeric>

#include <kiwi/Types.h>
#include <kiwi/BitUtils.h>

#include "ArchAvailable.h"
#include "search.h"

#define INSTANTIATE_IMPL(arch) \
	template bool detail::searchImpl<arch>(const uint8_t*, size_t, uint8_t, size_t&);\
	template bool detail::searchImpl<arch>(const uint16_t*, size_t, uint16_t, size_t&);\
	template bool detail::searchImpl<arch>(const uint32_t*, size_t, uint32_t, size_t&);\
	template bool detail::searchImpl<arch>(const uint64_t*, size_t, uint64_t, size_t&);\
	template bool detail::searchImpl<arch>(const char16_t*, size_t, char16_t, size_t&);\
	template Vector<size_t> detail::reorderImpl<arch>(const uint8_t*, size_t);\
	template Vector<size_t> detail::reorderImpl<arch>(const uint16_t*, size_t);\
	template Vector<size_t> detail::reorderImpl<arch>(const uint32_t*, size_t);\
	template Vector<size_t> detail::reorderImpl<arch>(const uint64_t*, size_t);\
	template Vector<size_t> detail::reorderImpl<arch>(const char16_t*, size_t);

#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64 || KIWI_ARCH_X86 || KIWI_ARCH_X86_64
#include <immintrin.h>
#elif CPUINFO_ARCH_ARM || CPUINFO_ARCH_ARM64 || KIWI_ARCH_ARM64
#include <arm_neon.h>
#endif

#ifdef __GNUC__
#define ARCH_TARGET(x) __attribute__((target(x)))
#else
#define ARCH_TARGET(x)
#endif

namespace kiwi
{
	namespace nst
	{
		template<class KeyTy>
		Vector<size_t> getBstOrder(const KeyTy* keys, size_t size)
		{
			Vector<size_t> ret(size), idx(size);
			std::iota(idx.begin(), idx.end(), 0);

			size_t height = 0;
			for (size_t s = size; s > 0; s >>= 1) height++;

			size_t complete_size = (1 << height) - 1;
			size_t off = complete_size - size;
			size_t off_start = complete_size + 1 - off * 2;

			size_t i = 0;
			for (size_t h = 0; h < height; ++h)
			{
				size_t start = (1 << (height - h - 1)) - 1;
				size_t stride = 1 << (height - h);
				for (size_t r = start; r < complete_size; r += stride)
				{
					size_t f = r;
					if (f > off_start) f -= (f - off_start + 1) / 2;
					ret[i++] = idx[f];
					if (i >= size) break;
				}
			}

			return ret;
		}

		template<class Ty>
		Ty powi(Ty a, size_t b)
		{
			if (b == 0) return 1;
			if (b == 1) return a;
			if (b == 2) return a * a;
			if (b == 3) return a * a * a;

			return powi(a, b / 2) * powi(a, b - (b / 2));
		}

		template<size_t n, class KeyTy>
		Vector<size_t> getNstOrder(const KeyTy* keys, size_t size, bool sort = false)
		{
			Vector<size_t> ret(size);
			size_t neg_pos = sort ? (std::find_if(keys, keys + size, [](KeyTy a) { return a < 0; }) - keys) : size;

			size_t height = 0;
			for (size_t s = size; s > 0; s /= n) height++;

			size_t complete_size = powi(n, height) - 1;
			size_t off = complete_size - size;
			size_t off_start = complete_size + 1 - ((off + n - 2) / (n - 1) + off);

			size_t i = 0;
			for (size_t h = 0; h < height; ++h)
			{
				size_t stride = powi(n, height - h - 1);
				size_t start = stride - 1;

				for (size_t r = start; r < complete_size; r += stride)
				{
					for (size_t k = 0; k < n - 1; ++k, r += stride)
					{
						size_t f = r;
						if (f > off_start) f -= (f - off_start) - (f - off_start) / n;
						
						if (f < size - neg_pos) f += neg_pos;
						else f -= size - neg_pos;

						ret[i++] = f;
						if (i >= size) break;
					}
					if (i >= size) break;
				}
			}
			return ret;
		}

		template<class KeyTy>
		bool bstSearch(const KeyTy* keys, size_t size, KeyTy target, size_t& ret)
		{
			size_t i = 0;
			while (i < size)
			{
				if (target == keys[i])
				{
					ret = i;
					return true;
				}
				else if (target < keys[i])
				{
					i = i * 2 + 1;
				}
				else
				{
					i = i * 2 + 2;
				}
			}
			return false;
		}

		template<ArchType arch> struct OptimizedImpl;

		template<ArchType arch, class IntTy>
		Vector<size_t> detail::reorderImpl(const IntTy* keys, size_t size)
		{
			return OptimizedImpl<arch>::reorder(keys, size);
		}

		template<ArchType arch, class IntTy>
		bool detail::searchImpl(const IntTy* keys, size_t size, IntTy target, size_t& ret)
		{
			return OptimizedImpl<arch>::template search<IntTy>(keys, size, target, ret);
		}

		template<>
		struct OptimizedImpl<ArchType::none>
		{
			template<class IntTy>
			static Vector<size_t> reorder(const IntTy* keys, size_t size)
			{
				return getBstOrder(keys, size);
			}

			template<class IntTy>
			static bool search(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				return bstSearch(keys, size, target, ret);
			}
		};
		INSTANTIATE_IMPL(ArchType::none);

		template<>
		struct OptimizedImpl<ArchType::balanced>
		{
			template<class IntTy>
			static Vector<size_t> reorder(const IntTy* keys, size_t size)
			{
				return {};
			}

			template<class IntTy>
			static bool search(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				int height = utils::ceilLog2(size + 1);
				size_t dist = (size_t)1 << (size_t)(height - 1);
				size_t mid = size - dist;
				dist >>= 1;
				size_t left1 = 0, left2 = mid + 1;
				while (height-- > 0)
				{
					if (target > keys[mid]) left1 = left2;
					left2 = left1 + dist;
					mid = left1 + dist - 1;
					dist >>= 1;
				}
				if (left1 == size || keys[left1] != target) return false;
				ret = left1;
				return true;
			}
		};
		INSTANTIATE_IMPL(ArchType::balanced);

		template<class IntTy>
		struct SignedType { using type = IntTy; };

		template<>
		struct SignedType<uint8_t> { using type = int8_t; };

		template<>
		struct SignedType<uint16_t> { using type = int16_t; };

		template<>
		struct SignedType<uint32_t> { using type = int32_t; };

		template<>
		struct SignedType<uint64_t> { using type = int64_t; };

		template<>
		struct SignedType<char16_t> { using type = int16_t; };
	}
}

#if defined(__x86_64__) || CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64 || KIWI_ARCH_X86 || KIWI_ARCH_X86_64
namespace kiwi
{
	namespace nst
	{
		template<class IntTy>
		ARCH_TARGET("sse2")
		inline bool testEq(__m128i p, size_t offset, size_t size, size_t& ret)
		{
			uint32_t m = _mm_movemask_epi8(p);
			uint32_t b = utils::countTrailingZeroes(m);
			if (m && (offset + b / sizeof(IntTy)) < size)
			{
				ret = offset + b / sizeof(IntTy);
				return true;
			}
			return false;
		}

		template<size_t n, class IntTy>
		ARCH_TARGET("sse2")
		bool nstSearchSSE2(const IntTy* keys, size_t size, IntTy target, size_t& ret)
		{
			size_t i = 0, r;

			__m128i ptarget, pkey, peq, pgt;
			switch (sizeof(IntTy))
			{
			case 1:
				ptarget = _mm_set1_epi8(target);
				break;
			case 2:
				ptarget = _mm_set1_epi16(target);
				break;
			case 4:
				ptarget = _mm_set1_epi32(target);
				break;
			}

			if (size >= n * n * n - 1)
			{
				pkey = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm_cmpeq_epi8(ptarget, pkey);
					pgt = _mm_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm_cmpeq_epi16(ptarget, pkey);
					pgt = _mm_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm_cmpeq_epi32(ptarget, pkey);
					pgt = _mm_cmpgt_epi32(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm_cmpeq_epi8(ptarget, pkey);
					pgt = _mm_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm_cmpeq_epi16(ptarget, pkey);
					pgt = _mm_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm_cmpeq_epi32(ptarget, pkey);
					pgt = _mm_cmpgt_epi32(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm_cmpeq_epi8(ptarget, pkey);
					pgt = _mm_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm_cmpeq_epi16(ptarget, pkey);
					pgt = _mm_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm_cmpeq_epi32(ptarget, pkey);
					pgt = _mm_cmpgt_epi32(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);
			}
			else if (size >= n * n - 1)
			{
				pkey = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm_cmpeq_epi8(ptarget, pkey);
					pgt = _mm_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm_cmpeq_epi16(ptarget, pkey);
					pgt = _mm_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm_cmpeq_epi32(ptarget, pkey);
					pgt = _mm_cmpgt_epi32(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm_cmpeq_epi8(ptarget, pkey);
					pgt = _mm_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm_cmpeq_epi16(ptarget, pkey);
					pgt = _mm_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm_cmpeq_epi32(ptarget, pkey);
					pgt = _mm_cmpgt_epi32(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);
			}

			while (i < size)
			{
				pkey = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm_cmpeq_epi8(ptarget, pkey);
					pgt = _mm_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm_cmpeq_epi16(ptarget, pkey);
					pgt = _mm_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm_cmpeq_epi32(ptarget, pkey);
					pgt = _mm_cmpgt_epi32(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);
			}
			return false;
		}

		template<>
		struct OptimizedImpl<ArchType::sse2>
		{
			template<class IntTy>
			static Vector<size_t> reorder(const IntTy* keys, size_t size)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return getNstOrder<16 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, true);
			}

			template<class IntTy>
			static bool search(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return nstSearchSSE2<16 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, (SignedIntTy)target, ret);
			}
		};
		INSTANTIATE_IMPL(ArchType::sse2);
	}
}
#endif

#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64 || KIWI_ARCH_X86 || KIWI_ARCH_X86_64
namespace kiwi
{
	namespace nst
	{
		template<class IntTy>
		ARCH_TARGET("avx2")
		inline bool testEq(__m256i p, size_t offset, size_t size, size_t& ret)
		{
			uint32_t m = _mm256_movemask_epi8(p);
			uint32_t b = utils::countTrailingZeroes(m);
			if (m && (offset + b / sizeof(IntTy)) < size)
			{
				ret = offset + b / sizeof(IntTy);
				return true;
			}
			return false;
		}

		inline bool testEqMask(uint64_t m, size_t offset, size_t size, size_t& ret)
		{
			uint32_t b = utils::countTrailingZeroes(m);
			if (m && (offset + b) < size)
			{
				ret = offset + b;
				return true;
			}
			return false;
		}

		template<size_t n, class IntTy>
		ARCH_TARGET("avx2")
		bool nstSearchAVX2(const IntTy* keys, size_t size, IntTy target, size_t& ret)
		{
			size_t i = 0, r;

			__m256i ptarget, pkey, peq, pgt;
			switch (sizeof(IntTy))
			{
			case 1:
				ptarget = _mm256_set1_epi8(target);
				break;
			case 2:
				ptarget = _mm256_set1_epi16(target);
				break;
			case 4:
				ptarget = _mm256_set1_epi32(target);
				break;
			case 8:
				ptarget = _mm256_set1_epi64x(target);
				break;
			}

			if (size >= n * n * n - 1)
			{
				pkey = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm256_cmpeq_epi8(ptarget, pkey);
					pgt = _mm256_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm256_cmpeq_epi16(ptarget, pkey);
					pgt = _mm256_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm256_cmpeq_epi32(ptarget, pkey);
					pgt = _mm256_cmpgt_epi32(ptarget, pkey);
					break;
				case 8:
					peq = _mm256_cmpeq_epi64(ptarget, pkey);
					pgt = _mm256_cmpgt_epi64(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm256_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm256_cmpeq_epi8(ptarget, pkey);
					pgt = _mm256_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm256_cmpeq_epi16(ptarget, pkey);
					pgt = _mm256_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm256_cmpeq_epi32(ptarget, pkey);
					pgt = _mm256_cmpgt_epi32(ptarget, pkey);
					break;
				case 8:
					peq = _mm256_cmpeq_epi64(ptarget, pkey);
					pgt = _mm256_cmpgt_epi64(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm256_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm256_cmpeq_epi8(ptarget, pkey);
					pgt = _mm256_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm256_cmpeq_epi16(ptarget, pkey);
					pgt = _mm256_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm256_cmpeq_epi32(ptarget, pkey);
					pgt = _mm256_cmpgt_epi32(ptarget, pkey);
					break;
				case 8:
					peq = _mm256_cmpeq_epi64(ptarget, pkey);
					pgt = _mm256_cmpgt_epi64(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm256_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);
			}
			else if (size >= n * n - 1)
			{
				pkey = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm256_cmpeq_epi8(ptarget, pkey);
					pgt = _mm256_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm256_cmpeq_epi16(ptarget, pkey);
					pgt = _mm256_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm256_cmpeq_epi32(ptarget, pkey);
					pgt = _mm256_cmpgt_epi32(ptarget, pkey);
					break;
				case 8:
					peq = _mm256_cmpeq_epi64(ptarget, pkey);
					pgt = _mm256_cmpgt_epi64(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm256_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm256_cmpeq_epi8(ptarget, pkey);
					pgt = _mm256_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm256_cmpeq_epi16(ptarget, pkey);
					pgt = _mm256_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm256_cmpeq_epi32(ptarget, pkey);
					pgt = _mm256_cmpgt_epi32(ptarget, pkey);
					break;
				case 8:
					peq = _mm256_cmpeq_epi64(ptarget, pkey);
					pgt = _mm256_cmpgt_epi64(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm256_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);
			}

			while (i < size)
			{
				pkey = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm256_cmpeq_epi8(ptarget, pkey);
					pgt = _mm256_cmpgt_epi8(ptarget, pkey);
					break;
				case 2:
					peq = _mm256_cmpeq_epi16(ptarget, pkey);
					pgt = _mm256_cmpgt_epi16(ptarget, pkey);
					break;
				case 4:
					peq = _mm256_cmpeq_epi32(ptarget, pkey);
					pgt = _mm256_cmpgt_epi32(ptarget, pkey);
					break;
				case 8:
					peq = _mm256_cmpeq_epi64(ptarget, pkey);
					pgt = _mm256_cmpgt_epi64(ptarget, pkey);
					break;
				}

				if (testEq<IntTy>(peq, i, size, ret)) return true;

				r = utils::popcount((uint32_t)_mm256_movemask_epi8(pgt)) / sizeof(IntTy);
				i = i * n + (n - 1) * (r + 1);
			}
			return false;
		}

		template<size_t n, class IntTy>
		ARCH_TARGET("avx512bw")
		bool nstSearchAVX512(const IntTy* keys, size_t size, IntTy target, size_t& ret)
		{
			size_t i = 0, r;

			__m512i ptarget, pkey;
			uint64_t peq, pgt;
			switch (sizeof(IntTy))
			{
			case 1:
				ptarget = _mm512_set1_epi8(target);
				break;
			case 2:
				ptarget = _mm512_set1_epi16(target);
				break;
			case 4:
				ptarget = _mm512_set1_epi32(target);
				break;
			case 8:
				ptarget = _mm512_set1_epi64(target);
				break;
			}

			if (size >= n * n * n - 1)
			{
				pkey = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm512_cmpeq_epi8_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi8_mask(ptarget, pkey);
					break;
				case 2:
					peq = _mm512_cmpeq_epi16_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi16_mask(ptarget, pkey);
					break;
				case 4:
					peq = _mm512_cmpeq_epi32_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi32_mask(ptarget, pkey);
					break;
				case 8:
					peq = _mm512_cmpeq_epi64_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi64_mask(ptarget, pkey);
					break;
				}

				if (testEqMask(peq, i, size, ret)) return true;

				r = utils::popcount(pgt);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm512_cmpeq_epi8_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi8_mask(ptarget, pkey);
					break;
				case 2:
					peq = _mm512_cmpeq_epi16_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi16_mask(ptarget, pkey);
					break;
				case 4:
					peq = _mm512_cmpeq_epi32_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi32_mask(ptarget, pkey);
					break;
				case 8:
					peq = _mm512_cmpeq_epi64_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi64_mask(ptarget, pkey);
					break;
				}

				if (testEqMask(peq, i, size, ret)) return true;

				r = utils::popcount(pgt);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm512_cmpeq_epi8_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi8_mask(ptarget, pkey);
					break;
				case 2:
					peq = _mm512_cmpeq_epi16_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi16_mask(ptarget, pkey);
					break;
				case 4:
					peq = _mm512_cmpeq_epi32_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi32_mask(ptarget, pkey);
					break;
				case 8:
					peq = _mm512_cmpeq_epi64_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi64_mask(ptarget, pkey);
					break;
				}

				if (testEqMask(peq, i, size, ret)) return true;

				r = utils::popcount(pgt);
				i = i * n + (n - 1) * (r + 1);
			}
			else if (size >= n * n - 1)
			{
				pkey = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm512_cmpeq_epi8_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi8_mask(ptarget, pkey);
					break;
				case 2:
					peq = _mm512_cmpeq_epi16_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi16_mask(ptarget, pkey);
					break;
				case 4:
					peq = _mm512_cmpeq_epi32_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi32_mask(ptarget, pkey);
					break;
				case 8:
					peq = _mm512_cmpeq_epi64_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi64_mask(ptarget, pkey);
					break;
				}

				if (testEqMask(peq, i, size, ret)) return true;

				r = utils::popcount(pgt);
				i = i * n + (n - 1) * (r + 1);

				pkey = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm512_cmpeq_epi8_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi8_mask(ptarget, pkey);
					break;
				case 2:
					peq = _mm512_cmpeq_epi16_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi16_mask(ptarget, pkey);
					break;
				case 4:
					peq = _mm512_cmpeq_epi32_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi32_mask(ptarget, pkey);
					break;
				case 8:
					peq = _mm512_cmpeq_epi64_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi64_mask(ptarget, pkey);
					break;
				}

				if (testEqMask(peq, i, size, ret)) return true;

				r = utils::popcount(pgt);
				i = i * n + (n - 1) * (r + 1);
			}

			while (i < size)
			{
				pkey = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&keys[i]));
				switch (sizeof(IntTy))
				{
				case 1:
					peq = _mm512_cmpeq_epi8_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi8_mask(ptarget, pkey);
					break;
				case 2:
					peq = _mm512_cmpeq_epi16_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi16_mask(ptarget, pkey);
					break;
				case 4:
					peq = _mm512_cmpeq_epi32_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi32_mask(ptarget, pkey);
					break;
				case 8:
					peq = _mm512_cmpeq_epi64_mask(ptarget, pkey);
					pgt = _mm512_cmpgt_epi64_mask(ptarget, pkey);
					break;
				}

				if (testEqMask(peq, i, size, ret)) return true;

				r = utils::popcount(pgt);
				i = i * n + (n - 1) * (r + 1);
			}
			return false;
		}

		template<>
		struct OptimizedImpl<ArchType::sse4_1>
		{
			template<class IntTy>
			static Vector<size_t> reorder(const IntTy* keys, size_t size)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return getNstOrder<16 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, true);
			}

			template<class IntTy>
			static bool search(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return nstSearchSSE2<16 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, (SignedIntTy)target, ret);
			}
		};
		INSTANTIATE_IMPL(ArchType::sse4_1);

		template<>
		struct OptimizedImpl<ArchType::avx2>
		{
			template<class IntTy>
			static Vector<size_t> reorder(const IntTy* keys, size_t size)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return getNstOrder<32 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, true);
			}

			template<class IntTy>
			static bool search(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return nstSearchAVX2<32 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, (SignedIntTy)target, ret);
			}
		};
		INSTANTIATE_IMPL(ArchType::avx2);

		template<>
		struct OptimizedImpl<ArchType::avx512bw>
		{
			template<class IntTy>
			static Vector<size_t> reorder(const IntTy* keys, size_t size)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return getNstOrder<64 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, true);
			}

			template<class IntTy>
			static bool search(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return nstSearchAVX512<64 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, (SignedIntTy)target, ret);
			}
		};
		INSTANTIATE_IMPL(ArchType::avx512bw);
	}
}
#endif

#if CPUINFO_ARCH_ARM || CPUINFO_ARCH_ARM64 || KIWI_ARCH_ARM64
namespace kiwi
{
	namespace nst
	{
		template<size_t n>
		ARCH_TARGET("arch=armv8-a")
		bool nstSearchNeon(const int8_t* keys, size_t size, int8_t target, size_t& ret)
		{
			size_t i = 0;

			int8x16_t ptarget = vdupq_n_s8(target), pkey;
			uint8x16_t peq, pgt, pmasked;
			static const uint8x16_t __attribute__((aligned(16))) mask = { 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128 };

			while (i < size)
			{
				pkey = vld1q_s8(&keys[i]);
				peq = vceqq_s8(ptarget, pkey);
				pgt = vcgtq_s8(ptarget, pkey);

				pmasked = vandq_u8(peq, mask);
				uint8_t mm0 = vaddv_u8(vget_low_u8(pmasked));
				uint8_t mm1 = vaddv_u8(vget_high_u8(pmasked));
				uint32_t mm = mm0 | (uint32_t)(mm1 << 8);
				uint32_t b = utils::countTrailingZeroes(mm);
				if (mm && (i + b) < size)
				{
					ret = i + b;
					return true;
				}

				size_t r = vaddvq_u8(vandq_u8(pgt, vdupq_n_u8(1)));
				i = i * n + (n - 1) * (r + 1);
			}
			return false;
		}

		template<size_t n>
		ARCH_TARGET("arch=armv8-a")
		bool nstSearchNeon(const int16_t* keys, size_t size, int16_t target, size_t& ret)
		{
			size_t i = 0;

			int16x8_t ptarget = vdupq_n_s16(target), pkey;
			uint16x8_t peq, pgt;
			static const uint16x8_t __attribute__((aligned(16))) mask = { 1, 2, 4, 8, 16, 32, 64, 128 };

			while (i < size)
			{
				pkey = vld1q_s16(&keys[i]);
				peq = vceqq_s16(ptarget, pkey);
				pgt = vcgtq_s16(ptarget, pkey);

				uint32_t mm = vaddvq_u16(vandq_u16(peq, mask));
				uint32_t b = utils::countTrailingZeroes(mm);
				if (mm && (i + b) < size)
				{
					ret = i + b;
					return true;
				}

				size_t r = vaddvq_u16(vandq_u16(pgt, vdupq_n_u16(1)));
				i = i * n + (n - 1) * (r + 1);
			}
			return false;
		}

		template<size_t n>
		ARCH_TARGET("arch=armv8-a")
		bool nstSearchNeon(const int32_t* keys, size_t size, int32_t target, size_t& ret)
		{
			size_t i = 0;

			int32x4_t ptarget = vdupq_n_s32(target), pkey;
			uint32x4_t peq, pgt;
			static const uint32x4_t __attribute__((aligned(16))) mask = { 1, 2, 4, 8 };

			while (i < size)
			{
				pkey = vld1q_s32(&keys[i]);
				peq = vceqq_s32(ptarget, pkey);
				pgt = vcgtq_s32(ptarget, pkey);

				uint32_t mm = vaddvq_u32(vandq_u32(peq, mask));
				uint32_t b = utils::countTrailingZeroes(mm);
				if (mm && (i + b) < size)
				{
					ret = i + b;
					return true;
				}

				size_t r = vaddvq_u32(vandq_u32(pgt, vdupq_n_u32(1)));
				i = i * n + (n - 1) * (r + 1);
			}
			return false;
		}

		template<size_t n>
		ARCH_TARGET("arch=armv8-a")
		bool nstSearchNeon(const int64_t* keys, size_t size, int64_t target, size_t& ret)
		{
			size_t i = 0;

			int64x2_t ptarget = vdupq_n_s64(target), pkey;
			uint64x2_t peq, pgt;
			static const uint64x2_t __attribute__((aligned(16))) mask = { 1, 2 };

			while (i < size)
			{
				pkey = vld1q_s64(&keys[i]);
				peq = vceqq_s64(ptarget, pkey);
				pgt = vcgtq_s64(ptarget, pkey);

				uint32_t mm = vaddvq_u64(vandq_u64(peq, mask));
				uint32_t b = utils::countTrailingZeroes(mm);
				if (mm && (i + b) < size)
				{
					ret = i + b;
					return true;
				}

				size_t r = vaddvq_u64(vandq_u64(pgt, vdupq_n_u64(1)));
				i = i * n + (n - 1) * (r + 1);
			}
			return false;
		}

		template<>
		struct OptimizedImpl<ArchType::neon>
		{
			template<class IntTy>
			static Vector<size_t> reorder(const IntTy* keys, size_t size)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return getNstOrder<16 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, true);
			}

			template<class IntTy>
			static bool search(const IntTy* keys, size_t size, IntTy target, size_t& ret)
			{
				using SignedIntTy = typename SignedType<IntTy>::type;
				return nstSearchNeon<16 / sizeof(IntTy) + 1>((const SignedIntTy*)keys, size, (SignedIntTy)target, ret);
			}
		};
		INSTANTIATE_IMPL(ArchType::neon);
	}
}
#endif
