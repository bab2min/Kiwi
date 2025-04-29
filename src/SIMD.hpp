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

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline))
#else
#define FORCE_INLINE inline
#endif


#include "ArchAvailable.h"

namespace kiwi
{
    namespace simd
    {
        template<ArchType arch, size_t size>
        struct BestArchType
        {
			static constexpr ArchType value = arch;
        };

        template<ArchType arch>
        struct PacketTrait;

        template<ArchType arch>
        using FloatPacket = typename PacketTrait<arch>::FloatPacket;

        template<ArchType arch>
        using IntPacket = typename PacketTrait<arch>::IntPacket;

        template<ArchType arch, class O>
        struct OperatorBase
        {
            enum { packetSize = PacketTrait<arch>::size };

            using FPacket = typename PacketTrait<arch>::FloatPacket;
            using IPacket = typename PacketTrait<arch>::IntPacket;

            static STRONG_INLINE FPacket addf(FPacket a, FPacket b) { return O::addf(a, b); }
            static STRONG_INLINE FPacket subf(FPacket a, FPacket b) { return O::subf(a, b); }
            static STRONG_INLINE FPacket mulf(FPacket a, FPacket b) { return O::mulf(a, b); }
            static STRONG_INLINE FPacket divf(FPacket a, FPacket b) { return O::divf(a, b); }
            static STRONG_INLINE FPacket maddf(FPacket a, FPacket b, FPacket c) { return O::maddf(a, b, c); }
            static STRONG_INLINE IPacket set1i(int32_t a) { return O::set1i(a); }
            static STRONG_INLINE FPacket set1f(float a) { return O::set1f(a); }
            static STRONG_INLINE FPacket set1frombits(uint32_t a) { return O::reinterpret_as_float(O::set1i(a)); }
            static STRONG_INLINE FPacket loadf(const float* a) { return O::loadf(a); }
            static STRONG_INLINE void storef(float* a, FPacket b) { return O::storef(a, b); }
            static STRONG_INLINE FPacket maxf(FPacket a, FPacket b) { return O::maxf(a, b); }
            static STRONG_INLINE FPacket minf(FPacket a, FPacket b) { return O::minf(a, b); }
            static STRONG_INLINE FPacket floorf(FPacket a) { return O::floorf(a); }
            static STRONG_INLINE FPacket negatef(FPacket a) { return O::negatef(a); }
            static STRONG_INLINE FPacket zerof() { return O::zerof(); }
            static STRONG_INLINE FPacket cast_to_float(IPacket a) { return O::cast_to_float(a); }
            static STRONG_INLINE IPacket cast_to_int(FPacket a) { return O::cast_to_int(a); }
            static STRONG_INLINE FPacket reinterpret_as_float(IPacket a) { return O::reinterpret_as_float(a); }
			static STRONG_INLINE IPacket reinterpret_as_int(FPacket a) { return O::reinterpret_as_int(a); }
            static STRONG_INLINE float firstf(FPacket a) { return O::firstf(a); }
            static STRONG_INLINE float redsumf(FPacket a) { return O::redsumf(a); }
            static STRONG_INLINE float redmaxf(FPacket a) { return O::redmaxf(a); }
            static STRONG_INLINE FPacket redmaxbf(FPacket a) { return O::redmaxbf(a); }

			static STRONG_INLINE IPacket band(IPacket a, IPacket b) { return O::band(a, b); }
            static STRONG_INLINE FPacket band(FPacket a, FPacket b) { return O::band(a, b); }

			static STRONG_INLINE IPacket bor(IPacket a, IPacket b) { return O::bor(a, b); }
            static STRONG_INLINE FPacket bor(FPacket a, FPacket b) { return O::bor(a, b); }

            static STRONG_INLINE IPacket select(IPacket mask, IPacket a, IPacket b) { return O::select(mask, a, b); }
			static STRONG_INLINE FPacket select(FPacket mask, FPacket a, FPacket b) { return O::select(mask, a, b); }

			static STRONG_INLINE FPacket cmp_eq(FPacket a, FPacket b) { return O::cmp_eq(a, b); }
			static STRONG_INLINE FPacket cmp_le(FPacket a, FPacket b) { return O::cmp_le(a, b); }
			static STRONG_INLINE FPacket cmp_lt(FPacket a, FPacket b) { return O::cmp_lt(a, b); }
            static STRONG_INLINE FPacket cmp_lt_or_nan(FPacket a, FPacket b) { return O::cmp_lt_or_nan(a, b); }

            template<int bit>
            static STRONG_INLINE IPacket sll(IPacket a) { return O::template sll<bit>(a); }

			template<int bit>
			static STRONG_INLINE IPacket srl(IPacket a) { return O::template srl<bit>(a); }

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

            static STRONG_INLINE FPacket frexpf_fast(FPacket x, FPacket& exp)
            {
				// ignore nan, inf, 0, denormalized numbers.
                const IPacket exp_mask = set1i(0x7F800000),
                    inv_exp_mask = set1i(~0x7F800000),
                    norm_exp = set1i(126 << 23);
                const FPacket exp_bias = set1f(126);
                IPacket ix = reinterpret_as_int(x);
                exp = subf(cast_to_float(srl<23>(band(ix, exp_mask))), exp_bias);
				ix = bor(band(ix, inv_exp_mask), norm_exp);
				return reinterpret_as_float(ix);
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

            static STRONG_INLINE FPacket logf(FPacket _x)
            {
                FPacket x = _x;

                const FPacket cst_1 = set1f(1.0f);
                const FPacket cst_neg_half = set1f(-0.5f);
                // The smallest non denormalized float number.
                const FPacket cst_min_norm_pos = set1frombits(0x00800000u);
                const FPacket cst_minus_inf = set1frombits(0xff800000u);
                const FPacket cst_pos_inf = set1frombits(0x7f800000u);

                // Polynomial coefficients.
                const FPacket cst_cephes_SQRTHF = set1f(0.707106781186547524f);
                const FPacket cst_cephes_log_p0 = set1f(7.0376836292E-2f);
                const FPacket cst_cephes_log_p1 = set1f(-1.1514610310E-1f);
                const FPacket cst_cephes_log_p2 = set1f(1.1676998740E-1f);
                const FPacket cst_cephes_log_p3 = set1f(-1.2420140846E-1f);
                const FPacket cst_cephes_log_p4 = set1f(+1.4249322787E-1f);
                const FPacket cst_cephes_log_p5 = set1f(-1.6668057665E-1f);
                const FPacket cst_cephes_log_p6 = set1f(+2.0000714765E-1f);
                const FPacket cst_cephes_log_p7 = set1f(-2.4999993993E-1f);
                const FPacket cst_cephes_log_p8 = set1f(+3.3333331174E-1f);

                // Truncate input values to the minimum positive normal.
                x = maxf(x, cst_min_norm_pos);

                FPacket e;
                // extract significant in the range [0.5,1) and exponent
                x = frexpf_fast(x, e);

                // part2: Shift the inputs from the range [0.5,1) to [sqrt(1/2),sqrt(2))
                // and shift by -1. The values are then centered around 0, which improves
                // the stability of the polynomial evaluation.
                //   if( x < SQRTHF ) {
                //     e -= 1;
                //     x = x + x - 1.0;
                //   } else { x = x - 1.0; }
                FPacket mask = cmp_lt(x, cst_cephes_SQRTHF);
                FPacket tmp = band(x, mask);
                x = subf(x, cst_1);
                e = subf(e, band(cst_1, mask));
                x = addf(x, tmp);

                FPacket x2 = mulf(x, x);
                FPacket x3 = mulf(x2, x);

                // Evaluate the polynomial approximant of degree 8 in three parts, probably
                // to improve instruction-level parallelism.
                FPacket y, y1, y2;
                y = maddf(cst_cephes_log_p0, x, cst_cephes_log_p1);
                y1 = maddf(cst_cephes_log_p3, x, cst_cephes_log_p4);
                y2 = maddf(cst_cephes_log_p6, x, cst_cephes_log_p7);
                y = maddf(y, x, cst_cephes_log_p2);
                y1 = maddf(y1, x, cst_cephes_log_p5);
                y2 = maddf(y2, x, cst_cephes_log_p8);
                y = maddf(y, x3, y1);
                y = maddf(y, x3, y2);
                y = mulf(y, x3);

                y = maddf(cst_neg_half, x2, y);
                x = addf(x, y);

                const FPacket cst_ln2 = set1f(0.69314718f);
                x = maddf(e, cst_ln2, x);

                FPacket invalid_mask = cmp_lt_or_nan(_x, zerof());
                FPacket iszero_mask = cmp_eq(_x, zerof());
                FPacket pos_inf_mask = cmp_eq(_x, cst_pos_inf);
                // Filter out invalid inputs, i.e.:
                //  - negative arg will be NAN
                //  - 0 will be -INF
                //  - +INF will be +INF
                return select(iszero_mask, cst_minus_inf,
                    bor(select(pos_inf_mask, cst_pos_inf, x), invalid_mask));
            }

            static STRONG_INLINE int32_t dotprod(const uint8_t* a, const int8_t* b, size_t size)
            {
                return 0;
            }
        };

        template<ArchType arch, class O>
        struct OperatorImpl;

        template<ArchType arch>
        struct Operator;
    }
}

#if defined(__x86_64__) || CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64 || defined(KIWI_ARCH_X86) || defined(KIWI_ARCH_X86_64)
#include <immintrin.h>
namespace kiwi
{
    namespace simd
    {
        template<size_t size>
        struct BestArchType<ArchType::sse2, size>
        {
			static constexpr ArchType value = ArchType::sse2;
        };

        template<>
        struct PacketTrait<ArchType::sse2>
        {
            static constexpr size_t size = 4;
            using IntPacket = __m128i;
            using FloatPacket = __m128;
        };

        template<size_t size>
		struct BestArchType<ArchType::sse4_1, size> : public BestArchType<ArchType::sse2, size>
        {};

        template<>
        struct PacketTrait<ArchType::sse4_1> : public PacketTrait<ArchType::sse2>
        {};

#if defined(_MSC_VER) || defined(__SSE2__) || defined(__SSE4_1__) || defined(__AVX2__)
        template<class O>
        struct OperatorImpl<ArchType::sse2, O> : public OperatorBase<ArchType::sse2, O>
        {
            static STRONG_INLINE __m128 addf(__m128 a, __m128 b) { return _mm_add_ps(a, b); }
            static STRONG_INLINE __m128 subf(__m128 a, __m128 b) { return _mm_sub_ps(a, b); }
            static STRONG_INLINE __m128 mulf(__m128 a, __m128 b) { return _mm_mul_ps(a, b); }
            static STRONG_INLINE __m128 divf(__m128 a, __m128 b) { return _mm_div_ps(a, b); }
            static STRONG_INLINE __m128 maddf(__m128 a, __m128 b, __m128 c) { return O::addf(O::mulf(a, b), c); }
			static STRONG_INLINE __m128i set1i(int32_t a) { return _mm_set1_epi32(a); }
            static STRONG_INLINE __m128 set1f(float a) { return _mm_set1_ps(a); }
            static STRONG_INLINE __m128 loadf(const float* a) { return _mm_loadu_ps(a); }
            static STRONG_INLINE void storef(float* a, __m128 b) { return _mm_storeu_ps(a, b); }
            static STRONG_INLINE __m128 maxf(__m128 a, __m128 b) { return _mm_max_ps(a, b); }
            static STRONG_INLINE __m128 minf(__m128 a, __m128 b) { return _mm_min_ps(a, b); }

            static STRONG_INLINE __m128 absf(__m128 a)
            {
                const __m128 mask = _mm_castsi128_ps(_mm_setr_epi32(0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF));
                return _mm_and_ps(a, mask);
            }

			static STRONG_INLINE __m128 band(__m128 a, __m128 b) { return _mm_and_ps(a, b); }
            static STRONG_INLINE __m128i band(__m128i a, __m128i b) { return _mm_and_si128(a, b); }

			static STRONG_INLINE __m128 bor(__m128 a, __m128 b) { return _mm_or_ps(a, b); }
			static STRONG_INLINE __m128i bor(__m128i a, __m128i b) { return _mm_or_si128(a, b); }

            static STRONG_INLINE __m128 select(__m128 mask, __m128 a, __m128 b)
            {
                return _mm_or_ps(_mm_and_ps(mask, a), _mm_andnot_ps(mask, b));
            }
			static STRONG_INLINE __m128i select(__m128i mask, __m128i a, __m128i b)
			{
				return _mm_or_si128(_mm_and_si128(mask, a), _mm_andnot_si128(mask, b));
			}

			static STRONG_INLINE __m128 cmp_eq(__m128 a, __m128 b) { return _mm_cmpeq_ps(a, b); }
			static STRONG_INLINE __m128 cmp_le(__m128 a, __m128 b) { return _mm_cmple_ps(a, b); }
			static STRONG_INLINE __m128 cmp_lt(__m128 a, __m128 b) { return _mm_cmplt_ps(a, b); }
			static STRONG_INLINE __m128 cmp_lt_or_nan(__m128 a, __m128 b) { return _mm_cmpnge_ps(a, b); }

            static STRONG_INLINE  __m128 rint(__m128 a)
            {
                const __m128 limit = O::set1f(static_cast<float>(1 << 23));
                const __m128 abs_a = O::absf(a);
                __m128 r = O::addf(abs_a, limit);
#ifdef __GNUC__
                __asm__("" : "+g,x" (r));
#endif
                r = O::subf(r, limit);

                r = O::select(_mm_cmplt_ps(abs_a, limit),
                    O::select(_mm_cmplt_ps(a, O::zerof()), O::negatef(r), r), a);
                return r;
            }

            static STRONG_INLINE __m128 floorf(__m128 a)
            {
                const __m128 cst_1 = O::set1f(1.0f);
                __m128 tmp = rint(a);
                __m128 mask = _mm_cmpgt_ps(tmp, a);
                mask = _mm_and_ps(mask, cst_1);
                return _mm_sub_ps(tmp, mask);
            }

            static STRONG_INLINE __m128 zerof() { return _mm_setzero_ps(); }
            static STRONG_INLINE __m128 negatef(__m128 a) { return subf(zerof(), a); }
            static STRONG_INLINE __m128i cast_to_int(__m128 a) { return _mm_cvtps_epi32(a); }
			static STRONG_INLINE __m128 cast_to_float(__m128i a) { return _mm_cvtepi32_ps(a); }
			static STRONG_INLINE __m128i reinterpret_as_int(__m128 a) { return _mm_castps_si128(a); }
            static STRONG_INLINE __m128 reinterpret_as_float(__m128i a) { return _mm_castsi128_ps(a); }
            
            template<int bit> static STRONG_INLINE __m128i sll(__m128i a) { return _mm_slli_epi32(a, bit); }
			template<int bit> static STRONG_INLINE __m128i srl(__m128i a) { return _mm_srli_epi32(a, bit); }

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

        template<>
        struct Operator<ArchType::sse2> : public OperatorImpl<ArchType::sse2, Operator<ArchType::sse2>>
        {
        };
#endif

#if defined(_MSC_VER) || defined(__SSE4_1__) || defined(__AVX2__)
        template<class O>
        struct OperatorImpl<ArchType::sse4_1, O> : public OperatorImpl<ArchType::sse2, O>
        {
			static STRONG_INLINE __m128 select(__m128 mask, __m128 a, __m128 b)
			{
				return _mm_blendv_ps(b, a, mask);
			}

            static STRONG_INLINE __m128i select(__m128i mask, __m128i a, __m128i b)
            {
                return _mm_castps_si128(_mm_blendv_ps(_mm_castsi128_ps(b), _mm_castsi128_ps(a), _mm_castsi128_ps(mask)));
            }

			static STRONG_INLINE int32_t dotprod(const uint8_t* a, const int8_t* b, size_t size)
			{
				__m128i pa, pb, sum = _mm_setzero_si128();
                __m128i one16 = _mm_set1_epi16(1), pt;
				for (size_t i = 0; i < size; i += 16)
				{
					pa = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i));
					pb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i));
					pt = _mm_maddubs_epi16(pa, pb);
					sum = _mm_add_epi32(sum, _mm_madd_epi16(pt, one16));
				}
				sum = _mm_hadd_epi32(sum, sum);
				sum = _mm_hadd_epi32(sum, sum);
				return _mm_cvtsi128_si32(sum);
			}
        };

        template<>
        struct Operator<ArchType::sse4_1> : public OperatorImpl<ArchType::sse4_1, Operator<ArchType::sse4_1>>
        {
        };
#endif

#if defined(_MSC_VER) || defined(__AVX2__)
        template<size_t size>
        struct BestArchType<ArchType::avx2, size>
        {
            static constexpr ArchType value = size >= 8 ? ArchType::avx2 : ArchType::sse4_1;
        };

        template<>
        struct PacketTrait<ArchType::avx2>
        {
            static constexpr size_t size = 8;
            using IntPacket = __m256i;
            using FloatPacket = __m256;
        };

        template<class O>
		struct OperatorImpl<ArchType::avx2, O> : public OperatorBase<ArchType::avx2, O>
        {
            static STRONG_INLINE __m256 addf(__m256 a, __m256 b) { return _mm256_add_ps(a, b); }
            static STRONG_INLINE __m256 subf(__m256 a, __m256 b) { return _mm256_sub_ps(a, b); }
            static STRONG_INLINE __m256 mulf(__m256 a, __m256 b) { return _mm256_mul_ps(a, b); }
            static STRONG_INLINE __m256 divf(__m256 a, __m256 b) { return _mm256_div_ps(a, b); }
            static STRONG_INLINE __m256 maddf(__m256 a, __m256 b, __m256 c) { return _mm256_fmadd_ps(a, b, c); }
			static STRONG_INLINE __m256i set1i(int32_t a) { return _mm256_set1_epi32(a); }
            static STRONG_INLINE __m256 set1f(float a) { return _mm256_set1_ps(a); }
            static STRONG_INLINE __m256 loadf(const float* a) { return _mm256_loadu_ps(a); }
            static STRONG_INLINE void storef(float* a, __m256 b) { return _mm256_storeu_ps(a, b); }
            static STRONG_INLINE __m256 maxf(__m256 a, __m256 b) { return _mm256_max_ps(a, b); }
            static STRONG_INLINE __m256 minf(__m256 a, __m256 b) { return _mm256_min_ps(a, b); }
            static STRONG_INLINE __m256 floorf(__m256 a) { return _mm256_floor_ps(a); }
            static STRONG_INLINE __m256 zerof() { return _mm256_setzero_ps(); }
            static STRONG_INLINE __m256 negatef(__m256 a) { return subf(zerof(), a); }
            static STRONG_INLINE __m256i cast_to_int(__m256 a) { return _mm256_cvtps_epi32(a); }
			static STRONG_INLINE __m256 cast_to_float(__m256i a) { return _mm256_cvtepi32_ps(a); }
			static STRONG_INLINE __m256i reinterpret_as_int(__m256 a) { return _mm256_castps_si256(a); }
            static STRONG_INLINE __m256 reinterpret_as_float(__m256i a) { return _mm256_castsi256_ps(a); }
            template<int bit> static STRONG_INLINE __m256i sll(__m256i a) { return _mm256_slli_epi32(a, bit); }
			template<int bit> static STRONG_INLINE __m256i srl(__m256i a) { return _mm256_srli_epi32(a, bit); }
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

			static STRONG_INLINE __m256 band(__m256 a, __m256 b) { return _mm256_and_ps(a, b); }
			static STRONG_INLINE __m256i band(__m256i a, __m256i b) { return _mm256_and_si256(a, b); }

			static STRONG_INLINE __m256 bor(__m256 a, __m256 b) { return _mm256_or_ps(a, b); }
			static STRONG_INLINE __m256i bor(__m256i a, __m256i b) { return _mm256_or_si256(a, b); }

            static STRONG_INLINE __m256 select(__m256 mask, __m256 a, __m256 b) 
            { 
                return _mm256_blendv_ps(b, a, mask); 
            }
            static STRONG_INLINE __m256i select(__m256i mask, __m256i a, __m256i b) 
            { 
				return _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(b), _mm256_castsi256_ps(a), _mm256_castsi256_ps(mask)));
            }

			static STRONG_INLINE __m256 cmp_eq(__m256 a, __m256 b) { return _mm256_cmp_ps(a, b, _CMP_EQ_OQ); }
			static STRONG_INLINE __m256 cmp_le(__m256 a, __m256 b) { return _mm256_cmp_ps(a, b, _CMP_LE_OQ); }
			static STRONG_INLINE __m256 cmp_lt(__m256 a, __m256 b) { return _mm256_cmp_ps(a, b, _CMP_LT_OQ); }
			static STRONG_INLINE __m256 cmp_lt_or_nan(__m256 a, __m256 b) { return _mm256_cmp_ps(a, b, _CMP_NGE_UQ); }

            static STRONG_INLINE void load_transposed(const float* a, size_t stride,
                __m256& r0, __m256& r1, __m256& r2, __m256& r3, 
                __m256& r4, __m256& r5, __m256& r6, __m256& r7
            ) {
                __m256 t0, t1, t2, t3, t4, t5, t6, t7;

                r0 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[0 * stride + 0])), _mm_load_ps(&a[4 * stride + 0]), 1);
                r1 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[1 * stride + 0])), _mm_load_ps(&a[5 * stride + 0]), 1);
                r2 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[2 * stride + 0])), _mm_load_ps(&a[6 * stride + 0]), 1);
                r3 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[3 * stride + 0])), _mm_load_ps(&a[7 * stride + 0]), 1);
                r4 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[0 * stride + 4])), _mm_load_ps(&a[4 * stride + 4]), 1);
                r5 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[1 * stride + 4])), _mm_load_ps(&a[5 * stride + 4]), 1);
                r6 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[2 * stride + 4])), _mm_load_ps(&a[6 * stride + 4]), 1);
                r7 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(&a[3 * stride + 4])), _mm_load_ps(&a[7 * stride + 4]), 1);

                t0 = _mm256_unpacklo_ps(r0, r1);
                t1 = _mm256_unpackhi_ps(r0, r1);
                t2 = _mm256_unpacklo_ps(r2, r3);
                t3 = _mm256_unpackhi_ps(r2, r3);
                t4 = _mm256_unpacklo_ps(r4, r5);
                t5 = _mm256_unpackhi_ps(r4, r5);
                t6 = _mm256_unpacklo_ps(r6, r7);
                t7 = _mm256_unpackhi_ps(r6, r7);

                r0 = _mm256_shuffle_ps(t0, t2, 0x44);
                r1 = _mm256_shuffle_ps(t0, t2, 0xEE);
                r2 = _mm256_shuffle_ps(t1, t3, 0x44);
                r3 = _mm256_shuffle_ps(t1, t3, 0xEE);
                r4 = _mm256_shuffle_ps(t4, t6, 0x44);
                r5 = _mm256_shuffle_ps(t4, t6, 0xEE);
                r6 = _mm256_shuffle_ps(t5, t7, 0x44);
                r7 = _mm256_shuffle_ps(t5, t7, 0xEE);
            }

            static STRONG_INLINE void store_transposed(float* a, size_t stride,
                __m256 r0, __m256 r1, __m256 r2, __m256 r3,
                __m256 r4, __m256 r5, __m256 r6, __m256 r7
            )
            {
                __m256 t0 = _mm256_unpacklo_ps(r0, r1);
                __m256 t1 = _mm256_unpackhi_ps(r0, r1);
                __m256 t2 = _mm256_unpacklo_ps(r2, r3);
                __m256 t3 = _mm256_unpackhi_ps(r2, r3);
                __m256 t4 = _mm256_unpacklo_ps(r4, r5);
                __m256 t5 = _mm256_unpackhi_ps(r4, r5);
                __m256 t6 = _mm256_unpacklo_ps(r6, r7);
                __m256 t7 = _mm256_unpackhi_ps(r6, r7);

                r0 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
                r1 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
                r2 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
                r3 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
                r4 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
                r5 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
                r6 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
                r7 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));

                t0 = _mm256_permute2f128_ps(r0, r4, 0x20);
                t1 = _mm256_permute2f128_ps(r1, r5, 0x20);
                t2 = _mm256_permute2f128_ps(r2, r6, 0x20);
                t3 = _mm256_permute2f128_ps(r3, r7, 0x20);
                t4 = _mm256_permute2f128_ps(r0, r4, 0x31);
                t5 = _mm256_permute2f128_ps(r1, r5, 0x31);
                t6 = _mm256_permute2f128_ps(r2, r6, 0x31);
                t7 = _mm256_permute2f128_ps(r3, r7, 0x31);

				_mm256_store_ps(&a[0 * stride], t0);
				_mm256_store_ps(&a[1 * stride], t1);
				_mm256_store_ps(&a[2 * stride], t2);
				_mm256_store_ps(&a[3 * stride], t3);
				_mm256_store_ps(&a[4 * stride], t4);
				_mm256_store_ps(&a[5 * stride], t5);
				_mm256_store_ps(&a[6 * stride], t6);
				_mm256_store_ps(&a[7 * stride], t7);
            }

            static STRONG_INLINE int32_t dotprod(const uint8_t* a, const int8_t* b, size_t size)
            {
                __m256i pa, pb, acc = _mm256_setzero_si256();
                __m256i one16 = _mm256_set1_epi16(1), pt;
                for (size_t i = 0; i < size; i += 32)
                {
                    pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&a[i]));
                    pb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&b[i]));
					pt = _mm256_maddubs_epi16(pa, pb);
					acc = _mm256_add_epi32(acc, _mm256_madd_epi16(pt, one16));
                }
                // reduce sum of eight int32_t to one int32_t
                __m256i sum = _mm256_hadd_epi32(acc, acc);
                sum = _mm256_hadd_epi32(sum, sum);
				return _mm_cvtsi128_si32(_mm256_castsi256_si128(sum)) + _mm256_extract_epi32(sum, 4);
            }
        };

		template<>
		struct Operator<ArchType::avx2> : public OperatorImpl<ArchType::avx2, Operator<ArchType::avx2>>
		{
		};

        template<size_t size>
		struct BestArchType<ArchType::avx_vnni, size> : public BestArchType<ArchType::avx2, size>
        {
        };

        template<>
		struct PacketTrait<ArchType::avx_vnni> : public PacketTrait<ArchType::avx2>
		{
		};

        template<class O>
		struct OperatorImpl<ArchType::avx_vnni, O> : public OperatorImpl<ArchType::avx2, O>
		{
            static STRONG_INLINE int32_t dotprod(const uint8_t* a, const int8_t* b, size_t size)
            {
                __m256i pa, pb, acc = _mm256_setzero_si256();
                for (size_t i = 0; i < size; i += 32)
                {
                    pa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&a[i]));
                    pb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&b[i]));
                    acc = _mm256_dpbusd_epi32(acc, pa, pb);
                }
				// reduce sum of eight int32_t to one int32_t
				__m256i sum = _mm256_hadd_epi32(acc, acc);
				sum = _mm256_hadd_epi32(sum, sum);
                return _mm_cvtsi128_si32(_mm256_castsi256_si128(sum)) + _mm256_extract_epi32(sum, 4);
            }
		};

		template<>
		struct Operator<ArchType::avx_vnni> : public OperatorImpl<ArchType::avx_vnni, Operator<ArchType::avx_vnni>>
		{
		};
#endif

#if defined(_MSC_VER) || defined(__AVX512F__) || defined(__AVX512BW__)
        template<size_t size>
        struct BestArchType<ArchType::avx512bw, size>
        {
            static constexpr ArchType value = size >= 16 ? ArchType::avx512bw : ArchType::avx2;
        };

        template<>
        struct PacketTrait<ArchType::avx512bw>
        {
            static constexpr size_t size = 16;
            using IntPacket = __m512i;
            using FloatPacket = __m512;
        };

        template<class O>
		struct OperatorImpl<ArchType::avx512bw, O> : public OperatorBase<ArchType::avx512bw, O>
        {
            static STRONG_INLINE __m512 addf(__m512 a, __m512 b) { return _mm512_add_ps(a, b); }
            static STRONG_INLINE __m512 subf(__m512 a, __m512 b) { return _mm512_sub_ps(a, b); }
            static STRONG_INLINE __m512 mulf(__m512 a, __m512 b) { return _mm512_mul_ps(a, b); }
            static STRONG_INLINE __m512 divf(__m512 a, __m512 b) { return _mm512_div_ps(a, b); }
            static STRONG_INLINE __m512 maddf(__m512 a, __m512 b, __m512 c) { return _mm512_fmadd_ps(a, b, c); }
			static STRONG_INLINE __m512i set1i(int32_t a) { return _mm512_set1_epi32(a); }
            static STRONG_INLINE __m512 set1f(float a) { return _mm512_set1_ps(a); }
            static STRONG_INLINE __m512 loadf(const float* a) { return _mm512_loadu_ps(a); }
            static STRONG_INLINE void storef(float* a, __m512 b) { return _mm512_storeu_ps(a, b); }
            static STRONG_INLINE __m512 maxf(__m512 a, __m512 b) { return _mm512_max_ps(a, b); }
            static STRONG_INLINE __m512 minf(__m512 a, __m512 b) { return _mm512_min_ps(a, b); }
            static STRONG_INLINE __m512 floorf(__m512 a) { return _mm512_floor_ps(a); }
            static STRONG_INLINE __m512 zerof() { return _mm512_setzero_ps(); }
            static STRONG_INLINE __m512 negatef(__m512 a) { return subf(zerof(), a); }
            static STRONG_INLINE __m512i cast_to_int(__m512 a) { return _mm512_cvtps_epi32(a); }
			static STRONG_INLINE __m512 cast_to_float(__m512i a) { return _mm512_cvtepi32_ps(a); }
			static STRONG_INLINE __m512i reinterpret_as_int(__m512 a) { return _mm512_castps_si512(a); }
            static STRONG_INLINE __m512 reinterpret_as_float(__m512i a) { return _mm512_castsi512_ps(a); }
            template<int bit> static STRONG_INLINE __m512i sll(__m512i a) { return _mm512_slli_epi32(a, bit); }
			template<int bit> static STRONG_INLINE __m512i srl(__m512i a) { return _mm512_srli_epi32(a, bit); }
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

			static STRONG_INLINE __m512 band(__m512 a, __m512 b) { return _mm512_and_ps(a, b); }
			static STRONG_INLINE __m512i band(__m512i a, __m512i b) { return _mm512_and_si512(a, b); }

			static STRONG_INLINE __m512 bor(__m512 a, __m512 b) { return _mm512_or_ps(a, b); }
			static STRONG_INLINE __m512i bor(__m512i a, __m512i b) { return _mm512_or_si512(a, b); }

			static STRONG_INLINE __m512 select(__m512 mask, __m512 a, __m512 b) 
            {
                __mmask16 mask16 = _mm512_cmp_epi32_mask(_mm512_castps_si512(mask), _mm512_setzero_epi32(), _MM_CMPINT_EQ);
                return _mm512_mask_blend_ps(mask16, a, b);
            }
            static STRONG_INLINE __m512i select(__m512i mask, __m512i a, __m512i b)
            {
				__mmask16 mask16 = _mm512_cmp_epi32_mask(mask, _mm512_setzero_si512(), _MM_CMPINT_EQ);
				return _mm512_mask_blend_epi32(mask16, a, b);
            }

			static STRONG_INLINE __m512 cmp_eq(__m512 a, __m512 b) 
            { 
                __mmask16 mask = _mm512_cmp_ps_mask(a, b, _CMP_EQ_OQ);
                return _mm512_castsi512_ps(_mm512_mask_set1_epi32(_mm512_set1_epi32(0), mask, 0xffffffffu));
            }
			static STRONG_INLINE __m512 cmp_le(__m512 a, __m512 b)
			{
				__mmask16 mask = _mm512_cmp_ps_mask(a, b, _CMP_LE_OQ);
				return _mm512_castsi512_ps(_mm512_mask_set1_epi32(_mm512_set1_epi32(0), mask, 0xffffffffu));
			}
            static STRONG_INLINE __m512 cmp_lt(__m512 a, __m512 b)
            {
                __mmask16 mask = _mm512_cmp_ps_mask(a, b, _CMP_LT_OQ);
                return _mm512_castsi512_ps(_mm512_mask_set1_epi32(_mm512_set1_epi32(0), mask, 0xffffffffu));
            }
			static STRONG_INLINE __m512 cmp_lt_or_nan(__m512 a, __m512 b)
			{
				__mmask16 mask = _mm512_cmp_ps_mask(a, b, _CMP_NGE_UQ);
				return _mm512_castsi512_ps(_mm512_mask_set1_epi32(_mm512_set1_epi32(0), mask, 0xffffffffu));
			}

            static STRONG_INLINE int32_t dotprod(const uint8_t* a, const int8_t* b, size_t size)
            {
                __m512i pa, pb, acc = _mm512_setzero_si512();
				__m512i one16 = _mm512_set1_epi16(1), pt;
                for (size_t i = 0; i < size; i += 64)
                {
                    pa = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&a[i]));
                    pb = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&b[i]));
					pt = _mm512_maddubs_epi16(pa, pb);
					acc = _mm512_add_epi32(acc, _mm512_madd_epi16(pt, one16));
                }
				return _mm512_reduce_add_epi32(acc);
            }
        };

		template<>
		struct Operator<ArchType::avx512bw> : public OperatorImpl<ArchType::avx512bw, Operator<ArchType::avx512bw>>
		{
		};
        
        template<size_t size>
        struct BestArchType<ArchType::avx512vnni, size>
        {
            static constexpr ArchType value = size >= 16 ? ArchType::avx512vnni : ArchType::avx_vnni;
        };

        template<>
		struct PacketTrait<ArchType::avx512vnni> : public PacketTrait<ArchType::avx512bw>
		{
		};

		template<class O>
		struct OperatorImpl<ArchType::avx512vnni, O> : public OperatorImpl<ArchType::avx512bw, O>
		{
			static STRONG_INLINE int32_t dotprod(const uint8_t* a, const int8_t* b, size_t size)
			{
				__m512i pa, pb, acc = _mm512_setzero_si512();
				for (size_t i = 0; i < size; i += 64)
				{
					pa = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&a[i]));
					pb = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(&b[i]));
					acc = _mm512_dpbusd_epi32(acc, pa, pb);
				}
				return _mm512_reduce_add_epi32(acc);
			}
		};

		template<>
		struct Operator<ArchType::avx512vnni> : public OperatorImpl<ArchType::avx512vnni, Operator<ArchType::avx512vnni>>
		{
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

        template<class O>
        struct OperatorImpl<ArchType::neon, O> : public OperatorBase<ArchType::neon, O>
        {
            static STRONG_INLINE float32x4_t addf(float32x4_t a, float32x4_t b) { return vaddq_f32(a, b); }
            static STRONG_INLINE float32x4_t subf(float32x4_t a, float32x4_t b) { return vsubq_f32(a, b); }
            static STRONG_INLINE float32x4_t mulf(float32x4_t a, float32x4_t b) { return vmulq_f32(a, b); }
            static STRONG_INLINE float32x4_t divf(float32x4_t a, float32x4_t b) { return vdivq_f32(a, b); }
            static STRONG_INLINE float32x4_t maddf(float32x4_t a, float32x4_t b, float32x4_t c) { return addf(mulf(a, b), c); }
            static STRONG_INLINE int32x4_t set1i(int32_t a) { return vdupq_n_s32(a); }
            static STRONG_INLINE float32x4_t set1f(float a) { return vdupq_n_f32(a); }
            static STRONG_INLINE float32x4_t loadf(const float* a) { return vld1q_f32(a); }
            static STRONG_INLINE void storef(float* a, float32x4_t b) { return vst1q_f32(a, b); }
            static STRONG_INLINE float32x4_t maxf(float32x4_t a, float32x4_t b) { return vmaxq_f32(a, b); }
            static STRONG_INLINE float32x4_t minf(float32x4_t a, float32x4_t b) { return vminq_f32(a, b); }
            static STRONG_INLINE float32x4_t floorf(float32x4_t a) { return vrndmq_f32(a); }
            static STRONG_INLINE float32x4_t zerof() { return set1f(0.f); }
            static STRONG_INLINE float32x4_t negatef(float32x4_t a) { return subf(zerof(), a); }
            static STRONG_INLINE int32x4_t cast_to_int(float32x4_t a) { return vcvtq_s32_f32(a); }
            static STRONG_INLINE float32x4_t cast_to_float(int32x4_t a) { return vcvtq_f32_s32(a); }
            static STRONG_INLINE int32x4_t reinterpret_as_int(float32x4_t a) { return vreinterpretq_s32_f32(a); }
            static STRONG_INLINE float32x4_t reinterpret_as_float(int32x4_t a) { return vreinterpretq_f32_s32(a); }
            template<int bit> static STRONG_INLINE int32x4_t sll(int32x4_t a) { return vshlq_n_s32(a, bit); }
            template<int bit> static STRONG_INLINE int32x4_t srl(int32x4_t a) { return vshrq_n_s32(a, bit); }
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

			static STRONG_INLINE int32x4_t band(int32x4_t a, int32x4_t b) { return vandq_s32(a, b); }
            static STRONG_INLINE float32x4_t band(float32x4_t a, float32x4_t b) { return reinterpret_as_float(band(reinterpret_as_int(a), reinterpret_as_int(b))); }

			static STRONG_INLINE int32x4_t bor(int32x4_t a, int32x4_t b) { return vorrq_s32(a, b); }
			static STRONG_INLINE float32x4_t bor(float32x4_t a, float32x4_t b) { return reinterpret_as_float(bor(reinterpret_as_int(a), reinterpret_as_int(b))); }

			static STRONG_INLINE int32x4_t select(int32x4_t mask, int32x4_t a, int32x4_t b) 
            {
                return vbslq_s32(vreinterpretq_u32_s32(mask), a, b);
            }
            static STRONG_INLINE float32x4_t select(float32x4_t mask, float32x4_t a, float32x4_t b)
            {
                return vbslq_f32(vreinterpretq_u32_f32(mask), a, b);
            }

			static STRONG_INLINE float32x4_t cmp_eq(float32x4_t a, float32x4_t b) 
            {
                return vreinterpretq_f32_u32(vceqq_f32(a, b)); // Compare equal
            }

            static STRONG_INLINE float32x4_t cmp_le(float32x4_t a, float32x4_t b) 
            {
                return vreinterpretq_f32_u32(vcleq_f32(a, b)); // Compare less than or equal
            }

            static STRONG_INLINE float32x4_t cmp_lt(float32x4_t a, float32x4_t b) 
            {
                return vreinterpretq_f32_u32(vcltq_f32(a, b)); // Compare less than
            }

            static STRONG_INLINE float32x4_t cmp_lt_or_nan(float32x4_t a, float32x4_t b) 
            {
                return vreinterpretq_f32_u32(vmvnq_u32(vcleq_f32(b, a)));
            }

            static STRONG_INLINE int32_t dotprod(const uint8_t* a, const int8_t* b, size_t size)
            {
                int32x4_t sum = vdupq_n_s32(0);
				uint16x8_t pa;
				int8x16_t pb;
                for (size_t i = 0; i < size; i += 16)
                {
					//
                }
				sum = vpaddq_s32(sum, sum);
				sum = vpaddq_s32(sum, sum);
				return vgetq_lane_s32(sum, 0);
            }

            static STRONG_INLINE int32_t dotprod(const int8_t* a, const int8_t* b, size_t size)
            {
                int32x4_t sum = vdupq_n_s32(0);
				int8x16_t pa, pb;
                for (size_t i = 0; i < size; i += 16)
                {
					pa = vld1q_s8(a + i);
					pb = vld1q_s8(b + i);
					sum = vpadalq_s16(sum, vmull_s8(vget_low_s8(pb), vget_low_s8(pa)));
					sum = vpadalq_s16(sum, vmull_s8(vget_high_s8(pb), vget_high_s8(pa)));
                }
                sum = vpaddq_s32(sum, sum);
                sum = vpaddq_s32(sum, sum);
                return vgetq_lane_s32(sum, 0);
            }
        };

		template<>
        struct Operator<ArchType::neon> : public OperatorImpl<ArchType::neon, Operator<ArchType::neon>>
        {
        };
    }
}
#endif


#ifdef __GNUC__
    #pragma GCC diagnostic pop
#endif
