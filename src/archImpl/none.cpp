#include "../MathFunc.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::none>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::none>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::none>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::none>(float* arr, size_t size, size_t batchSize, size_t stride);

		template float logSumExp<ArchType::balanced>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::balanced>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::balanced>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::balanced>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

}

#define ARCH_TYPE ArchType::none
#include "eigen_gemm.hpp"

namespace kiwi
{
	namespace gemm
	{
		template<>
		void gemm<ArchType::balanced>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC
		)
		{
			return gemm<ArchType::none>(m, n, k, aT, strideA, b, strideB, c, strideC);
		}

		template<>
		void gemv<ArchType::balanced>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c
		)
		{
			return gemv<ArchType::none>(m, k, aT, strideA, b, c);
		}

		template<>
		void mul<ArchType::balanced>(
			size_t n,
			float a,
			const float* b,
			float* c
		)
		{
			return mul<ArchType::none>(n, a, b, c);
		}

		template<>
		void invNorm<ArchType::balanced>(
			size_t m, size_t k,
			const float* a, size_t lda,
			float* out
		)
		{
			return invNorm<ArchType::none>(m, k, a, lda, out);
		}
	}
}
