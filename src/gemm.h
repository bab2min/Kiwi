#pragma once
#include <kiwi/ArchUtils.h>

namespace kiwi
{
	namespace gemm
	{
		// c += a.transpose() * b
		template<ArchType archType>
		void gemm(size_t m, size_t n, size_t k, 
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC, 
			bool zeroMode = false
		);

		// c += a.transpose() * b
		template<ArchType archType>
		void gemv(size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c,
			bool zeroMode = false
		);

		// c *= (b * c)
		template<ArchType archType>
		void mul(
			size_t n,
			float a,
			const float* b,
			float* c
		);

		// out = 1 / |a|
		template<ArchType archType>
		void invNorm(
			size_t m, size_t k,
			const float* a, size_t lda,
			float* out
		);
	}
}
