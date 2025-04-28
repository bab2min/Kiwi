#include "../MathFunc.hpp"
#include "../qgemm.hpp"

#include "avx2_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::avx2>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::avx2>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::avx2>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::avx2>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::avx2>(const uint8_t* a, const int8_t* b, size_t n);

		template<>
		inline void scatteredGEMV<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return detail::scatteredGEMV_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return detail::scatteredGEMV8x1_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detail::scatteredGEMV2_256(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		struct ScatteredGEMMSmall<ArchType::avx2>
		{
			template<size_t m, size_t n>
			static void op(size_t, size_t, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c, size_t ldc)
			{
				return detail::scatteredGEMMSmall_256<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template void scatteredGEMMOpt<ArchType::avx2>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<>
		void gemv<ArchType::avx2>(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detail::gemv_256(m, k, a, b, ldb, c);
		}

		template<>
		void gemvS8S8<ArchType::avx2>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detail::gemvS8S8_256(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::avx2>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return detail::gemvU8U8_256(m, k, a, b, ldb, c);
		}

		template<>
		float dotS8S8<ArchType::avx2>(size_t k, const int8_t* a, const int8_t* b)
		{
			return detail::dotS8S8_256(k, a, b);
		}

		template<>
		float dotU8U8<ArchType::avx2>(size_t k, const uint8_t* a, const uint8_t* b)
		{
			return detail::dotU8U8_256(k, a, b);
		}

		template<>
		void invNormS8<ArchType::avx2>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return detail::invNormS8_256(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return detail::invNormU8_256(m, k, a, lda, out);
		}

		template<>
		float requantizePackedU4<ArchType::avx2>(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		)
		{
			__m256i a, b, ls, lzp, lsa, lsb, lzpa, lzpb;
			const __m256i shfIdx = _mm256_set_epi8(
				15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9, 8, 8,
				7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0
			), compressIdx = _mm256_set_epi8(
				14, 12, 10, 8, 6, 4, 2, 0, 14, 12, 10, 8, 6, 4, 2, 0,
				14, 12, 10, 8, 6, 4, 2, 0, 14, 12, 10, 8, 6, 4, 2, 0
			);
			const __m256i upperMask = _mm256_set1_epi8(0xF0),
				lowerMask = _mm256_set1_epi8(0x0F),
				blendMask = _mm256_set1_epi16(0xFF00);

			const __m256i localScaleMask = _mm256_set1_epi8(0x3F),
				localZeroPointMask = _mm256_set1_epi8(0xC0),
				localScaleBias = _mm256_set1_epi8(9),
				localZeroPointBias = _mm256_set1_epi8(6);

			const __m256i roundBias = _mm256_set1_epi16(4),
				divideBy9 = _mm256_set1_epi16(32768 / 9);

			const __m256i uintBias = _mm256_set1_epi8(128);

			__m256i localScaleBroadcastor;
			if (qgroup == 4)
			{
				localScaleBroadcastor = _mm256_set_epi8(
					7, 7, 7, 7, 6, 6, 6, 6,
					5, 5, 5, 5, 4, 4, 4, 4,
					3, 3, 3, 3, 2, 2, 2, 2,
					1, 1, 1, 1, 0, 0, 0, 0
				);
			}
			else if (qgroup == 8)
			{
				localScaleBroadcastor = _mm256_set_epi8(
					3, 3, 3, 3, 3, 3, 3, 3,
					2, 2, 2, 2, 2, 2, 2, 2,
					1, 1, 1, 1, 1, 1, 1, 1,
					0, 0, 0, 0, 0, 0, 0, 0
				);
			}
			else if (qgroup == 16)
			{
				localScaleBroadcastor = _mm256_set_epi8(
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


			for (size_t i = 0; i < n / 2; i += 16)
			{
				// unpack int4 to int8
				a = _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(packedInput + i)));
				a = _mm256_shuffle_epi8(a, shfIdx);
				b = _mm256_srli_epi16(_mm256_and_si256(a, upperMask), 4);
				a = _mm256_and_si256(a, lowerMask);
				a = _mm256_blendv_epi8(a, b, blendMask);

				ls = _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(localScale + i * 2 / qgroup)));
				lzp = _mm256_add_epi8(_mm256_srli_epi16(_mm256_and_si256(ls, localZeroPointMask), 6), localZeroPointBias);
				ls = _mm256_add_epi8(_mm256_and_si256(ls, localScaleMask), localScaleBias);
				ls = _mm256_shuffle_epi8(ls, localScaleBroadcastor);
				lzp = _mm256_shuffle_epi8(lzp, localScaleBroadcastor);

				b = _mm256_unpacklo_epi8(a, _mm256_setzero_si256()); // 0, 1, 2, 3, 4, 5, 6, 7, 
				a = _mm256_unpackhi_epi8(a, _mm256_setzero_si256()); // 8, 9, a, b, c, d, e, f,

				lsb = _mm256_unpacklo_epi8(ls, _mm256_setzero_si256());
				lsa = _mm256_unpackhi_epi8(ls, _mm256_setzero_si256());

				lzpb = _mm256_unpacklo_epi8(lzp, _mm256_setzero_si256());
				lzpa = _mm256_unpackhi_epi8(lzp, _mm256_setzero_si256());

				b = _mm256_mullo_epi16(_mm256_sub_epi16(b, lzpb), lsb);
				a = _mm256_mullo_epi16(_mm256_sub_epi16(a, lzpa), lsa);

				b = _mm256_add_epi16(b, _mm256_sign_epi16(roundBias, b));
				a = _mm256_add_epi16(a, _mm256_sign_epi16(roundBias, a));

				b = _mm256_mulhrs_epi16(b, divideBy9);
				a = _mm256_mulhrs_epi16(a, divideBy9);

				b = _mm256_shuffle_epi8(b, compressIdx);
				a = _mm256_shuffle_epi8(a, compressIdx);

				a = _mm256_unpacklo_epi64(b, a);
				if (toUint8) a = _mm256_add_epi8(a, uintBias);
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(out + i * 2), a);
			}

			return globalScale / 8;
		}
	}
}

#define Eigen EigenAVX2
#define ARCH_TYPE ArchType::avx2
#include "eigen_gemm.hpp"
