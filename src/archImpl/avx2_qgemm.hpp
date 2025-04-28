#pragma once
#include "../qgemm.hpp"
#include <cstring>

#ifdef USE_VNNI
#define DPBUSD _mm256_dpbusd_epi32
#define DETAIL detailVnni
#else
#define DPBUSD emulated_dpbusd
#define DETAIL detail
#endif

namespace kiwi
{
	namespace qgemm
	{
		namespace DETAIL
		{
			// emulate _mm256_dpbusd_epi32 using AVX2
			static FORCE_INLINE __m256i emulated_dpbusd(__m256i src, __m256i a, __m256i b)
			{
				__m256i one16 = _mm256_set1_epi16(1);
				__m256i t0 = _mm256_maddubs_epi16(a, b);
				__m256i t1 = _mm256_madd_epi16(t0, one16);
				return _mm256_add_epi32(src, t1);
			}

			inline void pack4x32to4x8x4(
				const void* a0, const void* a1, const void* a2, const void* a3,
				__m256i& p0, __m256i& p1, __m256i& p2, __m256i& p3
			)
			{
				__m256i q0, q1, q2, q3;
				// 00, 01, 02, 03, 04, 05, 06, 07, ...
				p0 = _mm256_loadu_si256((const __m256i*)a0);
				p1 = _mm256_loadu_si256((const __m256i*)a1);
				p2 = _mm256_loadu_si256((const __m256i*)a2);
				p3 = _mm256_loadu_si256((const __m256i*)a3);

				// 00, 10, 01, 11, 04, 14, 05, 15, ...
				q0 = _mm256_unpacklo_epi32(p0, p1);
				// 02, 12, 03, 13, 06, 16, 07, 17, ...
				q1 = _mm256_unpackhi_epi32(p0, p1);
				// 20, 30, 21, 31, 24, 34, 25, 35, ...
				q2 = _mm256_unpacklo_epi32(p2, p3);
				// 22, 32, 23, 33, 26, 36, 27, 37, ...
				q3 = _mm256_unpackhi_epi32(p2, p3);

				// 00, 10, 20, 30, 04, 14, 24, 34, ...
				p0 = _mm256_unpacklo_epi64(q0, q2);
				// 01, 11, 21, 31, 05, 15, 25, 35, ...
				p1 = _mm256_unpackhi_epi64(q0, q2);
				// 02, 12, 22, 32, 06, 16, 26, 36, ...
				p2 = _mm256_unpacklo_epi64(q1, q3);
				// 03, 13, 23, 33, 07, 17, 27, 37, ...
				p3 = _mm256_unpackhi_epi64(q1, q3);
			}

			inline void scatteredGEMV_256(
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

				__m256i pa[4], pb, pbs, psum, pbSum;
				__m128 paScale, paBias, pbScale, r;
				pbScale = _mm_set1_ps(bScale);
				pbSum = _mm256_set1_epi32(-bSum / 2);
				__m128i pr;

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
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(aPtr + aOffsets[0],
							aPtr + aOffsets[1],
							aPtr + aOffsets[2],
							aPtr + aOffsets[3],
							pa[0], pa[1], pa[2], pa[3]);
						pb = _mm256_loadu_si256((const __m256i*)(bBuffer + j));
						pbs = _mm256_shuffle_epi32(pb, 0x00);
						psum = DPBUSD(psum, pa[0], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0x55);
						psum = DPBUSD(psum, pa[1], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0xAA);
						psum = DPBUSD(psum, pa[2], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0xFF);
						psum = DPBUSD(psum, pa[3], pbs);
						aPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						aScale[i] = *reinterpret_cast<const float*>(aPtr + aOffsets[i]);
						aBias[i] = *reinterpret_cast<const float*>(aPtr + aOffsets[i] + 4);
					}
					pr = _mm_add_epi32(_mm256_castsi256_si128(psum), _mm256_extracti128_si256(psum, 1));
					aIdx += 4;

					paScale = _mm_loadu_ps(aScale);
					paBias = _mm_loadu_ps(aBias);
					r = _mm_fmadd_ps(_mm_mul_ps(_mm_cvtepi32_ps(pr), pbScale), paScale, paBias);
					_mm_storeu_ps(c, r);
					c += microM;
				}
			}

			inline void scatteredGEMV8x1_256(
				size_t m, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* b,
				float* c
			)
			{
				constexpr size_t packM = 8, packN = 1, packK = 384;
				auto* buffer = SharedThreadLocalBuffer<>::get();
				int8_t* bBuffer = reinterpret_cast<int8_t*>(buffer);
				float* aScale = reinterpret_cast<float*>(bBuffer + packN * packK);
				float* aBias = aScale + packM;
				memcpy(bBuffer, b, k);
				float bScale = *reinterpret_cast<const float*>(b + k);
				int32_t bSum = *reinterpret_cast<const int32_t*>(b + k + 4);

				__m256i pa[4], pb, pbs, psum, pbSum;
				__m256 paScale, paBias, pbScale, r;
				__m256i pr = _mm256_setzero_si256();
				pbScale = _mm256_set1_ps(bScale);
				pbSum = _mm256_set1_epi32(-bSum / 2);

				{
					auto* aPtr = aBase;
					psum = pbSum;
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(aPtr + aIdx[0] * aIdxScale,
							aPtr + aIdx[1] * aIdxScale,
							aPtr + aIdx[2] * aIdxScale,
							aPtr + aIdx[3] * aIdxScale,
							pa[0], pa[1], pa[2], pa[3]);
						pb = _mm256_loadu_si256((const __m256i*)(bBuffer + j));
						pbs = _mm256_shuffle_epi32(pb, 0x00);
						psum = DPBUSD(psum, pa[0], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0x55);
						psum = DPBUSD(psum, pa[1], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0xAA);
						psum = DPBUSD(psum, pa[2], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0xFF);
						psum = DPBUSD(psum, pa[3], pbs);
						aPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						aScale[i] = *reinterpret_cast<const float*>(aPtr + aIdx[i] * aIdxScale);
						aBias[i] = *reinterpret_cast<const float*>(aPtr + aIdx[i] * aIdxScale + 4);
					}
					pr = _mm256_add_epi32(psum, _mm256_castsi128_si256(_mm256_extracti128_si256(psum, 1)));
					aIdx += 4;
				}
				{
					auto* aPtr = aBase;
					psum = pbSum;
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(aPtr + aIdx[0] * aIdxScale,
							aPtr + aIdx[1] * aIdxScale,
							aPtr + aIdx[2] * aIdxScale,
							aPtr + aIdx[3] * aIdxScale,
							pa[0], pa[1], pa[2], pa[3]);
						pb = _mm256_loadu_si256((const __m256i*)(bBuffer + j));
						pbs = _mm256_shuffle_epi32(pb, 0x00);
						psum = DPBUSD(psum, pa[0], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0x55);
						psum = DPBUSD(psum, pa[1], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0xAA);
						psum = DPBUSD(psum, pa[2], pbs);
						pbs = _mm256_shuffle_epi32(pb, 0xFF);
						psum = DPBUSD(psum, pa[3], pbs);
						aPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						aScale[i + 4] = *reinterpret_cast<const float*>(aPtr + aIdx[i] * aIdxScale);
						aBias[i + 4] = *reinterpret_cast<const float*>(aPtr + aIdx[i] * aIdxScale + 4);
					}
					psum = _mm256_add_epi32(psum, _mm256_castsi128_si256(_mm256_extracti128_si256(psum, 1)));
					pr = _mm256_inserti128_si256(pr, _mm256_castsi256_si128(psum), 1);
					aIdx += 4;
				}

				paScale = _mm256_loadu_ps(aScale);
				paBias = _mm256_loadu_ps(aBias);
				r = _mm256_fmadd_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(pr), pbScale), paScale, paBias);
				_mm256_storeu_ps(c, r);
				c += 8;
			}

			inline void scatteredGEMV2_256(
				size_t m, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c
			)
			{
				constexpr size_t packM = 4, packN = 2, packK = 384;
				auto* buffer = SharedThreadLocalBuffer<>::get();
				int8_t* bBuffer = reinterpret_cast<int8_t*>(buffer);
				float* aScale = reinterpret_cast<float*>(bBuffer + packN * packK);
				float* aBias = aScale + packM * 2;
				memcpy(bBuffer, bBase + bIdx[0] * bIdxScale, k);
				memcpy(bBuffer + packK, bBase + bIdx[1] * bIdxScale, k);
				float bScale[2] = {
					*reinterpret_cast<const float*>(bBase + bIdx[0] * bIdxScale + k),
					*reinterpret_cast<const float*>(bBase + bIdx[1] * bIdxScale + k)
				};
				int32_t bSum[2] = {
					*reinterpret_cast<const int32_t*>(bBase + bIdx[0] * bIdxScale + k + 4),
					*reinterpret_cast<const int32_t*>(bBase + bIdx[1] * bIdxScale + k + 4)
				};

				__m256i pa[4], pb[2], psum[2], pbSum[2], pt[2];
				__m256 paScale, paBias, pbScale, r;
				__m256i pr;
				pbScale = _mm256_castsi256_ps(_mm256_set1_epi64x(*reinterpret_cast<const int64_t*>(bScale)));
				pbSum[0] = _mm256_set1_epi32(-bSum[0] / 2);
				pbSum[1] = _mm256_set1_epi32(-bSum[1] / 2);

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
					psum[0] = pbSum[0];
					psum[1] = pbSum[1];
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(aPtr + aOffsets[0],
							aPtr + aOffsets[1],
							aPtr + aOffsets[2],
							aPtr + aOffsets[3],
							pa[0], pa[1], pa[2], pa[3]);
						pb[0] = _mm256_loadu_si256((const __m256i*)(bBuffer + j));
						pb[1] = _mm256_loadu_si256((const __m256i*)(bBuffer + j + packK));
						psum[0] = DPBUSD(psum[0], pa[0], _mm256_shuffle_epi32(pb[0], 0x00));
						psum[0] = DPBUSD(psum[0], pa[1], _mm256_shuffle_epi32(pb[0], 0x55));
						psum[0] = DPBUSD(psum[0], pa[2], _mm256_shuffle_epi32(pb[0], 0xAA));
						psum[0] = DPBUSD(psum[0], pa[3], _mm256_shuffle_epi32(pb[0], 0xFF));
						psum[1] = DPBUSD(psum[1], pa[0], _mm256_shuffle_epi32(pb[1], 0x00));
						psum[1] = DPBUSD(psum[1], pa[1], _mm256_shuffle_epi32(pb[1], 0x55));
						psum[1] = DPBUSD(psum[1], pa[2], _mm256_shuffle_epi32(pb[1], 0xAA));
						psum[1] = DPBUSD(psum[1], pa[3], _mm256_shuffle_epi32(pb[1], 0xFF));
						aPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						aScale[i * 2] = aScale[i * 2 + 1] = *reinterpret_cast<const float*>(aPtr + aOffsets[i]);
						aBias[i * 2] = aBias[i * 2 + 1] = *reinterpret_cast<const float*>(aPtr + aOffsets[i] + 4);
					}
					psum[0] = _mm256_add_epi32(psum[0], _mm256_castsi128_si256(_mm256_extracti128_si256(psum[0], 1)));
					psum[1] = _mm256_add_epi32(psum[1], _mm256_castsi128_si256(_mm256_extracti128_si256(psum[1], 1)));

					// 00, 01, 10, 11, ...
					pt[0] = _mm256_unpacklo_epi32(psum[0], psum[1]);
					// 20, 21, 30, 31, ...
					pt[1] = _mm256_unpackhi_epi32(psum[0], psum[1]);

					// 00, 01, 10, 11, 20, 21, 30, 31
					pr = _mm256_inserti128_si256(pt[0], _mm256_castsi256_si128(pt[1]), 1);
					paScale = _mm256_loadu_ps(aScale);
					paBias = _mm256_loadu_ps(aBias);
					r = _mm256_fmadd_ps(_mm256_mul_ps(_mm256_cvtepi32_ps(pr), pbScale), paScale, paBias);
					_mm256_storeu_ps(c, r);

					aIdx += microM;
					c += microM * 2;
				}
			}

			inline int32_t reduce_sum(__m128i x)
			{
				__m128i hi64 = _mm_unpackhi_epi64(x, x);
				__m128i sum64 = _mm_add_epi32(hi64, x);
				__m128i hi32 = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
				__m128i sum32 = _mm_add_epi32(sum64, hi32);
				return _mm_cvtsi128_si32(sum32);
			}

			inline int32_t reduce_sum(__m256i x)
			{
				__m128i sum128 = _mm_add_epi32(
					_mm256_castsi256_si128(x),
					_mm256_extracti128_si256(x, 1));
				return reduce_sum(sum128);
			}

			template<size_t m, size_t n>
			inline void scatteredGEMMSmall_256(size_t, size_t, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c, size_t ldc)
			{
				static_assert(m <= 3, "m should be less than or equal to 3");
				static_assert(n <= 3, "n should be less than or equal to 3");
				__m256i pa[3], pb[3], psum[3][3];
				const uint8_t* aPtr[3];
				const int8_t* bPtr[3];

				psum[0][0] = _mm256_setzero_si256();
				if (m > 1) psum[1][0] = _mm256_setzero_si256();
				if (m > 2) psum[2][0] = _mm256_setzero_si256();
				if (n > 1) psum[0][1] = _mm256_setzero_si256();
				if (m > 1 && n > 1) psum[1][1] = _mm256_setzero_si256();
				if (m > 2 && n > 1) psum[2][1] = _mm256_setzero_si256();
				if (n > 2) psum[0][2] = _mm256_setzero_si256();
				if (m > 1 && n > 2) psum[1][2] = _mm256_setzero_si256();
				if (m > 2 && n > 2) psum[2][2] = _mm256_setzero_si256();

				aPtr[0] = aBase + aIdx[0] * aIdxScale;
				if (m > 1) aPtr[1] = aBase + aIdx[1] * aIdxScale;
				if (m > 2) aPtr[2] = aBase + aIdx[2] * aIdxScale;

				bPtr[0] = bBase + bIdx[0] * bIdxScale;
				if (n > 1) bPtr[1] = bBase + bIdx[1] * bIdxScale;
				if (n > 2) bPtr[2] = bBase + bIdx[2] * bIdxScale;

				for (size_t x = 0; x < k; x += 32)
				{
					if (m > 0)
					{
						pa[0] = _mm256_loadu_si256((const __m256i*)aPtr[0]);
						aPtr[0] += 32;
					}
					if (m > 1)
					{
						pa[1] = _mm256_loadu_si256((const __m256i*)aPtr[1]);
						aPtr[1] += 32;
					}
					if (m > 2)
					{
						pa[2] = _mm256_loadu_si256((const __m256i*)aPtr[2]);
						aPtr[2] += 32;
					}

					if (n > 0)
					{
						pb[0] = _mm256_loadu_si256((const __m256i*)bPtr[0]);
						bPtr[0] += 32;
					}
					if (n > 1)
					{
						pb[1] = _mm256_loadu_si256((const __m256i*)bPtr[1]);
						bPtr[1] += 32;
					}
					if (n > 2)
					{
						pb[2] = _mm256_loadu_si256((const __m256i*)bPtr[2]);
						bPtr[2] += 32;
					}

					psum[0][0] = DPBUSD(psum[0][0], pa[0], pb[0]);
					if (m > 1) psum[1][0] = DPBUSD(psum[1][0], pa[1], pb[0]);
					if (m > 2) psum[2][0] = DPBUSD(psum[2][0], pa[2], pb[0]);
					if (n > 1) psum[0][1] = DPBUSD(psum[0][1], pa[0], pb[1]);
					if (m > 1 && n > 1) psum[1][1] = DPBUSD(psum[1][1], pa[1], pb[1]);
					if (m > 2 && n > 1) psum[2][1] = DPBUSD(psum[2][1], pa[2], pb[1]);
					if (n > 2) psum[0][2] = DPBUSD(psum[0][2], pa[0], pb[2]);
					if (m > 1 && n > 2) psum[1][2] = DPBUSD(psum[1][2], pa[1], pb[2]);
					if (m > 2 && n > 2) psum[2][2] = DPBUSD(psum[2][2], pa[2], pb[2]);
				}

				float contextScale[3], outputScale[3], contextBias[3];
				int32_t hsum[3];

				if (m > 0)
				{
					contextScale[0] = *reinterpret_cast<const float*>(aPtr[0]);
					contextBias[0] = *reinterpret_cast<const float*>(aPtr[0] + 4);
				}
				if (m > 1)
				{
					contextScale[1] = *reinterpret_cast<const float*>(aPtr[1]);
					contextBias[1] = *reinterpret_cast<const float*>(aPtr[1] + 4);
				}
				if (m > 2)
				{
					contextScale[2] = *reinterpret_cast<const float*>(aPtr[2]);
					contextBias[2] = *reinterpret_cast<const float*>(aPtr[2] + 4);
				}


				if (n > 0)
				{
					outputScale[0] = *reinterpret_cast<const float*>(bPtr[0]);
					hsum[0] = *reinterpret_cast<const int32_t*>(bPtr[0] + 4);
				}
				if (n > 1)
				{
					outputScale[1] = *reinterpret_cast<const float*>(bPtr[1]);
					hsum[1] = *reinterpret_cast<const int32_t*>(bPtr[1] + 4);
				}
				if (n > 2)
				{
					outputScale[2] = *reinterpret_cast<const float*>(bPtr[2]);
					hsum[2] = *reinterpret_cast<const int32_t*>(bPtr[2] + 4);
				}

				{
					int32_t acc = reduce_sum(psum[0][0]);
					c[0] = (acc - hsum[0]) * contextScale[0] * outputScale[0] + contextBias[0];
				}
				if (m > 1)
				{
					int32_t acc = reduce_sum(psum[1][0]);
					c[ldc] = (acc - hsum[0]) * contextScale[1] * outputScale[0] + contextBias[1];
				}
				if (m > 2)
				{
					int32_t acc = reduce_sum(psum[2][0]);
					c[ldc * 2] = (acc - hsum[0]) * contextScale[2] * outputScale[0] + contextBias[2];
				}
				if (n > 1)
				{
					int32_t acc = reduce_sum(psum[0][1]);
					c[1] = (acc - hsum[1]) * contextScale[0] * outputScale[1] + contextBias[0];
				}
				if (m > 1 && n > 1)
				{
					int32_t acc = reduce_sum(psum[1][1]);
					c[ldc + 1] = (acc - hsum[1]) * contextScale[1] * outputScale[1] + contextBias[1];
				}
				if (m > 2 && n > 1)
				{
					int32_t acc = reduce_sum(psum[2][1]);
					c[ldc * 2 + 1] = (acc - hsum[1]) * contextScale[2] * outputScale[1] + contextBias[2];
				}
				if (n > 2)
				{
					int32_t acc = reduce_sum(psum[0][2]);
					c[2] = (acc - hsum[2]) * contextScale[0] * outputScale[2] + contextBias[0];
				}
				if (m > 1 && n > 2)
				{
					int32_t acc = reduce_sum(psum[1][2]);
					c[ldc + 2] = (acc - hsum[2]) * contextScale[1] * outputScale[2] + contextBias[1];
				}
				if (m > 2 && n > 2)
				{
					int32_t acc = reduce_sum(psum[2][2]);
					c[ldc * 2 + 2] = (acc - hsum[2]) * contextScale[2] * outputScale[2] + contextBias[2];
				}
			}

			inline void gemv_256(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
			{
				__m256i pa, pb[4], psum;
				__m128i pr;
				__m128 ps, paScale, pbScale;
				float aScale = *reinterpret_cast<const float*>(a + k);
				float bScale[4];
				int32_t bSum[4];
				paScale = _mm_set1_ps(aScale);

				for (size_t mi = 0; mi < m; mi += 4)
				{
					auto* aPtr = a;
					auto* bPtr = b + ldb * mi;
					psum = _mm256_setzero_si256();
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(
							bPtr,
							bPtr + 1 * ldb,
							bPtr + 2 * ldb,
							bPtr + 3 * ldb,
							pb[0], pb[1], pb[2], pb[3]
						);
						pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(aPtr));
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0x00), pb[0]);
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0x55), pb[1]);
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0xAA), pb[2]);
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0xFF), pb[3]);
						aPtr += 32;
						bPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						bScale[i] = *reinterpret_cast<const float*>(bPtr + i * ldb);
						bSum[i] = *reinterpret_cast<const int32_t*>(bPtr + i * ldb + 4);
					}
					pr = _mm_add_epi32(_mm256_castsi256_si128(psum), _mm256_extracti128_si256(psum, 1));
					ps = _mm_cvtepi32_ps(_mm_sub_epi32(pr, _mm_loadu_si128(reinterpret_cast<const __m128i*>(bSum))));
					pbScale = _mm_loadu_ps(bScale);
					ps = _mm_mul_ps(_mm_mul_ps(ps, paScale), pbScale);
					_mm_storeu_ps(c + mi, ps);
				}
			}

			inline void gemvS8S8_256(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
			{
				const __m256i pBias = _mm256_set1_epi8(128);
				__m256i pa, pb[4], psum;
				__m128i pr;
				__m128 ps, paScale, pbScale;
				float aScale = *reinterpret_cast<const float*>(a + k);
				float bScale[4];
				int32_t bSum[4];
				paScale = _mm_set1_ps(aScale);

				for (size_t mi = 0; mi < m; mi += 4)
				{
					auto* aPtr = a;
					auto* bPtr = b + ldb * mi;
					psum = _mm256_setzero_si256();
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(
							bPtr,
							bPtr + 1 * ldb,
							bPtr + 2 * ldb,
							bPtr + 3 * ldb,
							pb[0], pb[1], pb[2], pb[3]
						);
						pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(aPtr));
						pa = _mm256_add_epi8(pa, pBias);
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0x00), pb[0]);
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0x55), pb[1]);
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0xAA), pb[2]);
						psum = DPBUSD(psum, _mm256_shuffle_epi32(pa, 0xFF), pb[3]);
						aPtr += 32;
						bPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						bScale[i] = *reinterpret_cast<const float*>(bPtr + i * ldb);
						bSum[i] = *reinterpret_cast<const int32_t*>(bPtr + i * ldb + 4);
					}
					pr = _mm_add_epi32(_mm256_castsi256_si128(psum), _mm256_extracti128_si256(psum, 1));
					ps = _mm_cvtepi32_ps(_mm_sub_epi32(pr, _mm_loadu_si128(reinterpret_cast<const __m128i*>(bSum))));
					pbScale = _mm_loadu_ps(bScale);
					ps = _mm_mul_ps(_mm_mul_ps(ps, paScale), pbScale);
					_mm_storeu_ps(c + mi, ps);
				}
			}

			inline void gemvU8U8_256(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
			{
				const __m256i pBias = _mm256_set1_epi8(128), pOne = _mm256_set1_epi8(1);
				__m256i pa, pb[4], psum, paSum;
				__m128i pr;
				__m128 ps, paScale, pbScale;
				float aScale = *reinterpret_cast<const float*>(a + k);
				float bScale[4];
				int32_t aSum;
				paScale = _mm_set1_ps(aScale);

				psum = _mm256_setzero_si256();
				for (size_t j = 0; j < k; j += 32)
				{
					pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + j));
					psum = DPBUSD(psum, pOne, _mm256_sub_epi8(pa, pBias));
				}
				aSum = reduce_sum(psum) << 7;
				paSum = _mm256_set1_epi32(-aSum / 2);

				for (size_t mi = 0; mi < m; mi += 4)
				{
					auto* aPtr = a;
					auto* bPtr = b + ldb * mi;
					psum = paSum;
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(
							bPtr,
							bPtr + 1 * ldb,
							bPtr + 2 * ldb,
							bPtr + 3 * ldb,
							pb[0], pb[1], pb[2], pb[3]
						);
						pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(aPtr));
						pa = _mm256_sub_epi8(pa, pBias);
						psum = DPBUSD(psum, pb[0], _mm256_shuffle_epi32(pa, 0x00));
						psum = DPBUSD(psum, pb[1], _mm256_shuffle_epi32(pa, 0x55));
						psum = DPBUSD(psum, pb[2], _mm256_shuffle_epi32(pa, 0xAA));
						psum = DPBUSD(psum, pb[3], _mm256_shuffle_epi32(pa, 0xFF));
						aPtr += 32;
						bPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						bScale[i] = *reinterpret_cast<const float*>(bPtr + i * ldb);
					}
					pr = _mm_add_epi32(_mm256_castsi256_si128(psum), _mm256_extracti128_si256(psum, 1));
					ps = _mm_cvtepi32_ps(pr);
					pbScale = _mm_loadu_ps(bScale);
					ps = _mm_mul_ps(_mm_mul_ps(ps, paScale), pbScale);
					_mm_storeu_ps(c + mi, ps);
				}
			}

			inline float dotS8S8_256(size_t k, const int8_t* a, const int8_t* b)
			{
				const __m256i pBias = _mm256_set1_epi8(128);
				__m256i pa, pb, psum;
				float aScale = *reinterpret_cast<const float*>(a + k);
				float bScale = *reinterpret_cast<const float*>(b + k);
				int32_t bSum = *reinterpret_cast<const int32_t*>(b + k + 4);

				psum = _mm256_setzero_si256();
				for (size_t i = 0; i < k; i += 32)
				{
					pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
					pb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i));
					pa = _mm256_add_epi8(pa, pBias);
					psum = DPBUSD(psum, pa, pb);
				}
				return (reduce_sum(psum) - bSum) * aScale * bScale;
			}

			inline float dotU8U8_256(size_t k, const uint8_t* a, const uint8_t* b)
			{
				const __m256i pBias = _mm256_set1_epi8(128);
				__m256i pa, pb, psum, pasum;
				float aScale = *reinterpret_cast<const float*>(a + k);
				float bScale = *reinterpret_cast<const float*>(b + k);
				psum = _mm256_setzero_si256();
				pasum = _mm256_setzero_si256();
				for (size_t i = 0; i < k; i += 32)
				{
					pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
					pa = _mm256_sub_epi8(pa, pBias);
					pb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b + i));
					pasum = DPBUSD(pasum, pBias, pa);
					psum = DPBUSD(psum, pb, pa);
				}
				return (reduce_sum(_mm256_sub_epi32(psum, pasum))) * aScale * bScale;
			}

			inline void invNormS8_256(
				size_t m, size_t k,
				const int8_t* a, size_t lda,
				float* out
			)
			{
				const __m256i pBias = _mm256_set1_epi8(128);
				__m256i pa[4], pb[4], psum;
				__m128i pr;
				__m128 ps, pScale;
				float bScale[4];
				int32_t bSum[4];

				for (size_t mi = 0; mi < m; mi += 4)
				{
					auto* aPtr = a + lda * mi;
					psum = _mm256_setzero_si256();
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(
							aPtr,
							aPtr + 1 * lda,
							aPtr + 2 * lda,
							aPtr + 3 * lda,
							pb[0], pb[1], pb[2], pb[3]
						);
						pa[0] = _mm256_add_epi8(pb[0], pBias);
						pa[1] = _mm256_add_epi8(pb[1], pBias);
						pa[2] = _mm256_add_epi8(pb[2], pBias);
						pa[3] = _mm256_add_epi8(pb[3], pBias);
						psum = DPBUSD(psum, pa[0], pb[0]);
						psum = DPBUSD(psum, pa[1], pb[1]);
						psum = DPBUSD(psum, pa[2], pb[2]);
						psum = DPBUSD(psum, pa[3], pb[3]);
						aPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						bScale[i] = *reinterpret_cast<const float*>(aPtr + i * lda);
						bSum[i] = *reinterpret_cast<const int32_t*>(aPtr + i * lda + 4);
					}
					pr = _mm_add_epi32(_mm256_castsi256_si128(psum), _mm256_extracti128_si256(psum, 1));
					ps = _mm_cvtepi32_ps(_mm_sub_epi32(pr, _mm_loadu_si128(reinterpret_cast<const __m128i*>(bSum))));
					pScale = _mm_loadu_ps(bScale);
					ps = _mm_mul_ps(_mm_mul_ps(ps, pScale), pScale);
					ps = _mm_rsqrt_ps(ps);
					_mm_storeu_ps(out + mi, ps);
				}
			}

			inline void invNormU8_256(
				size_t m, size_t k,
				const uint8_t* a, size_t lda,
				float* out
			)
			{
				const __m256i pBias = _mm256_set1_epi8(128), pOne = _mm256_set1_epi8(1);
				__m256i pa[4], pb[4], psum, pHSum;
				__m128i pr, phr;
				__m128 ps, pScale;
				float aScale[4];

				for (size_t mi = 0; mi < m; mi += 4)
				{
					auto* aPtr = a + lda * mi;
					psum = _mm256_setzero_si256();
					pHSum = _mm256_setzero_si256();
					for (size_t j = 0; j < k; j += 32)
					{
						pack4x32to4x8x4(
							aPtr,
							aPtr + 1 * lda,
							aPtr + 2 * lda,
							aPtr + 3 * lda,
							pa[0], pa[1], pa[2], pa[3]
						);
						pb[0] = _mm256_sub_epi8(pa[0], pBias);
						pb[1] = _mm256_sub_epi8(pa[1], pBias);
						pb[2] = _mm256_sub_epi8(pa[2], pBias);
						pb[3] = _mm256_sub_epi8(pa[3], pBias);
						psum = DPBUSD(psum, pa[0], pb[0]);
						psum = DPBUSD(psum, pa[1], pb[1]);
						psum = DPBUSD(psum, pa[2], pb[2]);
						psum = DPBUSD(psum, pa[3], pb[3]);
						pHSum = DPBUSD(pHSum, pOne, pb[0]);
						pHSum = DPBUSD(pHSum, pOne, pb[1]);
						pHSum = DPBUSD(pHSum, pOne, pb[2]);
						pHSum = DPBUSD(pHSum, pOne, pb[3]);
						aPtr += 32;
					}
					for (size_t i = 0; i < 4; ++i)
					{
						aScale[i] = *reinterpret_cast<const float*>(aPtr + i * lda);
					}
					pr = _mm_add_epi32(_mm256_castsi256_si128(psum), _mm256_extracti128_si256(psum, 1));
					phr = _mm_add_epi32(_mm256_castsi256_si128(pHSum), _mm256_extracti128_si256(pHSum, 1));
					phr = _mm_slli_epi32(phr, 7);
					ps = _mm_cvtepi32_ps(_mm_sub_epi32(pr, phr));
					pScale = _mm_loadu_ps(aScale);
					ps = _mm_mul_ps(_mm_mul_ps(ps, pScale), pScale);
					ps = _mm_rsqrt_ps(ps);
					_mm_storeu_ps(out + mi, ps);
				}
			}
		}
	}
}
