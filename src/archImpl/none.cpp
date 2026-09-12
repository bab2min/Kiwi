#include "../MathFunc.hpp"
#include "../qgemm.h"

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::none>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::none>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::none>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::none>(float* arr, size_t size, size_t batchSize, size_t stride);

		template float logSumExp<ArchType::balanced>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::balanced>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::balanced>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::balanced>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		// 아래 두 헬퍼는 x86 SIMD 구현(_mm_mulhrs_epi16 / _mm_sign_epi16)과 결과를 비트 단위로
		// 일치시키기 위한 것. 단순히 `v / 9`로 계산하면 곱수 절단(32768/9 = 3640.888... -> 3640)과
		// 반올림 방식(최근접 반올림 vs 0 방향 절단) 차이 때문에 +-1 만큼 어긋날 수 있음.
		static constexpr int16_t divideBy9 = 32768 / 9;

		// _mm_mulhrs_epi16 와 동일: round(a * b / 2^15)
		static FORCE_INLINE int16_t mulhrs(int16_t a, int16_t b)
		{
			return static_cast<int16_t>(((static_cast<int32_t>(a) * b >> 14) + 1) >> 1);
		}

		// _mm_sign_epi16(4, v) 와 동일: v가 0일 때는 바이어스를 더하지 않는다
		static FORCE_INLINE int16_t signBias(int16_t v)
		{
			return v > 0 ? 4 : (v < 0 ? -4 : 0);
		}

		template<>
		float requantizePackedU4<ArchType::none>(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		)
		{
			const int8_t zeropointBias = 6;
			const uint8_t scaleBias = 9;
			for (size_t i = 0; i < n / 2; ++i)
			{
				const uint8_t packed = packedInput[i];
				int16_t lower = packed & 0x0F;
				int16_t upper = (packed >> 4) & 0x0F;
				uint8_t scale = localScale[(i * 2) / qgroup];
				int16_t lzp = (scale >> 6) + zeropointBias;
				scale = (scale & 0x3F) + scaleBias;
				
				lower = (lower - lzp) * scale;
				lower += signBias(lower);
				lower = mulhrs(lower, divideBy9);
				upper = (upper - lzp) * scale;
				upper += signBias(upper);
				upper = mulhrs(upper, divideBy9);
				if (toUint8)
				{
					lower += 128;
					upper += 128;
				}
				out[i * 2] = static_cast<uint8_t>(lower);
				out[i * 2 + 1] = static_cast<uint8_t>(upper);
			}
			return globalScale / 8;
		}

		template<>
		float requantizePackedU4<ArchType::balanced>(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		)
		{
			return requantizePackedU4<ArchType::none>(n, qgroup, packedInput, localScale, globalScale, toUint8, out);
		}
	}
}

#define ARCH_TYPE ArchType::none
#include "eigen_gemm.hpp"

namespace kiwi
{
	namespace gemm
	{
		template<>
		void gemm<ArchType::balanced>(
			size_t m, size_t n, size_t k,
			const float* aT, size_t strideA,
			const float* b, size_t strideB,
			float* c, size_t strideC,
			bool zeroMode
		)
		{
			return gemm<ArchType::none>(m, n, k, aT, strideA, b, strideB, c, strideC, zeroMode);
		}

		template<>
		void gemv<ArchType::balanced>(
			size_t m, size_t k,
			const float* aT, size_t strideA,
			const float* b,
			float* c,
			bool zeroMode
		)
		{
			return gemv<ArchType::none>(m, k, aT, strideA, b, c, zeroMode);
		}

		template<>
		void mul<ArchType::balanced>(
			size_t n,
			float a,
			const float* b,
			float* c
		)
		{
			return mul<ArchType::none>(n, a, b, c);
		}

		template<>
		void invNorm<ArchType::balanced>(
			size_t m, size_t k,
			const float* a, size_t lda,
			float* out
		)
		{
			return invNorm<ArchType::none>(m, k, a, lda, out);
		}
	}
}
