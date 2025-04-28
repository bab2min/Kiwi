#include "../MathFunc.hpp"
#include "../qgemm.hpp"
#include "../gemm.h"

#define USE_VNNI
#include "avx2_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
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
			return detailVnni::scatteredGEMV_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx_vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return detailVnni::scatteredGEMV8x1_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx_vnni>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detailVnni::scatteredGEMV2_256(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
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
				return detailVnni::scatteredGEMMSmall_256<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template void scatteredGEMMOpt<ArchType::avx_vnni>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<>
		void gemv<ArchType::avx_vnni>(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detailVnni::gemv_256(m, k, a, b, ldb, c);
		}

		template<>
		void gemvS8S8<ArchType::avx_vnni>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detailVnni::gemvS8S8_256(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::avx_vnni>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return detailVnni::gemvU8U8_256(m, k, a, b, ldb, c);
		}

		template<>
		float dotS8S8<ArchType::avx_vnni>(size_t k, const int8_t* a, const int8_t* b)
		{
			return detailVnni::dotS8S8_256(k, a, b);
		}

		template<>
		float dotU8U8<ArchType::avx_vnni>(size_t k, const uint8_t* a, const uint8_t* b)
		{
			return detailVnni::dotU8U8_256(k, a, b);
		}

		template<>
		void invNormS8<ArchType::avx_vnni>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return detailVnni::invNormS8_256(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::avx_vnni>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return detailVnni::invNormU8_256(m, k, a, lda, out);
		}

		template<>
		float requantizePackedU4<ArchType::avx_vnni>(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		)
		{
			return requantizePackedU4<ArchType::avx2>(n, qgroup, packedInput, localScale, globalScale, toUint8, out);
		}
	}

	namespace gemm
	{
		template<>
		void gemm<ArchType::avx_vnni>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC,
			bool zeroMode
		)
		{
			return gemm<ArchType::avx2>(m, n, k, aT, strideA, b, strideB, c, strideC, zeroMode);
		}

		template<>
		void gemv<ArchType::avx_vnni>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c,
			bool zeroMode
		)
		{
			return gemv<ArchType::avx2>(m, k, aT, strideA, b, c, zeroMode);
		}

		template<>
		void mul<ArchType::avx_vnni>(
			size_t n,
			float a,
			const float* b,
			float* c
		)
		{
			return mul<ArchType::avx2>(n, a, b, c);
		}

		template<>
		void invNorm<ArchType::avx_vnni>(
			size_t m, size_t k,
			const float* a, size_t lda,
			float* out
		)
		{
			return invNorm<ArchType::avx2>(m, k, a, lda, out);
		}
	}
}
