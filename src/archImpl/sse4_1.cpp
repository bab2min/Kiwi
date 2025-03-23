#include "../MathFunc.hpp"
#include "../qgemm.hpp"
#include "../gemm.h"

#define Eigen EigenSSE41
#include <Eigen/Dense>

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
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::sse4_1>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC
		)
		{
			Eigen::Map<const Eigen::MatrixXf, 0, Eigen::OuterStride<>> aMap(aT, k, m, Eigen::OuterStride<>(strideA));
			Eigen::Map<const Eigen::MatrixXf, 0, Eigen::OuterStride<>> bMap(b, k, n, Eigen::OuterStride<>(strideB));
			Eigen::Map<Eigen::MatrixXf, 0, Eigen::OuterStride<>> cMap(c, m, n, Eigen::OuterStride<>(strideC));
			cMap.noalias() += aMap.transpose() * bMap;
		}


		template<>
		void gemv<ArchType::sse4_1>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c
		)
		{
			Eigen::Map<const Eigen::MatrixXf, 0, Eigen::OuterStride<>> aMap(aT, k, m, Eigen::OuterStride<>(strideA));
			Eigen::Map<const Eigen::VectorXf> bMap(b, k);
			Eigen::Map<Eigen::VectorXf> cMap(c, m);
			cMap.noalias() += aMap.transpose() * bMap;
		}
	}
}
