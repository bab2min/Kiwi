#include "../MathFunc.hpp"
#include "../qgemm.hpp"
#include "../gemm.h"

#define DPBUSD _mm512_dpbusd_epi32
#include "avx512_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template<>
		float logSumExp<ArchType::avx512vnni>(const float* arr, size_t size)
		{
			if (size == 8) return LogSumExp<ArchType::avx2>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSumExp<ArchType::avx512vnni>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

		template<>
		void logSoftmax<ArchType::avx512vnni>(float* arr, size_t size)
		{
			if (size == 8) return LogSoftmax<ArchType::avx2>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSoftmax<ArchType::avx512vnni>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

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
			return scatteredGEMV_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV8x1_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV2_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV3<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV3_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV4<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV4_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
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
				return scatteredGEMMSmall_512<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template void scatteredGEMMOpt<ArchType::avx512vnni>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<>
		void gemvS8S8<ArchType::avx512vnni>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return gemvS8S8_512(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::avx512vnni>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return gemvU8U8_512(m, k, a, b, ldb, c);
		}

		template<>
		void invNormS8<ArchType::avx512vnni>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return invNormS8_512(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::avx512vnni>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return invNormU8_512(m, k, a, lda, out);
		}
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::avx512vnni>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC
		)
		{
			return gemm<ArchType::avx512bw>(m, n, k, aT, strideA, b, strideB, c, strideC);
		}

		template<>
		void gemv<ArchType::avx512vnni>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c
		)
		{
			return gemv<ArchType::avx512bw>(m, k, aT, strideA, b, c);
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
