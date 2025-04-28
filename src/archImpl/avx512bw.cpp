#include "../MathFunc.hpp"
#include "../qgemm.hpp"

#include "avx512_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::avx512bw>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::avx512bw>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::avx512bw>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::avx512bw>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::avx512bw>(const uint8_t* a, const int8_t* b, size_t n);

		template<>
		inline void scatteredGEMV<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return detail::scatteredGEMV_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return detail::scatteredGEMV8x1_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detail::scatteredGEMV2_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV3<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detail::scatteredGEMV3_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV4<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return detail::scatteredGEMV4_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		struct ScatteredGEMMSmall<ArchType::avx512bw>
		{
			template<size_t m, size_t n>
			static void op(size_t, size_t, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c, size_t ldc)
			{
				return detail::scatteredGEMMSmall_512<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template void scatteredGEMMOpt<ArchType::avx512bw>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<>
		void gemv<ArchType::avx512bw>(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detail::gemv_512(m, k, a, b, ldb, c);
		}

		template<>
		void gemvS8S8<ArchType::avx512bw>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return detail::gemvS8S8_512(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::avx512bw>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return detail::gemvU8U8_512(m, k, a, b, ldb, c);
		}

		template<>
		float dotS8S8<ArchType::avx512bw>(size_t k, const int8_t* a, const int8_t* b)
		{
			return detail::dotS8S8_512(k, a, b);
		}

		template<>
		float dotU8U8<ArchType::avx512bw>(size_t k, const uint8_t* a, const uint8_t* b)
		{
			return detail::dotU8U8_512(k, a, b);
		}

		template<>
		void invNormS8<ArchType::avx512bw>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return detail::invNormS8_512(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return detail::invNormU8_512(m, k, a, lda, out);
		}

		template<>
		float requantizePackedU4<ArchType::avx512bw>(
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
}

#define Eigen EigenAVX512
#define ARCH_TYPE ArchType::avx512bw
#include "eigen_gemm.hpp"
