#include <cstring>
#include <utility>
#include <immintrin.h>
#include <kiwi/Types.h>
#include "qgemm.h"
#include "SIMD.hpp"

namespace kiwi
{
	namespace qgemm
	{
		template<ArchType archType>
		int32_t dotprod(
			const uint8_t* a, const int8_t* b, size_t n
		)
		{
			simd::Operator<archType> op;
			return op.dotprod(a, b, n);
		}
		template int32_t dotprod<ArchType::sse4_1>(const uint8_t* a, const int8_t* b, size_t n);
		template int32_t dotprod<ArchType::avx2>(const uint8_t* a, const int8_t* b, size_t n);
		template int32_t dotprod<ArchType::avx_vnni>(const uint8_t* a, const int8_t* b, size_t n);
		template int32_t dotprod<ArchType::avx512bw>(const uint8_t* a, const int8_t* b, size_t n);
		template int32_t dotprod<ArchType::avx512vnni>(const uint8_t* a, const int8_t* b, size_t n);

		inline void packScatteredAPanel(uint8_t* out, size_t ld, const uint8_t* base, const int32_t* idx, size_t scale, size_t m, size_t k)
		{
			for (size_t i = 0; i < m; ++i)
			{
				const uint8_t* src = base + idx[i] * scale;
				memcpy(out + i * ld, src, k);
			}
		}

		template<size_t blockSize = 16>
		inline void packScatteredBPanel(int8_t* out, size_t ld, int32_t* sum,
			const int8_t* base, const int32_t* sumBase, const int32_t* idx,
			size_t scale, size_t sumScale, size_t n, size_t k)
		{
			int32_t* pout = reinterpret_cast<int32_t*>(out);
			
			for (size_t i = 0; i < n; i += blockSize)
			{
				const size_t innerN = std::min(blockSize, n - i);
				for (size_t j = 0; j < k; j += 4)
				{
					for (size_t x = 0; x < innerN; ++x)
					{
						const int8_t* src = base + idx[i + x] * scale;
						*pout++ = *reinterpret_cast<const int32_t*>(&src[j]);
					}
					pout += (blockSize - innerN);
				}

				for (size_t x = 0; x < innerN; ++x)
				{
					sum[i + x] = sumBase[idx[i + x] * sumScale];
				}
			}
		}

		template<size_t blockNSize = 16, size_t packK = 384>
		inline void qgemmKernel(
			size_t m, size_t n, size_t k,
			const uint8_t* a, const int8_t* b,
			const float* aScale, const float* bScale,
			const float* aBias, const int32_t* sumBuffer, 
			float* out, size_t ld)
		{
			// quantized sub-block gemm(m=4, n=64)
			static constexpr size_t blockNStride = blockNSize * 4;
			__m512i pa, pb[4], psum[16];
			__m512 paScale, paBias, pbScale[4], r;

			for (size_t i = 0; i < n; n += blockNSize * 4)
			{
				psum[0] = psum[4] = psum[8] = psum[12] = _mm512_loadu_si512(sumBuffer);
				psum[1] = psum[5] = psum[9] = psum[13] = _mm512_loadu_si512(sumBuffer + blockNSize);
				psum[2] = psum[6] = psum[10] = psum[14] = _mm512_loadu_si512(sumBuffer + blockNSize * 2);
				psum[3] = psum[7] = psum[11] = psum[15] = _mm512_loadu_si512(sumBuffer + blockNSize * 3);
				
				for (size_t j = 0; j < k; j += 4)
				{
					pb[0] = _mm512_loadu_si512(b);
					pb[1] = _mm512_loadu_si512(b + blockNStride * 1);
					pb[2] = _mm512_loadu_si512(b + blockNStride * 2);
					pb[3] = _mm512_loadu_si512(b + blockNStride * 3);

					pa = _mm512_set1_epi32(*reinterpret_cast<const int32_t*>(a));
					psum[0] = _mm512_dpbusd_epi32(psum[0], pa, pb[0]);
					psum[1] = _mm512_dpbusd_epi32(psum[1], pa, pb[1]);
					psum[2] = _mm512_dpbusd_epi32(psum[2], pa, pb[2]);
					psum[3] = _mm512_dpbusd_epi32(psum[3], pa, pb[3]);

					pa = _mm512_set1_epi32(*reinterpret_cast<const int32_t*>(a + k));
					psum[4] = _mm512_dpbusd_epi32(psum[4], pa, pb[0]);
					psum[5] = _mm512_dpbusd_epi32(psum[5], pa, pb[1]);
					psum[6] = _mm512_dpbusd_epi32(psum[6], pa, pb[2]);
					psum[7] = _mm512_dpbusd_epi32(psum[7], pa, pb[3]);

					pa = _mm512_set1_epi32(*reinterpret_cast<const int32_t*>(a + k * 2));
					psum[8] = _mm512_dpbusd_epi32(psum[8], pa, pb[0]);
					psum[9] = _mm512_dpbusd_epi32(psum[9], pa, pb[1]);
					psum[10] = _mm512_dpbusd_epi32(psum[10], pa, pb[2]);
					psum[11] = _mm512_dpbusd_epi32(psum[11], pa, pb[3]);

					pa = _mm512_set1_epi32(*reinterpret_cast<const int32_t*>(a + k * 3));
					psum[12] = _mm512_dpbusd_epi32(psum[12], pa, pb[0]);
					psum[13] = _mm512_dpbusd_epi32(psum[13], pa, pb[1]);
					psum[14] = _mm512_dpbusd_epi32(psum[14], pa, pb[2]);
					psum[15] = _mm512_dpbusd_epi32(psum[15], pa, pb[3]);

					a += 4;
					b += blockNStride * 4;
				}
				pbScale[0] = _mm512_loadu_ps(bScale);
				pbScale[1] = _mm512_loadu_ps(bScale + blockNSize);
				pbScale[2] = _mm512_loadu_ps(bScale + blockNSize * 2);
				pbScale[3] = _mm512_loadu_ps(bScale + blockNSize * 3);
				
				paScale = _mm512_set1_ps(*aScale++);
				paBias = _mm512_set1_ps(*aBias++);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[0]), pbScale[0]), paScale, paBias);
				_mm512_storeu_ps(out, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[1]), pbScale[1]), paScale, paBias);
				_mm512_storeu_ps(out + blockNSize, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[2]), pbScale[2]), paScale, paBias);
				_mm512_storeu_ps(out + blockNSize * 2, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[3]), pbScale[3]), paScale, paBias);
				_mm512_storeu_ps(out + blockNSize * 3, r);

				paScale = _mm512_set1_ps(*aScale++);
				paBias = _mm512_set1_ps(*aBias++);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[4]), pbScale[0]), paScale, paBias);
				_mm512_storeu_ps(out + ld, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[5]), pbScale[1]), paScale, paBias);
				_mm512_storeu_ps(out + ld + blockNSize, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[6]), pbScale[2]), paScale, paBias);
				_mm512_storeu_ps(out + ld + blockNSize * 2, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[7]), pbScale[3]), paScale, paBias);
				_mm512_storeu_ps(out + ld + blockNSize * 3, r);

				paScale = _mm512_set1_ps(*aScale++);
				paBias = _mm512_set1_ps(*aBias++);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[8]), pbScale[0]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 2, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[9]), pbScale[1]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 2 + blockNSize, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[10]), pbScale[2]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 2 + blockNSize * 2, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[11]), pbScale[3]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 2 + blockNSize * 3, r);

				paScale = _mm512_set1_ps(*aScale++);
				paBias = _mm512_set1_ps(*aBias++);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[12]), pbScale[0]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 3, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[13]), pbScale[1]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 3 + blockNSize, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[14]), pbScale[2]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 3 + blockNSize * 2, r);
				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[15]), pbScale[3]), paScale, paBias);
				_mm512_storeu_ps(out + ld * 3 + blockNSize * 3, r);
				sumBuffer += blockNSize * 4;
				out += blockNSize * 4;
			}
		}

		template<ArchType archType>
		void scatteredGEMM(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		)
		{
			// assert k <= 384
			constexpr size_t packM = 48, packN = 256, packK = 384;
			thread_local uint8_t buffer[packM * packK + packN * packK];
			thread_local int32_t sumBuffer[packN];
			uint8_t* aBuffer = buffer;
			int8_t* bBuffer = reinterpret_cast<int8_t*>(buffer + packM * packK);

			for (size_t ni = 0; ni < n; ni += packN)
			{
				const size_t microN = std::min(packN, n - ni);
				packScatteredBPanel(bBuffer, packK, sumBuffer, bBase, reinterpret_cast<const int32_t*>(bBase + k + 4), bIdx + ni, bIdxScale, bIdxScale / 4, microN, k);

				for (size_t mi = 0; mi < m; mi += packM)
				{
					const size_t microM = std::min(packM, m - mi);
					packScatteredAPanel(aBuffer, packK, aBase, aIdx + mi, aIdxScale, microM, k);

					//qgemmKernel<16>(microM, microN, k, aBuffer, bBuffer, sumBuffer, nullptr, n);
				}
			}
		}

		template void scatteredGEMM<ArchType::avx512bw>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<ArchType archType>
		void scatteredGEMMBaseline(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		)
		{
			thread_local Vector<uint8_t> buffer;
			buffer.resize((m + n) * (k + 8));
			uint8_t* aBuffer = buffer.data();
			int8_t* bBuffer = reinterpret_cast<int8_t*>(aBuffer + m * (k + 8));
			simd::Operator<archType> op;
			
			for (size_t i = 0; i < m; ++i)
			{
				std::memcpy(aBuffer + i * (k + 8), &aBase[aIdx[i] * aIdxScale], k + 8);
			}
			for (size_t i = 0; i < n; ++i)
			{
				std::memcpy(bBuffer + i * (k + 8), &bBase[bIdx[i] * bIdxScale], k + 8);
			}

			for (size_t i = 0; i < m; ++i)
			{
				for (size_t j = 0; j < n; ++j)
				{
					const auto* aPtr = aBuffer + i * (k + 8);
					const auto* bPtr = bBuffer + j * (k + 8);
					int32_t acc = op.dotprod(aPtr, bPtr, k);
					const float contextScale = *reinterpret_cast<const float*>(aPtr + k),
						outputScale = *reinterpret_cast<const float*>(bPtr + k),
						contextBias = *reinterpret_cast<const float*>(aPtr + k + 4);
					const int32_t hsum = *reinterpret_cast<const int32_t*>(bPtr + k + 4);
					c[i * ldc + j] = (acc - hsum) * contextScale * outputScale + contextBias;
				}
			}
		}

		inline void pack16x4(
			void* out,
			const void* a0,
			const void* a1,
			const void* a2,
			const void* a3,
			const void* a4,
			const void* a5,
			const void* a6,
			const void* a7,
			const void* a8,
			const void* a9,
			const void* a10,
			const void* a11,
			const void* a12,
			const void* a13,
			const void* a14,
			const void* a15
		)
		{
			// 00, 01, 02, 03, 40, 41, 42, 43
			auto p0 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a0)), _mm_loadu_epi32(a4), 1);
			auto p1 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a1)), _mm_loadu_epi32(a5), 1);
			auto p2 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a2)), _mm_loadu_epi32(a6), 1);
			auto p3 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a3)), _mm_loadu_epi32(a7), 1);
			auto p4 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a8)), _mm_loadu_epi32(a12), 1);
			auto p5 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a9)), _mm_loadu_epi32(a13), 1);
			auto p6 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a10)), _mm_loadu_epi32(a14), 1);
			auto p7 = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_epi32(a11)), _mm_loadu_epi32(a15), 1);

			// 00, 10, 01, 11, 40, 50, 41, 51
			auto q0 = _mm256_unpacklo_epi32(p0, p1);
			// 02, 12, 03, 13, 42, 52, 43, 53
			auto q1 = _mm256_unpackhi_epi32(p0, p1);
			// 20, 30, 21, 31, 60, 70, 61, 71
			auto q2 = _mm256_unpacklo_epi32(p2, p3);
			// 22, 32, 23, 33, 62, 72, 63, 73
			auto q3 = _mm256_unpackhi_epi32(p2, p3);
			auto q4 = _mm256_unpacklo_epi32(p4, p5);
			auto q5 = _mm256_unpackhi_epi32(p4, p5);
			auto q6 = _mm256_unpacklo_epi32(p6, p7);
			auto q7 = _mm256_unpackhi_epi32(p6, p7);

			// 00, 10, 20, 30, 40, 50, 60, 70
			p0 = _mm256_unpacklo_epi64(q0, q2);
			// 01, 11, 21, 31, 41, 51, 61, 71
			p1 = _mm256_unpackhi_epi64(q0, q2);
			p2 = _mm256_unpacklo_epi64(q1, q3);
			p3 = _mm256_unpackhi_epi64(q1, q3);
			p4 = _mm256_unpacklo_epi64(q4, q6);
			p5 = _mm256_unpackhi_epi64(q4, q6);
			p6 = _mm256_unpacklo_epi64(q5, q7);
			p7 = _mm256_unpackhi_epi64(q5, q7);

			auto* pout = reinterpret_cast<__m256i*>(out);
			_mm256_storeu_si256(pout++, p0);
			_mm256_storeu_si256(pout++, p4);
			_mm256_storeu_si256(pout++, p1);
			_mm256_storeu_si256(pout++, p5);
			_mm256_storeu_si256(pout++, p2);
			_mm256_storeu_si256(pout++, p6);
			_mm256_storeu_si256(pout++, p3);
			_mm256_storeu_si256(pout++, p7);
		}

		inline void packScatteredGEMVAPanel(uint8_t* out, size_t ld, 
			float* scale, float* bias,
			const uint8_t* base, const int32_t* idx,
			size_t idxScale, size_t m, size_t k)
		{
			static constexpr size_t blockSize = 16;
			int32_t* pout = reinterpret_cast<int32_t*>(out);

			for (size_t i = 0; i < m; i += blockSize)
			{
				const size_t innerM = std::min(blockSize, m - i);
				size_t j;
				for (j = 0; j < (k & ~15); j += 16)
				{
					pack16x4(pout, 
						base + idx[i] * idxScale + j, 
						base + (1 < innerM ? idx[i + 1] * idxScale + j : 0), 
						base + (2 < innerM ? idx[i + 2] * idxScale + j : 0),
						base + (3 < innerM ? idx[i + 3] * idxScale + j : 0),
						base + (4 < innerM ? idx[i + 4] * idxScale + j : 0),
						base + (5 < innerM ? idx[i + 5] * idxScale + j : 0),
						base + (6 < innerM ? idx[i + 6] * idxScale + j : 0),
						base + (7 < innerM ? idx[i + 7] * idxScale + j : 0),
						base + (8 < innerM ? idx[i + 8] * idxScale + j : 0),
						base + (9 < innerM ? idx[i + 9] * idxScale + j : 0),
						base + (10 < innerM ? idx[i + 10] * idxScale + j : 0),
						base + (11 < innerM ? idx[i + 11] * idxScale + j : 0),
						base + (12 < innerM ? idx[i + 12] * idxScale + j : 0),
						base + (13 < innerM ? idx[i + 13] * idxScale + j : 0),
						base + (14 < innerM ? idx[i + 14] * idxScale + j : 0),
						base + (15 < innerM ? idx[i + 15] * idxScale + j : 0)
					);
					pout += 64;
				}
				
				for (; j < k; j += 4)
				{
					for (size_t x = 0; x < innerM; ++x)
					{
						const uint8_t* src = base + idx[i + x] * idxScale;
						*pout++ = *reinterpret_cast<const int32_t*>(&src[j]);
					}
					pout += (blockSize - innerM);
				}

				for (size_t x = 0; x < innerM; ++x)
				{
					scale[i + x] = *reinterpret_cast<const float*>(&base[idx[i + x] * idxScale + k]);
					bias[i + x] = *reinterpret_cast<const float*>(&base[idx[i + x] * idxScale + k + 4]);
				}
			}
		}

		inline void qgemvKernel16(size_t m, size_t k,
			const uint8_t* a, const int8_t* b,
			const float* aScale, float bScale,
			const float* aBias, int32_t bSum,
			float* c)
		{
			__m512i pa, pb, psum, pbSum;
			__m512 paScale, paBias, pbScale, r;
			pbScale = _mm512_set1_ps(bScale);
			pbSum = _mm512_set1_epi32(-bSum);
			
			for (size_t j = 0; j < m; j += 16)
			{
				psum = pbSum;
				for (size_t i = 0; i < k; i += 4)
				{
					pa = _mm512_loadu_si512(a);

					pb = _mm512_set1_epi32(*reinterpret_cast<const int32_t*>(b + i));
					psum = _mm512_dpbusd_epi32(psum, pa, pb);

					a += 64;
				}
				paScale = _mm512_loadu_ps(aScale);
				paBias = _mm512_loadu_ps(aBias);

				r = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum), pbScale), paScale, paBias);
				_mm512_storeu_ps(c, r);
				aScale += 16;
				aBias += 16;
				c += 16;
			}
		}

		void scatteredGEMV(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			constexpr size_t packM = 128, packN = 1, packK = 384;
			thread_local uint8_t buffer[packM * packK + packN * packK + packM * 2 * sizeof(float)];
			thread_local uint8_t optABuffer[packM * packK];
			uint8_t* aBuffer = buffer;
			int8_t* bBuffer = reinterpret_cast<int8_t*>(aBuffer + packM * packK);
			float* aScale = reinterpret_cast<float*>(bBuffer + packN * packK);
			float* aBias = aScale + packM;
			memcpy(bBuffer, b, k);
			float bScale = *reinterpret_cast<const float*>(b + k);
			int32_t bSum = *reinterpret_cast<const int32_t*>(b + k + 4);
			for (size_t mi = 0; mi < m; mi += packM)
			{
				const size_t microM = std::min(packM, m - mi);
				packScatteredGEMVAPanel(aBuffer, packK, aScale, aBias, aBase, aIdx, aIdxScale, microM, k);
				qgemvKernel16(microM, k, aBuffer, bBuffer, aScale, bScale, aBias, bSum, c);
				aIdx += microM;
				c += microM;
			}
		}

		inline void qgemv2Kernel16(size_t m, size_t k,
			const uint8_t* a, const int8_t* b, size_t ldb,
			const float* aScale, float bScale[2],
			const float* aBias, int32_t bSum[2],
			float* c)
		{
			__m512i pa, pb[2], psum[2], pbSum[2];
			__m512 paScale, paBias, pbScale[2], r[2], t[2];
			pbScale[0] = _mm512_set1_ps(bScale[0]);
			pbScale[1] = _mm512_set1_ps(bScale[1]);
			pbSum[0] = _mm512_set1_epi32(-bSum[0]);
			pbSum[1] = _mm512_set1_epi32(-bSum[1]);

			for (size_t j = 0; j < m; j += 16)
			{
				psum[0] = pbSum[0];
				psum[1] = pbSum[1];
				for (size_t i = 0; i < k; i += 4)
				{
					pa = _mm512_loadu_si512(a);

					pb[0] = _mm512_set1_epi32(*reinterpret_cast<const int32_t*>(b + i));
					pb[1] = _mm512_set1_epi32(*reinterpret_cast<const int32_t*>(b + ldb + i));
					psum[0] = _mm512_dpbusd_epi32(psum[0], pa, pb[0]);
					psum[1] = _mm512_dpbusd_epi32(psum[1], pa, pb[1]);

					a += 64;
				}
				paScale = _mm512_loadu_ps(aScale);
				paBias = _mm512_loadu_ps(aBias);

				// 00, 10, 20, 30, 40, 50, 60, 70
				r[0] = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[0]), pbScale[0]), paScale, paBias);
				r[1] = _mm512_fmadd_ps(_mm512_mul_ps(_mm512_cvtepi32_ps(psum[1]), pbScale[1]), paScale, paBias);
				
				// 00, 01, 10, 11, 40, 41, 50, 51
				t[0] = _mm512_unpacklo_ps(r[0], r[1]);
				// 20, 21, 30, 31, 60, 61, 70, 71
				t[1] = _mm512_unpackhi_ps(r[0], r[1]);
				
				_mm_storeu_ps(c, _mm512_extractf32x4_ps(t[0], 0));
				_mm_storeu_ps(c + 4, _mm512_extractf32x4_ps(t[1], 0));
				_mm_storeu_ps(c + 8, _mm512_extractf32x4_ps(t[0], 1));
				_mm_storeu_ps(c + 12, _mm512_extractf32x4_ps(t[1], 1));
				_mm_storeu_ps(c + 16, _mm512_extractf32x4_ps(t[0], 2));
				_mm_storeu_ps(c + 20, _mm512_extractf32x4_ps(t[1], 2));
				_mm_storeu_ps(c + 24, _mm512_extractf32x4_ps(t[0], 3));
				_mm_storeu_ps(c + 28, _mm512_extractf32x4_ps(t[1], 3));

				aScale += 16;
				aBias += 16;
				c += 32;
			}
		}

		void scatteredGEMV2(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			constexpr size_t packM = 128, packN = 2, packK = 384;
			thread_local uint8_t buffer[packM * packK + packN * packK + packM * 2 * sizeof(float)];
			thread_local uint8_t optABuffer[packM * packK];
			uint8_t* aBuffer = buffer;
			int8_t* bBuffer = reinterpret_cast<int8_t*>(aBuffer + packM * packK);
			float* aScale = reinterpret_cast<float*>(bBuffer + packN * packK);
			float* aBias = aScale + packM;
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
			for (size_t mi = 0; mi < m; mi += packM)
			{
				const size_t microM = std::min(packM, m - mi);
				packScatteredGEMVAPanel(aBuffer, packK, aScale, aBias, aBase, aIdx, aIdxScale, microM, k);
				qgemv2Kernel16(microM, k, aBuffer, bBuffer, packK, aScale, bScale, aBias, bSum, c);
				aIdx += microM;
				c += microM * 2;
			}
		}

		template<ArchType archType, size_t m, size_t n>
		void scatteredGEMMSmall(
			size_t, size_t, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		)
		{
			static_assert(m <= 3, "m should be less than or equal to 3");
			static_assert(n <= 3, "n should be less than or equal to 3");
			__m512i pa[3], pb[3], psum[3][3];
			const uint8_t* aPtr[3];
			const int8_t* bPtr[3];

			psum[0][0] = _mm512_setzero_si512();
			if (m > 1) psum[1][0] = _mm512_setzero_si512();
			if (m > 2) psum[2][0] = _mm512_setzero_si512();
			if (n > 1) psum[0][1] = _mm512_setzero_si512();
			if (m > 1 && n > 1) psum[1][1] = _mm512_setzero_si512();
			if (m > 2 && n > 1) psum[2][1] = _mm512_setzero_si512();
			if (n > 2) psum[0][2] = _mm512_setzero_si512();
			if (m > 1 && n > 2) psum[1][2] = _mm512_setzero_si512();
			if (m > 2 && n > 2) psum[2][2] = _mm512_setzero_si512();

			aPtr[0] = aBase + aIdx[0] * aIdxScale;
			if (m > 1) aPtr[1] = aBase + aIdx[1] * aIdxScale;
			if (m > 2) aPtr[2] = aBase + aIdx[2] * aIdxScale;
			
			bPtr[0] = bBase + bIdx[0] * bIdxScale;
			if (n > 1) bPtr[1] = bBase + bIdx[1] * bIdxScale;
			if (n > 2) bPtr[2] = bBase + bIdx[2] * bIdxScale;

			for (size_t x = 0; x < k; x += 64)
			{
				if (m > 0)
				{
					pa[0] = _mm512_loadu_si512(aPtr[0]);
					aPtr[0] += 64;
				}
				if (m > 1)
				{
					pa[1] = _mm512_loadu_si512(aPtr[1]);
					aPtr[1] += 64;
				}
				if (m > 2)
				{
					pa[2] = _mm512_loadu_si512(aPtr[2]);
					aPtr[2] += 64;
				}

				if (n > 0)
				{
					pb[0] = _mm512_loadu_si512(bPtr[0]);
					bPtr[0] += 64;
				}
				if (n > 1)
				{
					pb[1] = _mm512_loadu_si512(bPtr[1]);
					bPtr[1] += 64;
				}
				if (n > 2)
				{
					pb[2] = _mm512_loadu_si512(bPtr[2]);
					bPtr[2] += 64;
				}

				psum[0][0] = _mm512_dpbusd_epi32(psum[0][0], pa[0], pb[0]);
				if (m > 1) psum[1][0] = _mm512_dpbusd_epi32(psum[1][0], pa[1], pb[0]);
				if (m > 2) psum[2][0] = _mm512_dpbusd_epi32(psum[2][0], pa[2], pb[0]);
				if (n > 1) psum[0][1] = _mm512_dpbusd_epi32(psum[0][1], pa[0], pb[1]);
				if (m > 1 && n > 1) psum[1][1] = _mm512_dpbusd_epi32(psum[1][1], pa[1], pb[1]);
				if (m > 2 && n > 1) psum[2][1] = _mm512_dpbusd_epi32(psum[2][1], pa[2], pb[1]);
				if (n > 2) psum[0][2] = _mm512_dpbusd_epi32(psum[0][2], pa[0], pb[2]);
				if (m > 1 && n > 2) psum[1][2] = _mm512_dpbusd_epi32(psum[1][2], pa[1], pb[2]);
				if (m > 2 && n > 2) psum[2][2] = _mm512_dpbusd_epi32(psum[2][2], pa[2], pb[2]);
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
				int32_t acc = _mm512_reduce_add_epi32(psum[0][0]);
				c[0] = (acc - hsum[0]) * contextScale[0] * outputScale[0] + contextBias[0];
			}
			if (m > 1)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[1][0]);
				c[ldc] = (acc - hsum[0]) * contextScale[1] * outputScale[0] + contextBias[1];
			}
			if (m > 2)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[2][0]);
				c[ldc * 2] = (acc - hsum[0]) * contextScale[2] * outputScale[0] + contextBias[2];
			}
			if (n > 1)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[0][1]);
				c[1] = (acc - hsum[1]) * contextScale[0] * outputScale[1] + contextBias[0];
			}
			if (m > 1 && n > 1)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[1][1]);
				c[ldc + 1] = (acc - hsum[1]) * contextScale[1] * outputScale[1] + contextBias[1];
			}
			if (m > 2 && n > 1)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[2][1]);
				c[ldc * 2 + 1] = (acc - hsum[1]) * contextScale[2] * outputScale[1] + contextBias[2];
			}
			if (n > 2)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[0][2]);
				c[2] = (acc - hsum[2]) * contextScale[0] * outputScale[2] + contextBias[0];
			}
			if (m > 1 && n > 2)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[1][2]);
				c[ldc + 2] = (acc - hsum[2]) * contextScale[1] * outputScale[2] + contextBias[1];
			}
			if (m > 2 && n > 2)
			{
				int32_t acc = _mm512_reduce_add_epi32(psum[2][2]);
				c[ldc * 2 + 2] = (acc - hsum[2]) * contextScale[2] * outputScale[2] + contextBias[2];
			}
		}

		template<ArchType archType>
		void scatteredGEMMOpt(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		)
		{
			using Fn = decltype(&scatteredGEMMBaseline<ArchType::none>);
			static constexpr Fn fnTable[] = {
				scatteredGEMMBaseline<archType>,
				scatteredGEMMSmall<archType, 1, 2>,
				scatteredGEMMSmall<archType, 1, 3>,
				scatteredGEMMSmall<archType, 2, 1>,
				scatteredGEMMSmall<archType, 2, 2>,
				scatteredGEMMSmall<archType, 2, 3>,
				scatteredGEMMSmall<archType, 3, 1>,
				scatteredGEMMSmall<archType, 3, 2>,
				scatteredGEMMSmall<archType, 3, 3>
			};

			if (m <= 3 && n <= 3)
			{
				return (*fnTable[(m - 1) * 3 + (n - 1)])(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}

			if (n == 1 && ldc == 1)
			{
				return scatteredGEMV(m, k, aBase, aIdx, aIdxScale, bBase + bIdx[0] * bIdxScale, c);
			}
			
			if (m >= 4 && n == 2 && ldc == 2)
			{
				return scatteredGEMV2(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
			}

			return scatteredGEMMBaseline<archType>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
		}


		template void scatteredGEMMOpt<ArchType::sse4_1>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
		template void scatteredGEMMOpt<ArchType::avx2>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
		template void scatteredGEMMOpt<ArchType::avx_vnni>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
		template void scatteredGEMMOpt<ArchType::avx512bw>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
		template void scatteredGEMMOpt<ArchType::avx512vnni>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
	}
}
