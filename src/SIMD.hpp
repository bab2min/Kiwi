#pragma once

#ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

#if _MSC_VER && !__INTEL_COMPILER
    #define STRONG_INLINE __forceinline
#else
    #define STRONG_INLINE inline
#endif

#include "ArchAvailable.h"

namespace kiwi
{
    namespace simd
    {
        template<ArchType arch>
        struct PacketTrait;

        template<ArchType arch>
        using FloatPacket = typename PacketTrait<arch>::FloatPacket;

        template<ArchType arch>
        using IntPacket = typename PacketTrait<arch>::IntPacket;

        template<ArchType arch, class O>
        class OperatorBase
        {
        public:
            enum { packetSize = PacketTrait<arch>::size };

            using FPacket = typename PacketTrait<arch>::FloatPacket;
            using IPacket = typename PacketTrait<arch>::IntPacket;

            static STRONG_INLINE FPacket addf(FPacket a, FPacket b) { return O::addf(a, b); }
            static STRONG_INLINE FPacket subf(FPacket a, FPacket b) { return O::subf(a, b); }
            static STRONG_INLINE FPacket mulf(FPacket a, FPacket b) { return O::mulf(a, b); }
            static STRONG_INLINE FPacket divf(FPacket a, FPacket b) { return O::divf(a, b); }
            static STRONG_INLINE FPacket maddf(FPacket a, FPacket b, FPacket c) { return O::maddf(a, b, c); }
            static STRONG_INLINE FPacket set1f(float a) { return O::set1f(a); }
            static STRONG_INLINE FPacket loadf(const float* a) { return O::loadf(a); }
            static STRONG_INLINE void storef(float* a, FPacket b) { return O::storef(a, b); }
            static STRONG_INLINE FPacket maxf(FPacket a, FPacket b) { return O::maxf(a, b); }
            static STRONG_INLINE FPacket minf(FPacket a, FPacket b) { return O::minf(a, b); }
            static STRONG_INLINE FPacket floorf(FPacket a) { return O::floorf(a); }
            static STRONG_INLINE FPacket negatef(FPacket a) { return O::negatef(a); }
            static STRONG_INLINE FPacket zerof() { return O::zerof(); }
            static STRONG_INLINE IPacket cast_to_int(FPacket a) { return O::cast_to_int(a); }
            static STRONG_INLINE FPacket reinterpret_as_float(IPacket a) { return O::reinterpret_as_float(a); }
            static STRONG_INLINE float firstf(FPacket a) { return O::firstf(a); }
            static STRONG_INLINE float redsumf(FPacket a) { return O::redsumf(a); }
            static STRONG_INLINE float redmaxf(FPacket a) { return O::redmaxf(a); }
            static STRONG_INLINE FPacket redmaxbf(FPacket a) { return O::redmaxbf(a); }

            template<int bit>
            static STRONG_INLINE IPacket sll(IPacket a) { return O::template sll<bit>(a); }

            static STRONG_INLINE FPacket ldexpf_fast(FPacket a, FPacket exponent)
            {
                static constexpr int exponentBits = 8, mantissaBits = 23;

                const FPacket bias = set1f((1 << (exponentBits - 1)) - 1);  // 127
                const FPacket limit = set1f((1 << exponentBits) - 1);     // 255
                // restrict biased exponent between 0 and 255 for float.
                const auto e = cast_to_int(minf(maxf(addf(exponent, bias), zerof()), limit)); // exponent + 127
                // return a * (2^e)
                return mulf(a, reinterpret_as_float(sll<mantissaBits>(e)));
            }

            static STRONG_INLINE FPacket expf(FPacket _x)
            {
                const FPacket cst_1 = set1f(1.0f);
                const FPacket cst_half = set1f(0.5f);
                const FPacket cst_exp_hi = set1f(88.723f);
                const FPacket cst_exp_lo = set1f(-88.723f);

                const FPacket cst_cephes_LOG2EF = set1f(1.44269504088896341f);
                const FPacket cst_cephes_exp_p0 = set1f(1.9875691500E-4f);
                const FPacket cst_cephes_exp_p1 = set1f(1.3981999507E-3f);
                const FPacket cst_cephes_exp_p2 = set1f(8.3334519073E-3f);
                const FPacket cst_cephes_exp_p3 = set1f(4.1665795894E-2f);
                const FPacket cst_cephes_exp_p4 = set1f(1.6666665459E-1f);
                const FPacket cst_cephes_exp_p5 = set1f(5.0000001201E-1f);

                // Clamp x.
                FPacket x = maxf(minf(_x, cst_exp_hi), cst_exp_lo);

                // Express exp(x) as exp(m*ln(2) + r), start by extracting
                // m = floor(x/ln(2) + 0.5).
                FPacket m = floorf(maddf(x, cst_cephes_LOG2EF, cst_half));

                // Get r = x - m*ln(2). If no FMA instructions are available, m*ln(2) is
                // subtracted out in two parts, m*C1+m*C2 = m*ln(2), to avoid accumulating
                // truncation errors.
                const FPacket cst_cephes_exp_C1 = set1f(-0.693359375f);
                const FPacket cst_cephes_exp_C2 = set1f(2.12194440e-4f);
                FPacket r = maddf(m, cst_cephes_exp_C1, x);
                r = maddf(m, cst_cephes_exp_C2, r);

                FPacket r2 = mulf(r, r);
                FPacket r3 = mulf(r2, r);

                // Evaluate the polynomial approximant,improved by instruction-level parallelism.
                FPacket y, y1, y2;
                y = maddf(cst_cephes_exp_p0, r, cst_cephes_exp_p1);
                y1 = maddf(cst_cephes_exp_p3, r, cst_cephes_exp_p4);
                y2 = addf(r, cst_1);
                y = maddf(y, r, cst_cephes_exp_p2);
                y1 = maddf(y1, r, cst_cephes_exp_p5);
                y = maddf(y, r3, y1);
                y = maddf(y, r2, y2);

                // Return 2^m * exp(r).
                // TODO: replace pldexp with faster implementation since y in [-1, 1).
                return maxf(ldexpf_fast(y, m), _x);
            }
        };

        template<ArchType arch>
        class Operator;
    }
}

#if defined(__x86_64__) || CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64 || defined(KIWI_ARCH_X86) || defined(KIWI_ARCH_X86_64)
#include <immintrin.h>
namespace kiwi
{
    namespace simd
    {
        template<>
        struct PacketTrait<ArchType::sse2>
        {
            static constexpr size_t size = 4;
            using IntPacket = __m128i;
            using FloatPacket = __m128;
        };

        template<>
        struct PacketTrait<ArchType::sse4_1> : public PacketTrait<ArchType::sse2>
        {};

#if defined(_MSC_VER) || defined(__SSE2__) || defined(__AVX2__)
        template<>
        class Operator<ArchType::sse2> : public OperatorBase<ArchType::sse2, Operator<ArchType::sse2>>
        {
        public:

            static STRONG_INLINE __m128 addf(__m128 a, __m128 b) { return _mm_add_ps(a, b); }
            static STRONG_INLINE __m128 subf(__m128 a, __m128 b) { return _mm_sub_ps(a, b); }
            static STRONG_INLINE __m128 mulf(__m128 a, __m128 b) { return _mm_mul_ps(a, b); }
            static STRONG_INLINE __m128 divf(__m128 a, __m128 b) { return _mm_div_ps(a, b); }
            static STRONG_INLINE __m128 maddf(__m128 a, __m128 b, __m128 c) { return addf(mulf(a, b), c); }
            static STRONG_INLINE __m128 set1f(float a) { return _mm_set1_ps(a); }
            static STRONG_INLINE __m128 loadf(const float* a) { return _mm_load_ps(a); }
            static STRONG_INLINE void storef(float* a, __m128 b) { return _mm_store_ps(a, b); }
            static STRONG_INLINE __m128 maxf(__m128 a, __m128 b) { return _mm_max_ps(a, b); }
            static STRONG_INLINE __m128 minf(__m128 a, __m128 b) { return _mm_min_ps(a, b); }

            static STRONG_INLINE __m128 absf(__m128 a)
            {
                const __m128 mask = _mm_castsi128_ps(_mm_setr_epi32(0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF));
                return _mm_and_ps(a, mask);
            }

            static STRONG_INLINE __m128 selectf(__m128 mask, __m128 a, __m128 b)
            {
                return _mm_or_ps(_mm_and_ps(mask, a), _mm_andnot_ps(mask, b));
            }

            static STRONG_INLINE  __m128 rint(__m128 a)
            {
                const __m128 limit = set1f(static_cast<float>(1 << 23));
                const __m128 abs_a = absf(a);
                __m128 r = addf(abs_a, limit);
#ifdef __GNUC__
                __asm__("" : "+g,x" (r));
#endif
                r = subf(r, limit);

                r = selectf(_mm_cmplt_ps(abs_a, limit),
                    selectf(_mm_cmplt_ps(a, zerof()), negatef(r), r), a);
                return r;
            }

            static STRONG_INLINE __m128 floorf(__m128 a)
            {
                const __m128 cst_1 = set1f(1.0f);
                __m128 tmp = rint(a);
                __m128 mask = _mm_cmpgt_ps(tmp, a);
                mask = _mm_and_ps(mask, cst_1);
                return _mm_sub_ps(tmp, mask);
            }

            static STRONG_INLINE __m128 zerof() { return _mm_setzero_ps(); }
            static STRONG_INLINE __m128 negatef(__m128 a) { return subf(zerof(), a); }
            static STRONG_INLINE __m128i cast_to_int(__m128 a) { return _mm_cvtps_epi32(a); }
            static STRONG_INLINE __m128 reinterpret_as_float(__m128i a) { return _mm_castsi128_ps(a); }
            template<int bit> static STRONG_INLINE __m128i sll(__m128i a) { return _mm_slli_epi32(a, bit); }
            static STRONG_INLINE float firstf(__m128 a) { return _mm_cvtss_f32(a); }

            static STRONG_INLINE float redsumf(__m128 a)
            {
                __m128 tmp = _mm_add_ps(a, _mm_movehl_ps(a, a));
                return firstf(_mm_add_ss(tmp, _mm_shuffle_ps(tmp, tmp, 1)));
            }

            static STRONG_INLINE float redmaxf(__m128 a)
            {
                __m128 tmp = _mm_max_ps(a, _mm_movehl_ps(a, a));
                return firstf(_mm_max_ss(tmp, _mm_shuffle_ps(tmp, tmp, 1)));
            }

            static STRONG_INLINE __m128 redmaxbf(__m128 a)
            {
                __m128 tmp = _mm_max_ps(a, _mm_shuffle_ps(a, a, _MM_SHUFFLE(1, 0, 3, 2)));
                return _mm_max_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
            }
        };
#endif

#if defined(_MSC_VER) || defined(__SSE4_1__) || defined(__AVX2__)
        template<>
        class Operator<ArchType::sse4_1> : public OperatorBase<ArchType::sse4_1, Operator<ArchType::sse4_1>>
        {
        public:

            static STRONG_INLINE __m128 addf(__m128 a, __m128 b) { return _mm_add_ps(a, b); }
            static STRONG_INLINE __m128 subf(__m128 a, __m128 b) { return _mm_sub_ps(a, b); }
            static STRONG_INLINE __m128 mulf(__m128 a, __m128 b) { return _mm_mul_ps(a, b); }
            static STRONG_INLINE __m128 divf(__m128 a, __m128 b) { return _mm_div_ps(a, b); }
            static STRONG_INLINE __m128 maddf(__m128 a, __m128 b, __m128 c) { return addf(mulf(a, b), c); }
            static STRONG_INLINE __m128 set1f(float a) { return _mm_set1_ps(a); }
            static STRONG_INLINE __m128 loadf(const float* a) { return _mm_load_ps(a); }
            static STRONG_INLINE void storef(float* a, __m128 b) { return _mm_store_ps(a, b); }
            static STRONG_INLINE __m128 maxf(__m128 a, __m128 b) { return _mm_max_ps(a, b); }
            static STRONG_INLINE __m128 minf(__m128 a, __m128 b) { return _mm_min_ps(a, b); }
            static STRONG_INLINE __m128 floorf(__m128 a) { return _mm_floor_ps(a); }
            static STRONG_INLINE __m128 zerof() { return _mm_setzero_ps(); }
            static STRONG_INLINE __m128 negatef(__m128 a) { return subf(zerof(), a); }
            static STRONG_INLINE __m128i cast_to_int(__m128 a) { return _mm_cvtps_epi32(a); }
            static STRONG_INLINE __m128 reinterpret_as_float(__m128i a) { return _mm_castsi128_ps(a); }
            template<int bit> static STRONG_INLINE __m128i sll(__m128i a) { return _mm_slli_epi32(a, bit); }
            static STRONG_INLINE float firstf(__m128 a) { return _mm_cvtss_f32(a); }

            static STRONG_INLINE float redsumf(__m128 a)
            {
                __m128 tmp = _mm_add_ps(a, _mm_movehl_ps(a, a));
                return firstf(_mm_add_ss(tmp, _mm_shuffle_ps(tmp, tmp, 1)));
            }

            static STRONG_INLINE float redmaxf(__m128 a)
            {
                __m128 tmp = _mm_max_ps(a, _mm_movehl_ps(a, a));
                return firstf(_mm_max_ss(tmp, _mm_shuffle_ps(tmp, tmp, 1)));
            }

            static STRONG_INLINE __m128 redmaxbf(__m128 a)
            {
                __m128 tmp = _mm_max_ps(a, _mm_shuffle_ps(a, a, _MM_SHUFFLE(1, 0, 3, 2)));
                return _mm_max_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
            }
        };
#endif

#if defined(_MSC_VER) || defined(__AVX2__)
        template<>
        struct PacketTrait<ArchType::avx2>
        {
            static constexpr size_t size = 8;
            using IntPacket = __m256i;
            using FloatPacket = __m256;
        };

        template<>
        class Operator<ArchType::avx2> : public OperatorBase<ArchType::avx2, Operator<ArchType::avx2>>
        {
        public:

            static STRONG_INLINE __m256 addf(__m256 a, __m256 b) { return _mm256_add_ps(a, b); }
            static STRONG_INLINE __m256 subf(__m256 a, __m256 b) { return _mm256_sub_ps(a, b); }
            static STRONG_INLINE __m256 mulf(__m256 a, __m256 b) { return _mm256_mul_ps(a, b); }
            static STRONG_INLINE __m256 divf(__m256 a, __m256 b) { return _mm256_div_ps(a, b); }
            static STRONG_INLINE __m256 maddf(__m256 a, __m256 b, __m256 c) { return _mm256_fmadd_ps(a, b, c); }
            static STRONG_INLINE __m256 set1f(float a) { return _mm256_set1_ps(a); }
            static STRONG_INLINE __m256 loadf(const float* a) { return _mm256_load_ps(a); }
            static STRONG_INLINE void storef(float* a, __m256 b) { return _mm256_store_ps(a, b); }
            static STRONG_INLINE __m256 maxf(__m256 a, __m256 b) { return _mm256_max_ps(a, b); }
            static STRONG_INLINE __m256 minf(__m256 a, __m256 b) { return _mm256_min_ps(a, b); }
            static STRONG_INLINE __m256 floorf(__m256 a) { return _mm256_floor_ps(a); }
            static STRONG_INLINE __m256 zerof() { return _mm256_setzero_ps(); }
            static STRONG_INLINE __m256 negatef(__m256 a) { return subf(zerof(), a); }
            static STRONG_INLINE __m256i cast_to_int(__m256 a) { return _mm256_cvtps_epi32(a); }
            static STRONG_INLINE __m256 reinterpret_as_float(__m256i a) { return _mm256_castsi256_ps(a); }
            template<int bit> static STRONG_INLINE __m256i sll(__m256i a) { return _mm256_slli_epi32(a, bit); }
            static STRONG_INLINE float firstf(__m256 a) { return _mm256_cvtss_f32(a); }

            static STRONG_INLINE float redsumf(__m256 a)
            {
                return Operator<ArchType::sse2>::redsumf(Operator<ArchType::sse2>::addf(_mm256_castps256_ps128(a), _mm256_extractf128_ps(a, 1)));
            }

            static STRONG_INLINE float redmaxf(__m256 a)
            {
                __m256 tmp = _mm256_max_ps(a, _mm256_permute2f128_ps(a, a, 1));
                tmp = _mm256_max_ps(tmp, _mm256_shuffle_ps(tmp, tmp, _MM_SHUFFLE(1, 0, 3, 2)));
                return firstf(_mm256_max_ps(tmp, _mm256_shuffle_ps(tmp, tmp, 1)));
            }

            static STRONG_INLINE __m256 redmaxbf(__m256 a)
            {
                __m256 tmp = _mm256_max_ps(a, _mm256_permute2f128_ps(a, a, 1));
                tmp = _mm256_max_ps(tmp, _mm256_shuffle_ps(tmp, tmp, _MM_SHUFFLE(1, 0, 3, 2)));
                return _mm256_max_ps(tmp, _mm256_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 3, 0, 1)));
            }
        };
#endif

#if defined(_MSC_VER) || defined(__AVX512F__) || defined(__AVX512BW__)
        template<>
        struct PacketTrait<ArchType::avx512bw>
        {
            static constexpr size_t size = 16;
            using IntPacket = __m512i;
            using FloatPacket = __m512;
        };

        template<>
        class Operator<ArchType::avx512bw> : public OperatorBase<ArchType::avx512bw, Operator<ArchType::avx512bw>>
        {
        public:

            static STRONG_INLINE __m512 addf(__m512 a, __m512 b) { return _mm512_add_ps(a, b); }
            static STRONG_INLINE __m512 subf(__m512 a, __m512 b) { return _mm512_sub_ps(a, b); }
            static STRONG_INLINE __m512 mulf(__m512 a, __m512 b) { return _mm512_mul_ps(a, b); }
            static STRONG_INLINE __m512 divf(__m512 a, __m512 b) { return _mm512_div_ps(a, b); }
            static STRONG_INLINE __m512 maddf(__m512 a, __m512 b, __m512 c) { return _mm512_fmadd_ps(a, b, c); }
            static STRONG_INLINE __m512 set1f(float a) { return _mm512_set1_ps(a); }
            static STRONG_INLINE __m512 loadf(const float* a) { return _mm512_load_ps(a); }
            static STRONG_INLINE void storef(float* a, __m512 b) { return _mm512_store_ps(a, b); }
            static STRONG_INLINE __m512 maxf(__m512 a, __m512 b) { return _mm512_max_ps(a, b); }
            static STRONG_INLINE __m512 minf(__m512 a, __m512 b) { return _mm512_min_ps(a, b); }
            static STRONG_INLINE __m512 floorf(__m512 a) { return _mm512_floor_ps(a); }
            static STRONG_INLINE __m512 zerof() { return _mm512_setzero_ps(); }
            static STRONG_INLINE __m512 negatef(__m512 a) { return subf(zerof(), a); }
            static STRONG_INLINE __m512i cast_to_int(__m512 a) { return _mm512_cvtps_epi32(a); }
            static STRONG_INLINE __m512 reinterpret_as_float(__m512i a) { return _mm512_castsi512_ps(a); }
            template<int bit> static STRONG_INLINE __m512i sll(__m512i a) { return _mm512_slli_epi32(a, bit); }
            static STRONG_INLINE float firstf(__m512 a) { return _mm512_cvtss_f32(a); }

            static STRONG_INLINE float redsumf(__m512 a)
            {
                __m128 lane0 = _mm512_extractf32x4_ps(a, 0);
                __m128 lane1 = _mm512_extractf32x4_ps(a, 1);
                __m128 lane2 = _mm512_extractf32x4_ps(a, 2);
                __m128 lane3 = _mm512_extractf32x4_ps(a, 3);
                __m128 sum = _mm_add_ps(_mm_add_ps(lane0, lane1), _mm_add_ps(lane2, lane3));
                sum = _mm_hadd_ps(sum, sum);
                sum = _mm_hadd_ps(sum, _mm_permute_ps(sum, 1));
                return _mm_cvtss_f32(sum);
            }

            static STRONG_INLINE float redmaxf(__m512 a)
            {
                __m128 lane0 = _mm512_extractf32x4_ps(a, 0);
                __m128 lane1 = _mm512_extractf32x4_ps(a, 1);
                __m128 lane2 = _mm512_extractf32x4_ps(a, 2);
                __m128 lane3 = _mm512_extractf32x4_ps(a, 3);
                __m128 res = _mm_max_ps(_mm_max_ps(lane0, lane1), _mm_max_ps(lane2, lane3));
                res = _mm_max_ps(res, _mm_permute_ps(res, _MM_SHUFFLE(0, 0, 3, 2)));
                return Operator<ArchType::sse2>::firstf(_mm_max_ps(res, _mm_permute_ps(res, _MM_SHUFFLE(0, 0, 0, 1))));
            }

            static STRONG_INLINE __m512 redmaxbf(__m512 a)
            {
                return set1f(redmaxf(a));
            }
        };
#endif
    }
}

#elif CPUINFO_ARCH_ARM || CPUINFO_ARCH_ARM64 || KIWI_ARCH_ARM64
#include <arm_neon.h>
namespace kiwi
{
    namespace simd
    {
        template<>
        struct PacketTrait<ArchType::neon>
        {
            static constexpr size_t size = 4;
            using IntPacket = int32x4_t;
            using FloatPacket = float32x4_t;
        };

        template<>
        class Operator<ArchType::neon> : public OperatorBase<ArchType::neon, Operator<ArchType::neon>>
        {
        public:

            static STRONG_INLINE float32x4_t addf(float32x4_t a, float32x4_t b) { return vaddq_f32(a, b); }
            static STRONG_INLINE float32x4_t subf(float32x4_t a, float32x4_t b) { return vsubq_f32(a, b); }
            static STRONG_INLINE float32x4_t mulf(float32x4_t a, float32x4_t b) { return vmulq_f32(a, b); }
            static STRONG_INLINE float32x4_t divf(float32x4_t a, float32x4_t b) { return vdivq_f32(a, b); }
            static STRONG_INLINE float32x4_t maddf(float32x4_t a, float32x4_t b, float32x4_t c) { return addf(mulf(a, b), c); }
            static STRONG_INLINE float32x4_t set1f(float a) { return vdupq_n_f32(a); }
            static STRONG_INLINE float32x4_t loadf(const float* a) { return vld1q_f32(a); }
            static STRONG_INLINE void storef(float* a, float32x4_t b) { return vst1q_f32(a, b); }
            static STRONG_INLINE float32x4_t maxf(float32x4_t a, float32x4_t b) { return vmaxq_f32(a, b); }
            static STRONG_INLINE float32x4_t minf(float32x4_t a, float32x4_t b) { return vminq_f32(a, b); }
            static STRONG_INLINE float32x4_t floorf(float32x4_t a) { return vrndmq_f32(a); }
            static STRONG_INLINE float32x4_t zerof() { return set1f(0.f); }
            static STRONG_INLINE float32x4_t negatef(float32x4_t a) { return subf(zerof(), a); }
            static STRONG_INLINE int32x4_t cast_to_int(float32x4_t a) { return vcvtq_s32_f32(a); }
            static STRONG_INLINE float32x4_t reinterpret_as_float(int32x4_t a) { return vreinterpretq_f32_s32(a); }
            template<int bit> static STRONG_INLINE int32x4_t sll(int32x4_t a) { return vshlq_n_s32(a, bit); }
            static STRONG_INLINE float firstf(float32x4_t a) { return vgetq_lane_f32(a, 0); }

            static STRONG_INLINE float redsumf(float32x4_t a)
            {
                const float32x2_t sum = vadd_f32(vget_low_f32(a), vget_high_f32(a));
                return vget_lane_f32(vpadd_f32(sum, sum), 0);
            }

            static STRONG_INLINE float redmaxf(float32x4_t a)
            {
                const float32x2_t max = vmax_f32(vget_low_f32(a), vget_high_f32(a));
                return vget_lane_f32(vpmax_f32(max, max), 0);
            }

            static STRONG_INLINE float32x4_t redmaxbf(float32x4_t a)
            {
                return set1f(redmaxf(a));
            }
        };
    }
}
#endif


#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif
