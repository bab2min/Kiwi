#pragma once
#include <Eigen/Dense>
#include "../gemm.h"

namespace kiwi
{
	namespace gemm
	{
		template<>
		void gemm<ARCH_TYPE>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC,
			bool zeroMode
		)
		{
			Eigen::Map<const Eigen::MatrixXf, 0, Eigen::OuterStride<>> aMap(aT, k, m, Eigen::OuterStride<>(strideA));
			Eigen::Map<const Eigen::MatrixXf, 0, Eigen::OuterStride<>> bMap(b, k, n, Eigen::OuterStride<>(strideB));
			Eigen::Map<Eigen::MatrixXf, 0, Eigen::OuterStride<>> cMap(c, m, n, Eigen::OuterStride<>(strideC));
			if (zeroMode) cMap.noalias() = aMap.transpose() * bMap;
			else cMap.noalias() += aMap.transpose() * bMap;
		}

		template<>
		void gemv<ARCH_TYPE>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c,
			bool zeroMode
		)
		{
			Eigen::Map<const Eigen::MatrixXf, 0, Eigen::OuterStride<>> aMap(aT, k, m, Eigen::OuterStride<>(strideA));
			Eigen::Map<const Eigen::VectorXf> bMap(b, k);
			Eigen::Map<Eigen::VectorXf> cMap(c, m);
			if (zeroMode) cMap.noalias() = aMap.transpose() * bMap;
			else cMap.noalias() += aMap.transpose() * bMap;
		}

		template<>
		void mul<ARCH_TYPE>(
			size_t n,
			float a,
			const float* b,
			float* c
		)
		{
			Eigen::Map<Eigen::ArrayXf> cMap(c, n);
			cMap *= a * Eigen::Map<const Eigen::ArrayXf>(b, n);
		}

		template<>
		void invNorm<ARCH_TYPE>(
			size_t m, size_t k,
			const float* a, size_t lda,
			float* out
		)
		{
			Eigen::Map<const Eigen::MatrixXf, 0, Eigen::OuterStride<>> aMap(a, k, m, Eigen::OuterStride<>(lda));
			Eigen::Map<Eigen::ArrayXf> outMap(out, m);
			outMap = aMap.colwise().norm().array().inverse();
		}
	}
}
