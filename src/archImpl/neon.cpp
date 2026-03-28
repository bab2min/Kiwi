#include "../MathFunc.hpp"
#include "../qgemm.hpp"
#include <arm_neon.h>

namespace kiwi
{
	namespace lm
	{
		template float logSumExp<ArchType::neon>(const float* arr, size_t size);
		template void logSumExpTransposed<ArchType::neon>(float* arr, size_t size, size_t batchSize, size_t stride);
		template void logSoftmax<ArchType::neon>(float* arr, size_t size);
		template void logSoftmaxTransposed<ArchType::neon>(float* arr, size_t size, size_t batchSize, size_t stride);
	}

	namespace qgemm
	{
		template int32_t dotprod<ArchType::neon>(const uint8_t* a, const int8_t* b, size_t n);

		template void scatteredGEMMOpt<ArchType::neon>(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		static FORCE_INLINE int32_t reduce_sum_s32(int32x4_t v)
		{
			v = vpaddq_s32(v, v);
			v = vpaddq_s32(v, v);
			return vgetq_lane_s32(v, 0);
		}

		// gemv: compute c[i] = (dotprod(a_uint8, b_int8[i]) - bSum[i]) * aScale * bScale[i]
		// a: [k uint8][float aScale], b rows: [k int8][float bScale][int32 bSum]
		inline void gemv_neon(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			const float aScale = *reinterpret_cast<const float*>(a + k);
			float bScale[4];
			int32_t bSum[4];
			const float32x4_t vaScale = vdupq_n_f32(aScale);

			for (size_t mi = 0; mi < m; mi += 4)
			{
				const int8_t* bPtr0 = b + ldb * (mi + 0);
				const int8_t* bPtr1 = b + ldb * (mi + 1);
				const int8_t* bPtr2 = b + ldb * (mi + 2);
				const int8_t* bPtr3 = b + ldb * (mi + 3);

				int32x4_t sum0 = vdupq_n_s32(0);
				int32x4_t sum1 = vdupq_n_s32(0);
				int32x4_t sum2 = vdupq_n_s32(0);
				int32x4_t sum3 = vdupq_n_s32(0);

				for (size_t j = 0; j < k; j += 16)
				{
					uint8x16_t pa = vld1q_u8(a + j);
					int8x16_t pb0 = vld1q_s8(bPtr0 + j);
					int8x16_t pb1 = vld1q_s8(bPtr1 + j);
					int8x16_t pb2 = vld1q_s8(bPtr2 + j);
					int8x16_t pb3 = vld1q_s8(bPtr3 + j);

					// Extend a (uint8) to int16 via zero-extend; b (int8) via sign-extend
					// Product fits in int16: range [-32640, 32385]
					int16x8_t pa_lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(pa)));
					int16x8_t pa_hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(pa)));

					sum0 = vpadalq_s16(sum0, vmulq_s16(pa_lo, vmovl_s8(vget_low_s8(pb0))));
					sum0 = vpadalq_s16(sum0, vmulq_s16(pa_hi, vmovl_s8(vget_high_s8(pb0))));
					sum1 = vpadalq_s16(sum1, vmulq_s16(pa_lo, vmovl_s8(vget_low_s8(pb1))));
					sum1 = vpadalq_s16(sum1, vmulq_s16(pa_hi, vmovl_s8(vget_high_s8(pb1))));
					sum2 = vpadalq_s16(sum2, vmulq_s16(pa_lo, vmovl_s8(vget_low_s8(pb2))));
					sum2 = vpadalq_s16(sum2, vmulq_s16(pa_hi, vmovl_s8(vget_high_s8(pb2))));
					sum3 = vpadalq_s16(sum3, vmulq_s16(pa_lo, vmovl_s8(vget_low_s8(pb3))));
					sum3 = vpadalq_s16(sum3, vmulq_s16(pa_hi, vmovl_s8(vget_high_s8(pb3))));
				}

				bScale[0] = *reinterpret_cast<const float*>(bPtr0 + k);
				bScale[1] = *reinterpret_cast<const float*>(bPtr1 + k);
				bScale[2] = *reinterpret_cast<const float*>(bPtr2 + k);
				bScale[3] = *reinterpret_cast<const float*>(bPtr3 + k);
				bSum[0] = *reinterpret_cast<const int32_t*>(bPtr0 + k + 4);
				bSum[1] = *reinterpret_cast<const int32_t*>(bPtr1 + k + 4);
				bSum[2] = *reinterpret_cast<const int32_t*>(bPtr2 + k + 4);
				bSum[3] = *reinterpret_cast<const int32_t*>(bPtr3 + k + 4);

				const int32_t sArr[4] = {
					reduce_sum_s32(sum0) - bSum[0],
					reduce_sum_s32(sum1) - bSum[1],
					reduce_sum_s32(sum2) - bSum[2],
					reduce_sum_s32(sum3) - bSum[3]
				};
				const float32x4_t vbScale = vld1q_f32(bScale);
				const float32x4_t vfsums = vcvtq_f32_s32(vld1q_s32(sArr));
				vst1q_f32(c + mi, vmulq_f32(vmulq_f32(vfsums, vaScale), vbScale));
			}
		}

		// gemvS8S8: native int8 x int8 GEMV, no bias correction needed
		// a: [k int8][float aScale], b rows: [k int8][float bScale][int32 bSum (unused)]
		// result[i] = dotprod(a, b[i]) * aScale * bScale[i]
		inline void gemvS8S8_neon(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			const float aScale = *reinterpret_cast<const float*>(a + k);
			float bScale[4];
			const float32x4_t vaScale = vdupq_n_f32(aScale);

			for (size_t mi = 0; mi < m; mi += 4)
			{
				const int8_t* bPtr0 = b + ldb * (mi + 0);
				const int8_t* bPtr1 = b + ldb * (mi + 1);
				const int8_t* bPtr2 = b + ldb * (mi + 2);
				const int8_t* bPtr3 = b + ldb * (mi + 3);

				int32x4_t sum0 = vdupq_n_s32(0);
				int32x4_t sum1 = vdupq_n_s32(0);
				int32x4_t sum2 = vdupq_n_s32(0);
				int32x4_t sum3 = vdupq_n_s32(0);

				for (size_t j = 0; j < k; j += 16)
				{
					int8x16_t pa = vld1q_s8(a + j);
					int8x16_t pb0 = vld1q_s8(bPtr0 + j);
					int8x16_t pb1 = vld1q_s8(bPtr1 + j);
					int8x16_t pb2 = vld1q_s8(bPtr2 + j);
					int8x16_t pb3 = vld1q_s8(bPtr3 + j);

					// Native int8 x int8 dot product using vmull_s8
					// Product range: [-128*127, 127*127] = [-16256, 16129], fits in int16
					sum0 = vpadalq_s16(sum0, vmull_s8(vget_low_s8(pa), vget_low_s8(pb0)));
					sum0 = vpadalq_s16(sum0, vmull_s8(vget_high_s8(pa), vget_high_s8(pb0)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_low_s8(pa), vget_low_s8(pb1)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_high_s8(pa), vget_high_s8(pb1)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_low_s8(pa), vget_low_s8(pb2)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_high_s8(pa), vget_high_s8(pb2)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_low_s8(pa), vget_low_s8(pb3)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_high_s8(pa), vget_high_s8(pb3)));
				}

				bScale[0] = *reinterpret_cast<const float*>(bPtr0 + k);
				bScale[1] = *reinterpret_cast<const float*>(bPtr1 + k);
				bScale[2] = *reinterpret_cast<const float*>(bPtr2 + k);
				bScale[3] = *reinterpret_cast<const float*>(bPtr3 + k);
				// bSum correction is not needed: native int8 x int8 gives exact result

				const int32_t sArr[4] = {
					reduce_sum_s32(sum0),
					reduce_sum_s32(sum1),
					reduce_sum_s32(sum2),
					reduce_sum_s32(sum3)
				};
				const float32x4_t vbScale = vld1q_f32(bScale);
				const float32x4_t vfsums = vcvtq_f32_s32(vld1q_s32(sArr));
				vst1q_f32(c + mi, vmulq_f32(vmulq_f32(vfsums, vaScale), vbScale));
			}
		}

		// gemvU8U8: centered uint8 x uint8 GEMV (both a and b represent int8 biased by +128)
		// result[i] = sum((a-128) * (b[i]-128)) * aScale * bScale[i]
		inline void gemvU8U8_neon(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			const uint8x16_t bias = vdupq_n_u8(128);
			const float aScale = *reinterpret_cast<const float*>(a + k);
			float bScale[4];
			const float32x4_t vaScale = vdupq_n_f32(aScale);

			for (size_t mi = 0; mi < m; mi += 4)
			{
				const uint8_t* bPtr0 = b + ldb * (mi + 0);
				const uint8_t* bPtr1 = b + ldb * (mi + 1);
				const uint8_t* bPtr2 = b + ldb * (mi + 2);
				const uint8_t* bPtr3 = b + ldb * (mi + 3);

				int32x4_t sum0 = vdupq_n_s32(0);
				int32x4_t sum1 = vdupq_n_s32(0);
				int32x4_t sum2 = vdupq_n_s32(0);
				int32x4_t sum3 = vdupq_n_s32(0);

				for (size_t j = 0; j < k; j += 16)
				{
					// Convert from uint8 (0-255) to int8 (-128 to 127) via XOR 0x80
					int8x16_t pa = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(a + j), bias));
					int8x16_t pb0 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(bPtr0 + j), bias));
					int8x16_t pb1 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(bPtr1 + j), bias));
					int8x16_t pb2 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(bPtr2 + j), bias));
					int8x16_t pb3 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(bPtr3 + j), bias));

					sum0 = vpadalq_s16(sum0, vmull_s8(vget_low_s8(pa), vget_low_s8(pb0)));
					sum0 = vpadalq_s16(sum0, vmull_s8(vget_high_s8(pa), vget_high_s8(pb0)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_low_s8(pa), vget_low_s8(pb1)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_high_s8(pa), vget_high_s8(pb1)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_low_s8(pa), vget_low_s8(pb2)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_high_s8(pa), vget_high_s8(pb2)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_low_s8(pa), vget_low_s8(pb3)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_high_s8(pa), vget_high_s8(pb3)));
				}

				bScale[0] = *reinterpret_cast<const float*>(bPtr0 + k);
				bScale[1] = *reinterpret_cast<const float*>(bPtr1 + k);
				bScale[2] = *reinterpret_cast<const float*>(bPtr2 + k);
				bScale[3] = *reinterpret_cast<const float*>(bPtr3 + k);

				const int32_t sArr[4] = {
					reduce_sum_s32(sum0),
					reduce_sum_s32(sum1),
					reduce_sum_s32(sum2),
					reduce_sum_s32(sum3)
				};
				const float32x4_t vbScale = vld1q_f32(bScale);
				const float32x4_t vfsums = vcvtq_f32_s32(vld1q_s32(sArr));
				vst1q_f32(c + mi, vmulq_f32(vmulq_f32(vfsums, vaScale), vbScale));
			}
		}

		template<>
		void gemv<ArchType::neon>(size_t m, size_t k, const uint8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return gemv_neon(m, k, a, b, ldb, c);
		}

		template<>
		void gemvS8S8<ArchType::neon>(size_t m, size_t k, const int8_t* a, const int8_t* b, size_t ldb, float* c)
		{
			return gemvS8S8_neon(m, k, a, b, ldb, c);
		}

		template<>
		void gemvU8U8<ArchType::neon>(size_t m, size_t k, const uint8_t* a, const uint8_t* b, size_t ldb, float* c)
		{
			return gemvU8U8_neon(m, k, a, b, ldb, c);
		}

		template<>
		float dotS8S8<ArchType::neon>(size_t k, const int8_t* a, const int8_t* b)
		{
			const float aScale = *reinterpret_cast<const float*>(a + k);
			const float bScale = *reinterpret_cast<const float*>(b + k);
			// No bSum correction needed for native int8 x int8

			int32x4_t sum = vdupq_n_s32(0);
			for (size_t i = 0; i < k; i += 16)
			{
				int8x16_t pa = vld1q_s8(a + i);
				int8x16_t pb = vld1q_s8(b + i);
				sum = vpadalq_s16(sum, vmull_s8(vget_low_s8(pa), vget_low_s8(pb)));
				sum = vpadalq_s16(sum, vmull_s8(vget_high_s8(pa), vget_high_s8(pb)));
			}
			return static_cast<float>(reduce_sum_s32(sum)) * aScale * bScale;
		}

		template<>
		float dotU8U8<ArchType::neon>(size_t k, const uint8_t* a, const uint8_t* b)
		{
			const float aScale = *reinterpret_cast<const float*>(a + k);
			const float bScale = *reinterpret_cast<const float*>(b + k);
			const uint8x16_t bias = vdupq_n_u8(128);

			int32x4_t sum = vdupq_n_s32(0);
			for (size_t i = 0; i < k; i += 16)
			{
				int8x16_t pa = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(a + i), bias));
				int8x16_t pb = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(b + i), bias));
				sum = vpadalq_s16(sum, vmull_s8(vget_low_s8(pa), vget_low_s8(pb)));
				sum = vpadalq_s16(sum, vmull_s8(vget_high_s8(pa), vget_high_s8(pb)));
			}
			return static_cast<float>(reduce_sum_s32(sum)) * aScale * bScale;
		}

		template<>
		void invNormS8<ArchType::neon>(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		)
		{
			for (size_t mi = 0; mi < m; mi += 4)
			{
				const int8_t* aPtr0 = a + lda * (mi + 0);
				const int8_t* aPtr1 = a + lda * (mi + 1);
				const int8_t* aPtr2 = a + lda * (mi + 2);
				const int8_t* aPtr3 = a + lda * (mi + 3);

				int32x4_t sum0 = vdupq_n_s32(0);
				int32x4_t sum1 = vdupq_n_s32(0);
				int32x4_t sum2 = vdupq_n_s32(0);
				int32x4_t sum3 = vdupq_n_s32(0);

				for (size_t j = 0; j < k; j += 16)
				{
					int8x16_t pa0 = vld1q_s8(aPtr0 + j);
					int8x16_t pa1 = vld1q_s8(aPtr1 + j);
					int8x16_t pa2 = vld1q_s8(aPtr2 + j);
					int8x16_t pa3 = vld1q_s8(aPtr3 + j);

					// Compute a^2 using native int8 x int8 multiply
					// Max product: (-128)*(-128) = 16384, fits in int16
					sum0 = vpadalq_s16(sum0, vmull_s8(vget_low_s8(pa0), vget_low_s8(pa0)));
					sum0 = vpadalq_s16(sum0, vmull_s8(vget_high_s8(pa0), vget_high_s8(pa0)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_low_s8(pa1), vget_low_s8(pa1)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_high_s8(pa1), vget_high_s8(pa1)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_low_s8(pa2), vget_low_s8(pa2)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_high_s8(pa2), vget_high_s8(pa2)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_low_s8(pa3), vget_low_s8(pa3)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_high_s8(pa3), vget_high_s8(pa3)));
				}

				const float aScale0 = *reinterpret_cast<const float*>(aPtr0 + k);
				const float aScale1 = *reinterpret_cast<const float*>(aPtr1 + k);
				const float aScale2 = *reinterpret_cast<const float*>(aPtr2 + k);
				const float aScale3 = *reinterpret_cast<const float*>(aPtr3 + k);

				const float rArr[4] = {
					static_cast<float>(reduce_sum_s32(sum0)) * aScale0 * aScale0,
					static_cast<float>(reduce_sum_s32(sum1)) * aScale1 * aScale1,
					static_cast<float>(reduce_sum_s32(sum2)) * aScale2 * aScale2,
					static_cast<float>(reduce_sum_s32(sum3)) * aScale3 * aScale3
				};
				vst1q_f32(out + mi, vrsqrteq_f32(vld1q_f32(rArr)));
			}
		}

		template<>
		void invNormU8<ArchType::neon>(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		)
		{
			const uint8x16_t bias = vdupq_n_u8(128);

			for (size_t mi = 0; mi < m; mi += 4)
			{
				const uint8_t* aPtr0 = a + lda * (mi + 0);
				const uint8_t* aPtr1 = a + lda * (mi + 1);
				const uint8_t* aPtr2 = a + lda * (mi + 2);
				const uint8_t* aPtr3 = a + lda * (mi + 3);

				int32x4_t sum0 = vdupq_n_s32(0);
				int32x4_t sum1 = vdupq_n_s32(0);
				int32x4_t sum2 = vdupq_n_s32(0);
				int32x4_t sum3 = vdupq_n_s32(0);

				for (size_t j = 0; j < k; j += 16)
				{
					// Center uint8 to int8 via XOR 0x80: (a-128)
					int8x16_t pa0 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(aPtr0 + j), bias));
					int8x16_t pa1 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(aPtr1 + j), bias));
					int8x16_t pa2 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(aPtr2 + j), bias));
					int8x16_t pa3 = vreinterpretq_s8_u8(veorq_u8(vld1q_u8(aPtr3 + j), bias));

					// Compute (a-128)^2
					sum0 = vpadalq_s16(sum0, vmull_s8(vget_low_s8(pa0), vget_low_s8(pa0)));
					sum0 = vpadalq_s16(sum0, vmull_s8(vget_high_s8(pa0), vget_high_s8(pa0)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_low_s8(pa1), vget_low_s8(pa1)));
					sum1 = vpadalq_s16(sum1, vmull_s8(vget_high_s8(pa1), vget_high_s8(pa1)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_low_s8(pa2), vget_low_s8(pa2)));
					sum2 = vpadalq_s16(sum2, vmull_s8(vget_high_s8(pa2), vget_high_s8(pa2)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_low_s8(pa3), vget_low_s8(pa3)));
					sum3 = vpadalq_s16(sum3, vmull_s8(vget_high_s8(pa3), vget_high_s8(pa3)));
				}

				const float aScale0 = *reinterpret_cast<const float*>(aPtr0 + k);
				const float aScale1 = *reinterpret_cast<const float*>(aPtr1 + k);
				const float aScale2 = *reinterpret_cast<const float*>(aPtr2 + k);
				const float aScale3 = *reinterpret_cast<const float*>(aPtr3 + k);

				const float rArr[4] = {
					static_cast<float>(reduce_sum_s32(sum0)) * aScale0 * aScale0,
					static_cast<float>(reduce_sum_s32(sum1)) * aScale1 * aScale1,
					static_cast<float>(reduce_sum_s32(sum2)) * aScale2 * aScale2,
					static_cast<float>(reduce_sum_s32(sum3)) * aScale3 * aScale3
				};
				vst1q_f32(out + mi, vrsqrteq_f32(vld1q_f32(rArr)));
			}
		}

		template<>
		float requantizePackedU4<ArchType::neon>(
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

#define Eigen EigenNeon
#define ARCH_TYPE ArchType::neon
#include "eigen_gemm.hpp"
