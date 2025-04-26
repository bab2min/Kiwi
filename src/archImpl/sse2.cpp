#include "../MathFunc.hpp"
#include "../qgemm.h"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::sse2>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::sse2>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::sse2>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::sse2>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template<>
		float requantizePackedU4<ArchType::sse2>(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		)
		{
			return requantizePackedU4<ArchType::none>(n, qgroup, packedInput, localScale, globalScale, toUint8, out);
		}
	}
}

#define Eigen EigenSSE2
#define ARCH_TYPE ArchType::sse2
#include "eigen_gemm.hpp"
