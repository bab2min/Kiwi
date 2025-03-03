#include "../SkipBigramModelImpl.hpp"
#include "../qgemm.hpp"
#include "../gemm.h"

#define Eigen EigenAVX512
#include <Eigen/Dense>

namespace kiwi
{
	namespace qgemm
	{
		// emulate _mm512_dpbusd_epi32 using AVX512BW
		static FORCE_INLINE __m512i dpbusd(__m512i src, __m512i a, __m512i b)
		{
			__m512i one16 = _mm512_set1_epi16(1);
			__m512i t0 = _mm512_maddubs_epi16(a, b);
			__m512i t1 = _mm512_madd_epi16(t0, one16);
			return _mm512_add_epi32(src, t1);
		}
	}
}

#define DPBUSD dpbusd
#include "avx512_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::avx512bw, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint64_t, 8>;

		template<>
		float logSumExp<ArchType::avx512bw>(const float* arr, size_t size)
		{
			if (size == 8) return LogSumExp<ArchType::avx2>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSumExp<ArchType::avx512bw>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

		template<>
		void logSoftmax<ArchType::avx512bw>(float* arr, size_t size)
		{
			if (size == 8) return LogSoftmax<ArchType::avx2>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSoftmax<ArchType::avx512bw>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

		template float logSumExp<ArchType::avx512bw>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::avx512bw>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::avx512bw>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::avx512bw>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::avx512bw>(const uint8_t* a, const int8_t* b, size_t n);

		template<>
		inline void scatteredGEMV<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV8x1_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV2_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV3<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV3_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV4<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV4_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		struct ScatteredGEMMSmall<ArchType::avx512bw>
		{
			template<size_t m, size_t n>
			static void op(size_t, size_t, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c, size_t ldc)
			{
				return scatteredGEMMSmall_512<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template void scatteredGEMMOpt<ArchType::avx512bw>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::avx512bw>(
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
		void gemv<ArchType::avx512bw>(
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
