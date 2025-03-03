#include "../SkipBigramModelImpl.hpp"
#include "../qgemm.hpp"
#include "../gemm.h"

#define DPBUSD _mm256_dpbusd_epi32
#include "avx2_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::avx_vnni, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx_vnni, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx_vnni, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx_vnni, uint64_t, 8>;

		template float logSumExp<ArchType::avx_vnni>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::avx_vnni>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::avx_vnni>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::avx_vnni>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::avx_vnni>(const uint8_t* a, const int8_t* b, size_t n);

		template<>
		inline void scatteredGEMV<ArchType::avx_vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx_vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV8x1_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx_vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV2_256(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		struct ScatteredGEMMSmall<ArchType::avx_vnni>
		{
			template<size_t m, size_t n>
			static void op(size_t, size_t, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c, size_t ldc)
			{
				return scatteredGEMMSmall_256<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template void scatteredGEMMOpt<ArchType::avx_vnni>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::avx_vnni>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC
		)
		{
			return gemm<ArchType::avx2>(m, n, k, aT, strideA, b, strideB, c, strideC);
		}

		template<>
		void gemv<ArchType::avx_vnni>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c
		)
		{
			return gemv<ArchType::avx2>(m, k, aT, strideA, b, c);
		}
	}
}
