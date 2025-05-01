#include "../MathFunc.hpp"
#include "../qgemm.hpp"
#include "../gemm.h"

#define USE_VNNI
#include "avx512_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::avx512vnni>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::avx512vnni>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::avx512vnni>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::avx512vnni>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::avx512vnni>(const uint8_t* a, const int8_t* b, size_t n);

		template<>
		inline void scatteredGEMV<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return detailVnni::scatteredGEMV_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return detailVnni::scatteredGEMV8x1_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detailVnni::scatteredGEMV2_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV3<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detailVnni::scatteredGEMV3_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV4<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detailVnni::scatteredGEMV4_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		struct ScatteredGEMMSmall<ArchType::avx512vnni>
		{
			template<size_t m, size_t n>
			static void op(size_t, size_t, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c, size_t ldc)
			{
				return detailVnni::scatteredGEMMSmall_512<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template void scatteredGEMMOpt<ArchType::avx512vnni>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<>
		void gemv<ArchType::avx512vnni>(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detailVnni::gemv_512(m, k, a, b, ldb, c);
		}

		template<>
		void gemvS8S8<ArchType::avx512vnni>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detailVnni::gemvS8S8_512(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::avx512vnni>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return detailVnni::gemvU8U8_512(m, k, a, b, ldb, c);
		}

		template<>
		float dotS8S8<ArchType::avx512vnni>(size_t k, const int8_t* a, const int8_t* b)
		{
			return detailVnni::dotS8S8_512(k, a, b);
		}

		template<>
		float dotU8U8<ArchType::avx512vnni>(size_t k, const uint8_t* a, const uint8_t* b)
		{
			return detailVnni::dotU8U8_512(k, a, b);
		}

		template<>
		void invNormS8<ArchType::avx512vnni>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return detailVnni::invNormS8_512(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return detailVnni::invNormU8_512(m, k, a, lda, out);
		}

		/*inline __m512i m512_sign_epi16(__m512i a, __m512i b)
		{
			__mmask32 b_nonzero = _mm512_test_epi16_mask(b, b);
			__mmask32 b_neg = _mm512_movepi16_mask(b);  // extract sign bits: b < 0

			__m512i a_zeroed = _mm512_maskz_mov_epi16(b_nonzero, a);  // (b!=0) ? a : 0
			return _mm512_mask_sub_epi16(a_zeroed, b_neg, _mm512_setzero_si512(), a_zeroed);  // b_neg ? 0-a_zeroed : a_zeroed
		}

		template<>
		float requantizePackedU4<ArchType::avx512vnni>(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		)
		{
			__m512i a, b, ls, lzp, lsa, lsb, lzpa, lzpb;
			const __m512i shfIdx = _mm512_set_epi8(
				15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9, 8, 8,
				7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0,
				15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9, 8, 8,
				7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0
			), compressIdx = _mm512_set_epi8(
				14, 12, 10, 8, 6, 4, 2, 0, 14, 12, 10, 8, 6, 4, 2, 0,
				14, 12, 10, 8, 6, 4, 2, 0, 14, 12, 10, 8, 6, 4, 2, 0,
				14, 12, 10, 8, 6, 4, 2, 0, 14, 12, 10, 8, 6, 4, 2, 0,
				14, 12, 10, 8, 6, 4, 2, 0, 14, 12, 10, 8, 6, 4, 2, 0
			);
			const __m512i upperMask = _mm512_set1_epi8(0xF0),
				lowerMask = _mm512_set1_epi8(0x0F);
			const __mmask64 blendMask = 0x5555555555555555ull;

			const __m512i localScaleMask = _mm512_set1_epi8(0x3F),
				localZeroPointMask = _mm512_set1_epi8(0xC0),
				localScaleBias = _mm512_set1_epi8(9),
				localZeroPointBias = _mm512_set1_epi8(6);

			const __m512i roundBias = _mm512_set1_epi16(4),
				divideBy9 = _mm512_set1_epi16(32768 / 9);

			const __m512i uintBias = _mm512_set1_epi8(128);

			__m512i localScaleBroadcastor;
			if (qgroup == 4)
			{
				localScaleBroadcastor = _mm512_set_epi8(
					15, 15, 15, 15, 14, 14, 14, 14,
					13, 13, 13, 13, 12, 12, 12, 12,
					11, 11, 11, 11, 10, 10, 10, 10,
					9, 9, 9, 9, 8, 8, 8, 8,
					7, 7, 7, 7, 6, 6, 6, 6,
					5, 5, 5, 5, 4, 4, 4, 4,
					3, 3, 3, 3, 2, 2, 2, 2,
					1, 1, 1, 1, 0, 0, 0, 0
				);
			}
			else if (qgroup == 8)
			{
				localScaleBroadcastor = _mm512_set_epi8(
					7, 7, 7, 7, 7, 7, 7, 7,
					6, 6, 6, 6, 6, 6, 6, 6,
					5, 5, 5, 5, 5, 5, 5, 5,
					4, 4, 4, 4, 4, 4, 4, 4,
					3, 3, 3, 3, 3, 3, 3, 3,
					2, 2, 2, 2, 2, 2, 2, 2,
					1, 1, 1, 1, 1, 1, 1, 1,
					0, 0, 0, 0, 0, 0, 0, 0
				);
			}
			else if (qgroup == 16)
			{
				localScaleBroadcastor = _mm512_set_epi8(
					3, 3, 3, 3, 3, 3, 3, 3,
					3, 3, 3, 3, 3, 3, 3, 3,
					2, 2, 2, 2, 2, 2, 2, 2,
					2, 2, 2, 2, 2, 2, 2, 2,
					1, 1, 1, 1, 1, 1, 1, 1,
					1, 1, 1, 1, 1, 1, 1, 1,
					0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0, 0, 0, 0, 0, 0
				);
			}
			else
			{
				throw std::runtime_error("Unsupported qgroup");
			}


			for (size_t i = 0; i < n / 2; i += 32)
			{
				// unpack int4 to int8
				a = _mm512_castsi256_si512(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(packedInput + i)));
				a = _mm512_shuffle_i32x4(a, a, 0x50);
				a = _mm512_shuffle_epi8(a, shfIdx);
				b = _mm512_srli_epi16(_mm512_and_si512(a, upperMask), 4);
				a = _mm512_and_si512(a, lowerMask);
				a = _mm512_mask_mov_epi8(b, blendMask, a);

				ls = _mm512_castsi128_si512(_mm_loadu_si128(reinterpret_cast<const __m128i*>(localScale + i * 2 / qgroup)));
				lzp = _mm512_add_epi8(_mm512_srli_epi16(_mm512_and_si512(ls, localZeroPointMask), 6), localZeroPointBias);
				ls = _mm512_add_epi8(_mm512_and_si512(ls, localScaleMask), localScaleBias);
				ls = _mm512_permutexvar_epi8(localScaleBroadcastor, ls); // vbmi
				lzp = _mm512_permutexvar_epi8(localScaleBroadcastor, lzp); // vbmi

				b = _mm512_unpacklo_epi8(a, _mm512_setzero_si512()); // 0, 1, 2, 3, 4, 5, 6, 7, 
				a = _mm512_unpackhi_epi8(a, _mm512_setzero_si512()); // 8, 9, a, b, c, d, e, f,

				lsb = _mm512_unpacklo_epi8(ls, _mm512_setzero_si512());
				lsa = _mm512_unpackhi_epi8(ls, _mm512_setzero_si512());

				lzpb = _mm512_unpacklo_epi8(lzp, _mm512_setzero_si512());
				lzpa = _mm512_unpackhi_epi8(lzp, _mm512_setzero_si512());

				b = _mm512_mullo_epi16(_mm512_sub_epi16(b, lzpb), lsb);
				a = _mm512_mullo_epi16(_mm512_sub_epi16(a, lzpa), lsa);

				b = _mm512_add_epi16(b, m512_sign_epi16(roundBias, b));
				a = _mm512_add_epi16(a, m512_sign_epi16(roundBias, a));

				b = _mm512_mulhrs_epi16(b, divideBy9);
				a = _mm512_mulhrs_epi16(a, divideBy9);

				b = _mm512_shuffle_epi8(b, compressIdx);
				a = _mm512_shuffle_epi8(a, compressIdx);

				a = _mm512_unpacklo_epi64(b, a);
				if (toUint8) a = _mm512_add_epi8(a, uintBias);
				_mm512_storeu_si512(reinterpret_cast<__m512i*>(out + i * 2), a);
			}

			return globalScale / 8;
		}*/

		template<>
		float requantizePackedU4<ArchType::avx512vnni>(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		)
		{
			return requantizePackedU4<ArchType::avx2>(n, qgroup, packedInput, localScale, globalScale, toUint8, out);
		}
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::avx512vnni>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC,
			bool zeroMode
		)
		{
			return gemm<ArchType::avx512bw>(m, n, k, aT, strideA, b, strideB, c, strideC, zeroMode);
		}

		template<>
		void gemv<ArchType::avx512vnni>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c,
			bool zeroMode
		)
		{
			return gemv<ArchType::avx512bw>(m, k, aT, strideA, b, c, zeroMode);
		}

		template<>
		void mul<ArchType::avx512vnni>(
			size_t n,
			float a,
			const float* b,
			float* c
		)
		{
			return mul<ArchType::avx512bw>(n, a, b, c);
		}

		template<>
		void invNorm<ArchType::avx512vnni>(
			size_t m, size_t k,
			const float* a, size_t lda,
			float* out
		)
		{
			return invNorm<ArchType::avx512bw>(m, k, a, lda, out);
		}
	}
}
