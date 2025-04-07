#include "../MathFunc.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::sse2>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::sse2>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::sse2>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::sse2>(float* arr, size_t size, size_t batchSize, size_t stride);
	}
}

#define Eigen EigenSSE2
#define ARCH_TYPE ArchType::sse2
#include "eigen_gemm.hpp"
