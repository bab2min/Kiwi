#include "../MathFunc.hpp"
#include "../qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::neon>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::neon>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::neon>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::neon>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::neon>(const uint8_t* a, const int8_t* b, size_t n);

		template void scatteredGEMMOpt<ArchType::neon>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

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

#define Eigen EigenNeon
#define ARCH_TYPE ArchType::neon
#include "eigen_gemm.hpp"
