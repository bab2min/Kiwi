#include "../SkipBigramModelImpl.hpp"
#include "../gemm.h"

#include <Eigen/Dense>

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::none, uint8_t, 8>;
		template class SkipBigramModel<ArchType::none, uint16_t, 8>;
		template class SkipBigramModel<ArchType::none, uint32_t, 8>;
		template class SkipBigramModel<ArchType::none, uint64_t, 8>;

		template float logSumExp<ArchType::none>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::none>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::none>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::none>(float* arr, size_t size, size_t batchSize, size_t stride);

		template class SkipBigramModel<ArchType::balanced, uint8_t, 8>;
		template class SkipBigramModel<ArchType::balanced, uint16_t, 8>;
		template class SkipBigramModel<ArchType::balanced, uint32_t, 8>;
		template class SkipBigramModel<ArchType::balanced, uint64_t, 8>;

		template float logSumExp<ArchType::balanced>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::balanced>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::balanced>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::balanced>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::none>(
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
		void gemv<ArchType::none>(
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
	}
}
