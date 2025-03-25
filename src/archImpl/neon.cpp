#include "../MathFunc.hpp"
#include "../qgemm.hpp"
#include "../gemm.h"

#define Eigen EigenNEON
#include <Eigen/Dense>

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
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::neon>(
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
		void gemv<ArchType::neon>(
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
