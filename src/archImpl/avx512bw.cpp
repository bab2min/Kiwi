#include "../MathFunc.hpp"
#include "../qgemm.hpp"

namespace kiwi
{
	namespace qgemm
	{
		// emulate _mm512_dpbusd_epi32 using AVX512BW
		static FORCE_INLINE __m512i dpbusd(__m512i src, __m512i a, __m512i b)
		{
			__m512i one16 = _mm512_set1_epi16(1);
			__m512i t0 = _mm512_maddubs_epi16(a, b);
			__m512i t1 = _mm512_madd_epi16(t0, one16);
			return _mm512_add_epi32(src, t1);
		}
	}
}

#define DPBUSD dpbusd
#include "avx512_qgemm.hpp"

namespace kiwi
{
	namespace lm
	{
		template<>
		float logSumExp<ArchType::avx512bw>(const float* arr, size_t size)
		{
			if (size == 8) return LogSumExp<ArchType::avx2>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSumExp<ArchType::avx512bw>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

		template<>
		void logSoftmax<ArchType::avx512bw>(float* arr, size_t size)
		{
			if (size == 8) return LogSoftmax<ArchType::avx2>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSoftmax<ArchType::avx512bw>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

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
			return scatteredGEMV_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV8x1<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			return scatteredGEMV8x1_512(m, k, aBase, aIdx, aIdxScale, b, c);
		}

		template<>
		inline void scatteredGEMV2<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV2_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV3<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV3_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
		}

		template<>
		inline void scatteredGEMV4<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMV4_512(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
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
				return scatteredGEMMSmall_512<m, n>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
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
			return gemv_512(m, k, a, b, ldb, c);
		}

		template<>
		void gemvS8S8<ArchType::avx512bw>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return gemvS8S8_512(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::avx512bw>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return gemvU8U8_512(m, k, a, b, ldb, c);
		}

		template<>
		float dotS8S8<ArchType::avx512bw>(size_t k, const int8_t* a, const int8_t* b)
		{
			return dotS8S8_512(k, a, b);
		}

		template<>
		float dotU8U8<ArchType::avx512bw>(size_t k, const uint8_t* a, const uint8_t* b)
		{
			return dotU8U8_512(k, a, b);
		}

		template<>
		void invNormS8<ArchType::avx512bw>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			return invNormS8_512(m, k, a, lda, out);
		}

		template<>
		void invNormU8<ArchType::avx512bw>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			return invNormU8_512(m, k, a, lda, out);
		}
	}
}

#define Eigen EigenAVX512
#define ARCH_TYPE ArchType::avx512bw
#include "eigen_gemm.hpp"
