#include "../MathFunc.hpp"
#include "../qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::sse4_1>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::sse4_1>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::sse4_1>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::sse4_1>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::sse4_1>(const uint8_t* a, const int8_t* b, size_t n);
		
		static FORCE_INLINE __m128i dpbusd(__m128i src, __m128i a, __m128i b)
		{
			__m128i one16 = _mm_set1_epi16(1);
			__m128i t0 = _mm_maddubs_epi16(a, b);
			__m128i t1 = _mm_madd_epi16(t0, one16);
			return _mm_add_epi32(src, t1);
		}

		inline void pack4x16to4x4x4(
			const void* a0, const void* a1, const void* a2, const void* a3,
			__m128i& p0, __m128i& p1, __m128i& p2, __m128i& p3
		)
		{
			__m128i q0, q1, q2, q3;
			// 00, 01, 02, 03, 04, 05, 06, 07, ...
			p0 = _mm_loadu_si128((const __m128i*)a0);
			p1 = _mm_loadu_si128((const __m128i*)a1);
			p2 = _mm_loadu_si128((const __m128i*)a2);
			p3 = _mm_loadu_si128((const __m128i*)a3);

			// 00, 10, 01, 11, 04, 14, 05, 15, ...
			q0 = _mm_unpacklo_epi32(p0, p1);
			// 02, 12, 03, 13, 06, 16, 07, 17, ...
			q1 = _mm_unpackhi_epi32(p0, p1);
			// 20, 30, 21, 31, 24, 34, 25, 35, ...
			q2 = _mm_unpacklo_epi32(p2, p3);
			// 22, 32, 23, 33, 26, 36, 27, 37, ...
			q3 = _mm_unpackhi_epi32(p2, p3);

			// 00, 10, 20, 30, 04, 14, 24, 34, ...
			p0 = _mm_unpacklo_epi64(q0, q2);
			// 01, 11, 21, 31, 05, 15, 25, 35, ...
			p1 = _mm_unpackhi_epi64(q0, q2);
			// 02, 12, 22, 32, 06, 16, 26, 36, ...
			p2 = _mm_unpacklo_epi64(q1, q3);
			// 03, 13, 23, 33, 07, 17, 27, 37, ...
			p3 = _mm_unpackhi_epi64(q1, q3);
		}

		inline void scatteredGEMV_128(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			constexpr size_t packM = 4, packN = 1, packK = 384;
			auto* buffer = SharedThreadLocalBuffer<>::get();
			int8_t* bBuffer = reinterpret_cast<int8_t*>(buffer);
			float* aScale = reinterpret_cast<float*>(bBuffer + packN * packK);
			float* aBias = aScale + packM;
			memcpy(bBuffer, b, k);
			float bScale = *reinterpret_cast<const float*>(b + k);
			int32_t bSum = *reinterpret_cast<const int32_t*>(b + k + 4);

			__m128i pa[4], pb, pbs, psum, pbSum;
			__m128 paScale, paBias, pbScale, r;
			pbScale = _mm_set1_ps(bScale);
			pbSum = _mm_set1_epi32(-bSum);

			for (size_t mi = 0; mi < m; mi += packM)
			{
				const size_t microM = std::min(packM, m - mi);
				const int32_t aOffsets[4] = {
					(int32_t)(aIdx[0] * aIdxScale),
					1 < microM ? (int32_t)(aIdx[1] * aIdxScale) : 0,
					2 < microM ? (int32_t)(aIdx[2] * aIdxScale) : 0,
					3 < microM ? (int32_t)(aIdx[3] * aIdxScale) : 0,
				};
				auto* aPtr = aBase;
				psum = pbSum;
				for (size_t j = 0; j < k; j += 16)
				{
					pack4x16to4x4x4(aPtr + aOffsets[0],
						aPtr + aOffsets[1],
						aPtr + aOffsets[2],
						aPtr + aOffsets[3],
						pa[0], pa[1], pa[2], pa[3]);
					pb = _mm_loadu_si128((const __m128i*)(bBuffer + j));
					pbs = _mm_shuffle_epi32(pb, 0x00);
					psum = dpbusd(psum, pa[0], pbs);
					pbs = _mm_shuffle_epi32(pb, 0x55);
					psum = dpbusd(psum, pa[1], pbs);
					pbs = _mm_shuffle_epi32(pb, 0xAA);
					psum = dpbusd(psum, pa[2], pbs);
					pbs = _mm_shuffle_epi32(pb, 0xFF);
					psum = dpbusd(psum, pa[3], pbs);
					aPtr += 16;
				}
				for (size_t i = 0; i < 4; ++i)
				{
					aScale[i] = *reinterpret_cast<const float*>(aPtr + aOffsets[i]);
					aBias[i] = *reinterpret_cast<const float*>(aPtr + aOffsets[i] + 4);
				}
				aIdx += 4;

				paScale = _mm_loadu_ps(aScale);
				paBias = _mm_loadu_ps(aBias);
				r = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(_mm_cvtepi32_ps(psum), pbScale), paScale), paBias);
				_mm_storeu_ps(c, r);
				c += microM;
			}
		}

		template<>
		inline void scatteredGEMV<ArchType::sse4_1>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV_128(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template void scatteredGEMMOpt<ArchType::sse4_1>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		inline int32_t reduce_sum(__m128i x)
		{
			__m128i hi64 = _mm_unpackhi_epi64(x, x);
			__m128i sum64 = _mm_add_epi32(hi64, x);
			__m128i hi32 = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
			__m128i sum32 = _mm_add_epi32(sum64, hi32);
			return _mm_cvtsi128_si32(sum32);
		}

		inline void gemvS8S8_128(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			const __m128i pBias = _mm_set1_epi8(128);
			__m128i pa, pb[4], psum;
			__m128 ps, paScale, pbScale;
			float aScale = *reinterpret_cast<const float*>(a + k);
			float bScale[4];
			int32_t bSum[4];
			paScale = _mm_set1_ps(aScale);

			for (size_t mi = 0; mi < m; mi += 4)
			{
				auto* aPtr = a;
				auto* bPtr = b + ldb * mi;
				psum = _mm_setzero_si128();
				for (size_t j = 0; j < k; j += 16)
				{
					pack4x16to4x4x4(
						bPtr,
						bPtr + 1 * ldb,
						bPtr + 2 * ldb,
						bPtr + 3 * ldb,
						pb[0], pb[1], pb[2], pb[3]
					);
					pa = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aPtr));
					pa = _mm_add_epi8(pa, pBias);
					psum = dpbusd(psum, _mm_shuffle_epi32(pa, 0x00), pb[0]);
					psum = dpbusd(psum, _mm_shuffle_epi32(pa, 0x55), pb[1]);
					psum = dpbusd(psum, _mm_shuffle_epi32(pa, 0xAA), pb[2]);
					psum = dpbusd(psum, _mm_shuffle_epi32(pa, 0xFF), pb[3]);
					aPtr += 16;
					bPtr += 16;
				}
				for (size_t i = 0; i < 4; ++i)
				{
					bScale[i] = *reinterpret_cast<const float*>(bPtr + i * ldb);
					bSum[i] = *reinterpret_cast<const int32_t*>(bPtr + i * ldb + 4);
				}
				ps = _mm_cvtepi32_ps(_mm_sub_epi32(psum, _mm_loadu_si128(reinterpret_cast<const __m128i*>(bSum))));
				pbScale = _mm_loadu_ps(bScale);
				ps = _mm_mul_ps(_mm_mul_ps(ps, paScale), pbScale);
				_mm_storeu_ps(c + mi, ps);
			}
		}

		inline void gemvU8U8_128(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			const __m128i pBias = _mm_set1_epi8(128), pOne = _mm_set1_epi8(1);
			__m128i pa, pb[4], psum, paSum;
			__m128 ps, paScale, pbScale;
			float aScale = *reinterpret_cast<const float*>(a + k);
			float bScale[4];
			int32_t aSum;
			paScale = _mm_set1_ps(aScale);

			psum = _mm_setzero_si128();
			for (size_t j = 0; j < k; j += 16)
			{
				pa = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + j));
				psum = dpbusd(psum, pOne, _mm_sub_epi8(pa, pBias));
			}
			aSum = reduce_sum(psum) << 7;
			paSum = _mm_set1_epi32(-aSum);

			for (size_t mi = 0; mi < m; mi += 4)
			{
				auto* aPtr = a;
				auto* bPtr = b + ldb * mi;
				psum = paSum;
				for (size_t j = 0; j < k; j += 16)
				{
					pack4x16to4x4x4(
						bPtr,
						bPtr + 1 * ldb,
						bPtr + 2 * ldb,
						bPtr + 3 * ldb,
						pb[0], pb[1], pb[2], pb[3]
					);
					pa = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aPtr));
					pa = _mm_sub_epi8(pa, pBias);
					psum = dpbusd(psum, pb[0], _mm_shuffle_epi32(pa, 0x00));
					psum = dpbusd(psum, pb[1], _mm_shuffle_epi32(pa, 0x55));
					psum = dpbusd(psum, pb[2], _mm_shuffle_epi32(pa, 0xAA));
					psum = dpbusd(psum, pb[3], _mm_shuffle_epi32(pa, 0xFF));
					aPtr += 16;
					bPtr += 16;
				}
				for (size_t i = 0; i < 4; ++i)
				{
					bScale[i] = *reinterpret_cast<const float*>(bPtr + i * ldb);
				}
				ps = _mm_cvtepi32_ps(psum);
				pbScale = _mm_loadu_ps(bScale);
				ps = _mm_mul_ps(_mm_mul_ps(ps, paScale), pbScale);
				_mm_storeu_ps(c + mi, ps);
			}
		}

		inline void invNormS8_128(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			const __m128i pBias = _mm_set1_epi8(128);
			__m128i pa[4], pb[4], psum;
			__m128 ps, pScale;
			float bScale[4];
			int32_t bSum[4];

			for (size_t mi = 0; mi < m; mi += 4)
			{
				auto* aPtr = a + lda * mi;
				psum = _mm_setzero_si128();
				for (size_t j = 0; j < k; j += 16)
				{
					pack4x16to4x4x4(
						aPtr,
						aPtr + 1 * lda,
						aPtr + 2 * lda,
						aPtr + 3 * lda,
						pb[0], pb[1], pb[2], pb[3]
					);
					pa[0] = _mm_add_epi8(pb[0], pBias);
					pa[1] = _mm_add_epi8(pb[1], pBias);
					pa[2] = _mm_add_epi8(pb[2], pBias);
					pa[3] = _mm_add_epi8(pb[3], pBias);
					psum = dpbusd(psum, pa[0], pb[0]);
					psum = dpbusd(psum, pa[1], pb[1]);
					psum = dpbusd(psum, pa[2], pb[2]);
					psum = dpbusd(psum, pa[3], pb[3]);
					aPtr += 16;
				}
				for (size_t i = 0; i < 4; ++i)
				{
					bScale[i] = *reinterpret_cast<const float*>(aPtr + i * lda);
					bSum[i] = *reinterpret_cast<const int32_t*>(aPtr + i * lda + 4);
				}
				ps = _mm_cvtepi32_ps(_mm_sub_epi32(psum, _mm_loadu_si128(reinterpret_cast<const __m128i*>(bSum))));
				pScale = _mm_loadu_ps(bScale);
				ps = _mm_mul_ps(_mm_mul_ps(ps, pScale), pScale);
				ps = _mm_rsqrt_ps(ps);
				_mm_storeu_ps(out + mi, ps);
			}
		}

		inline void invNormU8_128(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			const __m128i pBias = _mm_set1_epi8(128), pOne = _mm_set1_epi8(1);
			__m128i pa[4], pb[4], psum, pHSum;
			__m128 ps, pScale;
			float aScale[4];

			for (size_t mi = 0; mi < m; mi += 4)
			{
				auto* aPtr = a + lda * mi;
				psum = _mm_setzero_si128();
				pHSum = _mm_setzero_si128();
				for (size_t j = 0; j < k; j += 16)
				{
					pack4x16to4x4x4(
						aPtr,
						aPtr + 1 * lda,
						aPtr + 2 * lda,
						aPtr + 3 * lda,
						pa[0], pa[1], pa[2], pa[3]
					);
					pb[0] = _mm_sub_epi8(pa[0], pBias);
					pb[1] = _mm_sub_epi8(pa[1], pBias);
					pb[2] = _mm_sub_epi8(pa[2], pBias);
					pb[3] = _mm_sub_epi8(pa[3], pBias);
					psum = dpbusd(psum, pa[0], pb[0]);
					psum = dpbusd(psum, pa[1], pb[1]);
					psum = dpbusd(psum, pa[2], pb[2]);
					psum = dpbusd(psum, pa[3], pb[3]);
					pHSum = dpbusd(pHSum, pOne, pb[0]);
					pHSum = dpbusd(pHSum, pOne, pb[1]);
					pHSum = dpbusd(pHSum, pOne, pb[2]);
					pHSum = dpbusd(pHSum, pOne, pb[3]);
					aPtr += 16;
				}
				for (size_t i = 0; i < 4; ++i)
				{
					aScale[i] = *reinterpret_cast<const float*>(aPtr + i * lda);
				}
				pHSum = _mm_slli_epi32(pHSum, 7);
				ps = _mm_cvtepi32_ps(_mm_sub_epi32(psum, pHSum));
				pScale = _mm_loadu_ps(aScale);
				ps = _mm_mul_ps(_mm_mul_ps(ps, pScale), pScale);
				ps = _mm_rsqrt_ps(ps);
				_mm_storeu_ps(out + mi, ps);
			}
		}

		template<>
		void gemvS8S8<ArchType::sse4_1>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return gemvS8S8_128(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::sse4_1>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return gemvU8U8_128(m, k, a, b, ldb, c);
		}

		template<>
		void invNormS8<ArchType::sse4_1>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return invNormS8_128(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::sse4_1>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return invNormU8_128(m, k, a, lda, out);
		}
	}
}

#define Eigen EigenSSE4_1
#define ARCH_TYPE ArchType::sse4_1
#include "eigen_gemm.hpp"
