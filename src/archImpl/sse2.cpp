#include "../MathFunc.hpp"
#include "../gemm.h"

#define Eigen EigenSSE2
#include <Eigen/Dense>

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::sse2>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::sse2>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::sse2>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::sse2>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::sse2>(
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
		void gemv<ArchType::sse2>(
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

