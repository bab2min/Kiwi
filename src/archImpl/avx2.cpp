#include "../MathFunc.hpp"
#include "../qgemm.hpp"

namespace kiwi
{
	namespace qgemm
	{
		// emulate _mm256_dpbusd_epi32 using AVX2
		static FORCE_INLINE __m256i dpbusd(__m256i src, __m256i a, __m256i b)
		{
			__m256i one16 = _mm256_set1_epi16(1);
			__m256i t0 = _mm256_maddubs_epi16(a, b);
			__m256i t1 = _mm256_madd_epi16(t0, one16);
			return _mm256_add_epi32(src, t1);
		}
	}
}

#define DPBUSD dpbusd
#include "avx2_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::avx2>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::avx2>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::avx2>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::avx2>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::avx2>(const uint8_t* a, const int8_t* b, size_t n);

		template<>
		inline void scatteredGEMV<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV8x1_256(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV2_256(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		struct ScatteredGEMMSmall<ArchType::avx2>
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

		template void scatteredGEMMOpt<ArchType::avx2>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<>
		void gemv<ArchType::avx2>(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return gemv_256(m, k, a, b, ldb, c);
		}

		template<>
		void gemvS8S8<ArchType::avx2>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return gemvS8S8_256(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::avx2>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return gemvU8U8_256(m, k, a, b, ldb, c);
		}

		template<>
		float dotS8S8<ArchType::avx2>(size_t k, const int8_t* a, const int8_t* b)
		{
			return dotS8S8_256(k, a, b);
		}

		template<>
		float dotU8U8<ArchType::avx2>(size_t k, const uint8_t* a, const uint8_t* b)
		{
			return dotU8U8_256(k, a, b);
		}

		template<>
		void invNormS8<ArchType::avx2>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return invNormS8_256(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::avx2>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return invNormU8_256(m, k, a, lda, out);
		}
	}
}

#define Eigen EigenAVX2
#define ARCH_TYPE ArchType::avx2
#include "eigen_gemm.hpp"
