#pragma once

/*
* This code was rewritten in C++ based on the code from https://github.com/IlyaGrebnov/libsais, the SAIS library written in C & OpenMP.
*/

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <climits>

#include "mp_utils.hpp"


#if defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#elif defined(_MSC_VER) || defined(__INTEL_COMPILER)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

#if defined(__has_builtin)
#if __has_builtin(__builtin_prefetch)
#define HAS_BUILTIN_PREFECTCH
#endif
#elif defined(__GNUC__) && (((__GNUC__ == 3) && (__GNUC_MINOR__ >= 2)) || (__GNUC__ >= 4))
#define HAS_BUILTIN_PREFECTCH
#endif 

#if defined(HAS_BUILTIN_PREFECTCH)
#define prefetchr(address) __builtin_prefetch((const void *)(address), 0, 3)
#define prefetchw(address) __builtin_prefetch((const void *)(address), 1, 3)
#elif defined (_M_IX86) || defined (_M_AMD64)
#include <intrin.h>
#define prefetchr(address) _mm_prefetch((const char*)(address), _MM_HINT_T0)
#define prefetchw(address) _m_prefetchw((const void *)(address))
#elif defined (_M_ARM)
#include <intrin.h>
#define prefetchr(address) __prefetch((const void *)(address))
#define prefetchw(address) __prefetchw((const void *)(address))
#elif defined (_M_ARM64)
#include <intrin.h>
#define prefetchr(address) __prefetch2((const void *)(address), 0)
#define prefetchw(address) __prefetch2((const void *)(address), 16)
#else
#define prefetchr(x)
#define prefetchw(x)
#endif

namespace sais
{
#define UNBWT_FASTBITS                  (17)

template<class Ty1, class Ty2>
inline constexpr auto buckets_index2(Ty1 c, Ty2 s) -> decltype(c + s)
{
    return (c << 1) + s;
}

template<class Ty1, class Ty2>
inline constexpr auto buckets_index4(Ty1 c, Ty2 s) -> decltype(c + s)
{
    return (c << 2) + s;
}

inline uint8_t to_unsigned(int8_t i)
{
    return (uint8_t)i;
}

inline uint16_t to_unsigned(int16_t i)
{
    return (uint16_t)i;
}

inline uint32_t to_unsigned(int32_t i)
{
    return (uint32_t)i;
}

inline uint64_t to_unsigned(int64_t i)
{
    return (uint64_t)i;
}

inline void* align_up(const void* address, size_t alignment)
{
    return (void*)((((ptrdiff_t)address) + ((ptrdiff_t)alignment) - 1) & (-((ptrdiff_t)alignment)));
}

inline void* alloc_aligned(size_t size, size_t alignment)
{
    void* address = malloc(size + sizeof(short) + alignment - 1);
    if (address != nullptr)
    {
        void* aligned_address = align_up((void*)((ptrdiff_t)address + (ptrdiff_t)(sizeof(short))), alignment);
        ((short*)aligned_address)[-1] = (short)((ptrdiff_t)aligned_address - (ptrdiff_t)address);

        return aligned_address;
    }

    return nullptr;
}

inline void free_aligned(void* aligned_address)
{
    if (aligned_address != nullptr)
    {
        free((void*)((ptrdiff_t)aligned_address - ((short*)aligned_address)[-1]));
    }
}


template<class AlphabetTy, class SaTy>
class SaisImpl
{
    static constexpr SaTy saint_bit = sizeof(SaTy) * 8;
    static constexpr SaTy saint_min = std::numeric_limits<SaTy>::min();
    static constexpr SaTy saint_max = std::numeric_limits<SaTy>::max();
    static constexpr SaTy alphabet_size = (SaTy)1 << (sizeof(AlphabetTy) * 8);
    static constexpr SaTy suffix_group_bit = saint_bit - 1;
    static constexpr SaTy suffix_group_marker = (SaTy)1 << (suffix_group_bit - 1);
    static constexpr size_t per_thread_cache_size = 24576;

    using fast_sint_t = ptrdiff_t;
    using fast_uint_t = size_t;

    struct ThreadCache
    {
        SaTy symbol;
        SaTy index;
    };

    union ThreadState
    {
        struct
        {
            fast_sint_t position;
            fast_sint_t count;

            fast_sint_t m;
            fast_sint_t last_lms_suffix;

            SaTy* buckets;
            ThreadCache* cache;
        } state;

        uint8_t padding[64];
    };

    struct Context
    {
        SaTy* buckets;
        ThreadState* thread_state;
        mp::ThreadPool* pool = nullptr;
    };

    struct UnbwtContext
    {
        SaTy* bucket2;
        AlphabetTy* fastbits;
        SaTy* buckets;
        mp::ThreadPool* pool = nullptr;
    };

    static ThreadState* alloc_thread_state(mp::ThreadPool* pool)
    {
        ThreadState* RESTRICT thread_state = (ThreadState*)alloc_aligned(mp::getPoolSize(pool) * sizeof(ThreadState), 4096);
        SaTy* RESTRICT thread_buckets = (SaTy*)alloc_aligned(mp::getPoolSize(pool) * 4 * alphabet_size * sizeof(SaTy), 4096);
        ThreadCache* RESTRICT thread_cache = (ThreadCache*)alloc_aligned(mp::getPoolSize(pool) * per_thread_cache_size * sizeof(ThreadCache), 4096);

        if (thread_state != nullptr && thread_buckets != nullptr && thread_cache != nullptr)
        {
            fast_sint_t t;
            for (t = 0; t < mp::getPoolSize(pool); ++t)
            {
                thread_state[t].state.buckets = thread_buckets;   thread_buckets += 4 * alphabet_size;
                thread_state[t].state.cache = thread_cache;     thread_cache += per_thread_cache_size;
            }

            return thread_state;
        }

        free_aligned(thread_cache);
        free_aligned(thread_buckets);
        free_aligned(thread_state);
        return nullptr;
    }

    static void free_thread_state(ThreadState* thread_state)
    {
        if (thread_state != nullptr)
        {
            free_aligned(thread_state[0].state.cache);
            free_aligned(thread_state[0].state.buckets);
            free_aligned(thread_state);
        }
    }

    static Context* create_ctx_main(mp::ThreadPool* pool)
    {
        Context* RESTRICT ctx = (Context*)alloc_aligned(sizeof(Context), 64);
        SaTy* RESTRICT buckets = (SaTy*)alloc_aligned(8 * alphabet_size * sizeof(SaTy), 4096);
        ThreadState* RESTRICT thread_state = mp::getPoolSize(pool) > 1 ? alloc_thread_state(pool) : nullptr;

        if (ctx != nullptr && buckets != nullptr && (thread_state != nullptr || mp::getPoolSize(pool) == 1))
        {
            ctx->buckets = buckets;
            ctx->pool = pool;
            ctx->thread_state = thread_state;

            return ctx;
        }

        free_thread_state(thread_state);
        free_aligned(buckets);
        free_aligned(ctx);
        return nullptr;
    }

    static void free_ctx_main(Context* ctx)
    {
        if (ctx != nullptr)
        {
            free_thread_state(ctx->thread_state);
            free_aligned(ctx->buckets);
            free_aligned(ctx);
        }
    }

    static SaTy count_negative_marked_suffixes(SaTy* RESTRICT SA, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        SaTy count = 0;

        fast_sint_t i; for (i = omp_block_start; i < omp_block_start + omp_block_size; ++i) { count += (SA[i] < 0); }

        return count;
    }

    static SaTy count_zero_marked_suffixes(SaTy* RESTRICT SA, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        SaTy count = 0;

        fast_sint_t i; for (i = omp_block_start; i < omp_block_start + omp_block_size; ++i) { count += (SA[i] == 0); }

        return count;
    }

    static void place_cached_suffixes(SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchr(&cache[i + 2 * prefetch_distance]);

            prefetchw(&SA[cache[i + prefetch_distance + 0].symbol]);
            prefetchw(&SA[cache[i + prefetch_distance + 1].symbol]);
            prefetchw(&SA[cache[i + prefetch_distance + 2].symbol]);
            prefetchw(&SA[cache[i + prefetch_distance + 3].symbol]);

            SA[cache[i + 0].symbol] = cache[i + 0].index;
            SA[cache[i + 1].symbol] = cache[i + 1].index;
            SA[cache[i + 2].symbol] = cache[i + 2].index;
            SA[cache[i + 3].symbol] = cache[i + 3].index;
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            SA[cache[i].symbol] = cache[i].index;
        }
    }

    static void compact_and_place_cached_suffixes(SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j, l;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 3, l = omp_block_start; i < j; i += 4)
        {
            prefetchw(&cache[i + prefetch_distance]);

            cache[l] = cache[i + 0]; l += cache[l].symbol >= 0;
            cache[l] = cache[i + 1]; l += cache[l].symbol >= 0;
            cache[l] = cache[i + 2]; l += cache[l].symbol >= 0;
            cache[l] = cache[i + 3]; l += cache[l].symbol >= 0;
        }

        for (j += 3; i < j; i += 1)
        {
            cache[l] = cache[i]; l += cache[l].symbol >= 0;
        }

        place_cached_suffixes(SA, cache, omp_block_start, l - omp_block_start);
    }

    static void accumulate_counts_s32_2(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s]; }
    }

    static void accumulate_counts_s32_3(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        SaTy* RESTRICT bucket02 = bucket01 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s] + bucket02[s]; }
    }

    static void accumulate_counts_s32_4(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        SaTy* RESTRICT bucket02 = bucket01 - bucket_stride;
        SaTy* RESTRICT bucket03 = bucket02 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s] + bucket02[s] + bucket03[s]; }
    }

    static void accumulate_counts_s32_5(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        SaTy* RESTRICT bucket02 = bucket01 - bucket_stride;
        SaTy* RESTRICT bucket03 = bucket02 - bucket_stride;
        SaTy* RESTRICT bucket04 = bucket03 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s] + bucket02[s] + bucket03[s] + bucket04[s]; }
    }

    static void accumulate_counts_s32_6(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        SaTy* RESTRICT bucket02 = bucket01 - bucket_stride;
        SaTy* RESTRICT bucket03 = bucket02 - bucket_stride;
        SaTy* RESTRICT bucket04 = bucket03 - bucket_stride;
        SaTy* RESTRICT bucket05 = bucket04 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s] + bucket02[s] + bucket03[s] + bucket04[s] + bucket05[s]; }
    }

    static void accumulate_counts_s32_7(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        SaTy* RESTRICT bucket02 = bucket01 - bucket_stride;
        SaTy* RESTRICT bucket03 = bucket02 - bucket_stride;
        SaTy* RESTRICT bucket04 = bucket03 - bucket_stride;
        SaTy* RESTRICT bucket05 = bucket04 - bucket_stride;
        SaTy* RESTRICT bucket06 = bucket05 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s] + bucket02[s] + bucket03[s] + bucket04[s] + bucket05[s] + bucket06[s]; }
    }

    static void accumulate_counts_s32_8(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        SaTy* RESTRICT bucket02 = bucket01 - bucket_stride;
        SaTy* RESTRICT bucket03 = bucket02 - bucket_stride;
        SaTy* RESTRICT bucket04 = bucket03 - bucket_stride;
        SaTy* RESTRICT bucket05 = bucket04 - bucket_stride;
        SaTy* RESTRICT bucket06 = bucket05 - bucket_stride;
        SaTy* RESTRICT bucket07 = bucket06 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s] + bucket02[s] + bucket03[s] + bucket04[s] + bucket05[s] + bucket06[s] + bucket07[s]; }
    }

    static void accumulate_counts_s32_9(SaTy* RESTRICT bucket00, fast_sint_t bucket_size, fast_sint_t bucket_stride)
    {
        SaTy* RESTRICT bucket01 = bucket00 - bucket_stride;
        SaTy* RESTRICT bucket02 = bucket01 - bucket_stride;
        SaTy* RESTRICT bucket03 = bucket02 - bucket_stride;
        SaTy* RESTRICT bucket04 = bucket03 - bucket_stride;
        SaTy* RESTRICT bucket05 = bucket04 - bucket_stride;
        SaTy* RESTRICT bucket06 = bucket05 - bucket_stride;
        SaTy* RESTRICT bucket07 = bucket06 - bucket_stride;
        SaTy* RESTRICT bucket08 = bucket07 - bucket_stride;
        fast_sint_t s; for (s = 0; s < bucket_size; s += 1) { bucket00[s] = bucket00[s] + bucket01[s] + bucket02[s] + bucket03[s] + bucket04[s] + bucket05[s] + bucket06[s] + bucket07[s] + bucket08[s]; }
    }

    static void accumulate_counts_s32(SaTy* RESTRICT buckets, fast_sint_t bucket_size, fast_sint_t bucket_stride, fast_sint_t num_buckets)
    {
        while (num_buckets >= 9)
        {
            accumulate_counts_s32_9(buckets - (num_buckets - 9) * bucket_stride, bucket_size, bucket_stride); num_buckets -= 8;
        }

        switch (num_buckets)
        {
        case 1: break;
        case 2: accumulate_counts_s32_2(buckets, bucket_size, bucket_stride); break;
        case 3: accumulate_counts_s32_3(buckets, bucket_size, bucket_stride); break;
        case 4: accumulate_counts_s32_4(buckets, bucket_size, bucket_stride); break;
        case 5: accumulate_counts_s32_5(buckets, bucket_size, bucket_stride); break;
        case 6: accumulate_counts_s32_6(buckets, bucket_size, bucket_stride); break;
        case 7: accumulate_counts_s32_7(buckets, bucket_size, bucket_stride); break;
        case 8: accumulate_counts_s32_8(buckets, bucket_size, bucket_stride); break;
        }
    }

    static void gather_lms_suffixes_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, fast_sint_t m, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        if (omp_block_size > 0)
        {
            const fast_sint_t prefetch_distance = 128;

            fast_sint_t i, j = omp_block_start + omp_block_size, c0 = T[omp_block_start + omp_block_size - 1], c1 = -1;

            while (j < n && (c1 = T[j]) == c0) { ++j; }

            fast_uint_t s = c0 >= c1;

            for (i = omp_block_start + omp_block_size - 2, j = omp_block_start + 3; i >= j; i -= 4)
            {
                prefetchr(&T[i - prefetch_distance]);

                c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
                c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 0); m -= ((s & 3) == 1);
                c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 1); m -= ((s & 3) == 1);
                c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 2); m -= ((s & 3) == 1);
            }

            for (j -= 3; i >= j; i -= 1)
            {
                c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
            }

            SA[m] = (SaTy)(i + 1);
        }
    }

    static void gather_lms_suffixes_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            fast_sint_t t, m = 0; for (t = num_threads - 1; t > id; --t) { m += thread_state[t].state.m; }

            gather_lms_suffixes_16u(T, SA, n, (fast_sint_t)n - 1 - m, omp_block_start, omp_block_size);

            if (num_threads > 1)
            {
                mp::barrier(barrier);

                if (pool && thread_state[id].state.m > 0)
                {
                    SA[(fast_sint_t)n - 1 - m] = (SaTy)thread_state[id].state.last_lms_suffix;
                }
            }
        }, mp::ParallelCond{n >= 65536});
    }

    static SaTy gather_lms_suffixes_32s(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy             i = n - 2;
        SaTy             m = n - 1;
        fast_uint_t           s = 1;
        fast_sint_t           c0 = T[n - 1];
        fast_sint_t           c1 = 0;

        for (; i >= 3; i -= 4)
        {
            prefetchr(&T[i - prefetch_distance]);

            c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = i + 1; m -= ((s & 3) == 1);
            c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = i - 0; m -= ((s & 3) == 1);
            c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = i - 1; m -= ((s & 3) == 1);
            c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = i - 2; m -= ((s & 3) == 1);
        }

        for (; i >= 0; i -= 1)
        {
            c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = i + 1; m -= ((s & 3) == 1);
        }

        return n - 1 - m;
    }

    static SaTy gather_compacted_lms_suffixes_32s(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy             i = n - 2;
        SaTy             m = n - 1;
        fast_uint_t           s = 1;
        fast_sint_t           c0 = T[n - 1];
        fast_sint_t           c1 = 0;

        for (; i >= 3; i -= 4)
        {
            prefetchr(&T[i - prefetch_distance]);

            c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = i + 1; m -= ((fast_sint_t)(s & 3) == (c0 >= 0));
            c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = i - 0; m -= ((fast_sint_t)(s & 3) == (c1 >= 0));
            c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = i - 1; m -= ((fast_sint_t)(s & 3) == (c0 >= 0));
            c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = i - 2; m -= ((fast_sint_t)(s & 3) == (c1 >= 0));
        }

        for (; i >= 0; i -= 1)
        {
            c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = i + 1; m -= ((fast_sint_t)(s & 3) == (c1 >= 0));
        }

        return n - 1 - m;
    }

    static void count_lms_suffixes_32s_4k(const SaTy* RESTRICT T, SaTy n, SaTy k, SaTy* RESTRICT buckets)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, 4 * (size_t)k * sizeof(SaTy));

        SaTy             i = n - 2;
        fast_uint_t           s = 1;
        fast_sint_t           c0 = T[n - 1];
        fast_sint_t           c1 = 0;

        for (; i >= prefetch_distance + 3; i -= 4)
        {
            prefetchr(&T[i - 2 * prefetch_distance]);

            prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 0], 0)]);
            prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 1], 0)]);
            prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 2], 0)]);
            prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 3], 0)]);

            c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;

            c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;

            c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;

            c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;
        }

        for (; i >= 0; i -= 1)
        {
            c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;
        }

        buckets[buckets_index4((fast_uint_t)c0, (s << 1) & 3)]++;
    }

    static void count_lms_suffixes_32s_2k(const SaTy* RESTRICT T, SaTy n, SaTy k, SaTy* RESTRICT buckets)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, 2 * (size_t)k * sizeof(SaTy));

        SaTy             i = n - 2;
        fast_uint_t           s = 1;
        fast_sint_t           c0 = T[n - 1];
        fast_sint_t           c1 = 0;

        for (; i >= prefetch_distance + 3; i -= 4)
        {
            prefetchr(&T[i - 2 * prefetch_distance]);

            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 0], 0)]);
            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 1], 0)]);
            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 2], 0)]);
            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 3], 0)]);

            c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

            c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;

            c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

            c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
        }

        for (; i >= 0; i -= 1)
        {
            c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
        }

        buckets[buckets_index2((fast_uint_t)c0, 0)]++;
    }


    static void count_compacted_lms_suffixes_32s_2k(const SaTy* RESTRICT T, SaTy n, SaTy k, SaTy* RESTRICT buckets)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, 2 * (size_t)k * sizeof(SaTy));

        SaTy             i = n - 2;
        fast_uint_t           s = 1;
        fast_sint_t           c0 = T[n - 1];
        fast_sint_t           c1 = 0;

        for (; i >= prefetch_distance + 3; i -= 4)
        {
            prefetchr(&T[i - 2 * prefetch_distance]);

            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 0] & saint_max, 0)]);
            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 1] & saint_max, 0)]);
            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 2] & saint_max, 0)]);
            prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 3] & saint_max, 0)]);

            c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            c0 &= saint_max; buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

            c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            c1 &= saint_max; buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;

            c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            c0 &= saint_max; buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

            c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            c1 &= saint_max; buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
        }

        for (; i >= 0; i -= 1)
        {
            c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            c1 &= saint_max; buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
        }

        c0 &= saint_max; buckets[buckets_index2((fast_uint_t)c0, 0)]++;
    }


    static SaTy count_and_gather_lms_suffixes_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        std::memset(buckets, 0, 4 * alphabet_size * sizeof(SaTy));

        fast_sint_t m = omp_block_start + omp_block_size - 1;

        if (omp_block_size > 0)
        {
            const fast_sint_t prefetch_distance = 128;

            fast_sint_t i, j = m + 1, c0 = T[m], c1 = -1;

            while (j < n && (c1 = T[j]) == c0) { ++j; }

            fast_uint_t s = c0 >= c1;

            for (i = m - 1, j = omp_block_start + 3; i >= j; i -= 4)
            {
                prefetchr(&T[i - prefetch_distance]);

                c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;

                c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 0); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;

                c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 1); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;

                c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 2); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;
            }

            for (j -= 3; i >= j; i -= 1)
            {
                c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;
            }

            c1 = (i >= 0) ? T[i] : -1; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
            buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;
        }

        return (SaTy)(omp_block_start + omp_block_size - 1 - m);
    }

    static SaTy count_and_gather_lms_suffixes_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy m = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            if (num_threads == 1)
            {
                m = count_and_gather_lms_suffixes_16u(T, SA, n, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.position = omp_block_start + omp_block_size;
                thread_state[id].state.m = count_and_gather_lms_suffixes_16u(T, SA, n, thread_state[id].state.buckets, omp_block_start, omp_block_size);

                if (thread_state[id].state.m > 0)
                {
                    thread_state[id].state.last_lms_suffix = SA[thread_state[id].state.position - 1];
                }
            }
        }, mp::parallelFinal([&]()
        {
            std::memset(buckets, 0, 4 * alphabet_size * sizeof(SaTy));
            for (fast_sint_t t = mp::getPoolSize(pool) - 1; t >= 0; --t)
            {
                m += (SaTy)thread_state[t].state.m;

                if (t != mp::getPoolSize(pool) - 1 && thread_state[t].state.m > 0)
                {
                    std::memcpy(&SA[n - m], &SA[thread_state[t].state.position - thread_state[t].state.m], (size_t)thread_state[t].state.m * sizeof(SaTy));
                }

                {
                    SaTy* RESTRICT temp_bucket = thread_state[t].state.buckets;
                    fast_sint_t s; for (s = 0; s < 4 * alphabet_size; s += 1) { SaTy A = buckets[s], B = temp_bucket[s]; buckets[s] = A + B; temp_bucket[s] = A; }
                }
            }
        }), mp::ParallelCond{n >= 65536});
        return m;
    }

    static SaTy count_and_gather_lms_suffixes_32s_4k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        std::memset(buckets, 0, 4 * (size_t)k * sizeof(SaTy));

        fast_sint_t m = omp_block_start + omp_block_size - 1;

        if (omp_block_size > 0)
        {
            const fast_sint_t prefetch_distance = 32;

            fast_sint_t i, j = m + 1, c0 = T[m], c1 = -1;

            while (j < n && (c1 = T[j]) == c0) { ++j; }

            fast_uint_t s = c0 >= c1;

            for (i = m - 1, j = omp_block_start + prefetch_distance + 3; i >= j; i -= 4)
            {
                prefetchr(&T[i - 2 * prefetch_distance]);

                prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 0], 0)]);
                prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 1], 0)]);
                prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 2], 0)]);
                prefetchw(&buckets[buckets_index4(T[i - prefetch_distance - 3], 0)]);

                c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;

                c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 0); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;

                c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 1); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;

                c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 2); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;
            }

            for (j -= prefetch_distance + 3; i >= j; i -= 1)
            {
                c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]++;
            }

            c1 = (i >= 0) ? T[i] : -1; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
            buckets[buckets_index4((fast_uint_t)c0, s & 3)]++;
        }

        return (SaTy)(omp_block_start + omp_block_size - 1 - m);
    }

    static SaTy count_and_gather_lms_suffixes_32s_2k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        std::memset(buckets, 0, 2 * (size_t)k * sizeof(SaTy));

        fast_sint_t m = omp_block_start + omp_block_size - 1;

        if (omp_block_size > 0)
        {
            const fast_sint_t prefetch_distance = 32;

            fast_sint_t i, j = m + 1, c0 = T[m], c1 = -1;

            while (j < n && (c1 = T[j]) == c0) { ++j; }

            fast_uint_t s = c0 >= c1;

            for (i = m - 1, j = omp_block_start + prefetch_distance + 3; i >= j; i -= 4)
            {
                prefetchr(&T[i - 2 * prefetch_distance]);

                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 0], 0)]);
                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 1], 0)]);
                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 2], 0)]);
                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 3], 0)]);

                c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
                buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

                c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 0); m -= ((s & 3) == 1);
                buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;

                c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 1); m -= ((s & 3) == 1);
                buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

                c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 2); m -= ((s & 3) == 1);
                buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
            }

            for (j -= prefetch_distance + 3; i >= j; i -= 1)
            {
                c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
                buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
            }

            c1 = (i >= 0) ? T[i] : -1; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((s & 3) == 1);
            buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;
        }

        return (SaTy)(omp_block_start + omp_block_size - 1 - m);
    }

    static SaTy count_and_gather_compacted_lms_suffixes_32s_2k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        std::memset(buckets, 0, 2 * (size_t)k * sizeof(SaTy));

        fast_sint_t m = omp_block_start + omp_block_size - 1;

        if (omp_block_size > 0)
        {
            const fast_sint_t prefetch_distance = 32;

            fast_sint_t i, j = m + 1, c0 = T[m], c1 = -1;

            while (j < n && (c1 = T[j]) == c0) { ++j; }

            fast_uint_t s = c0 >= c1;

            for (i = m - 1, j = omp_block_start + prefetch_distance + 3; i >= j; i -= 4)
            {
                prefetchr(&T[i - 2 * prefetch_distance]);

                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 0] & saint_max, 0)]);
                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 1] & saint_max, 0)]);
                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 2] & saint_max, 0)]);
                prefetchw(&buckets[buckets_index2(T[i - prefetch_distance - 3] & saint_max, 0)]);

                c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((fast_sint_t)(s & 3) == (c0 >= 0));
                c0 &= saint_max; buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

                c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 0); m -= ((fast_sint_t)(s & 3) == (c1 >= 0));
                c1 &= saint_max; buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;

                c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 1); m -= ((fast_sint_t)(s & 3) == (c0 >= 0));
                c0 &= saint_max; buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;

                c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i - 2); m -= ((fast_sint_t)(s & 3) == (c1 >= 0));
                c1 &= saint_max; buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
            }

            for (j -= prefetch_distance + 3; i >= j; i -= 1)
            {
                c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((fast_sint_t)(s & 3) == (c1 >= 0));
                c1 &= saint_max; buckets[buckets_index2((fast_uint_t)c1, (s & 3) == 1)]++;
            }

            c1 = (i >= 0) ? T[i] : -1; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1))); SA[m] = (SaTy)(i + 1); m -= ((fast_sint_t)(s & 3) == (c0 >= 0));
            c0 &= saint_max; buckets[buckets_index2((fast_uint_t)c0, (s & 3) == 1)]++;
        }

        return (SaTy)(omp_block_start + omp_block_size - 1 - m);
    }

    static fast_sint_t get_bucket_stride(fast_sint_t free_space, fast_sint_t bucket_size, fast_sint_t num_buckets)
    {
        fast_sint_t bucket_size_1024 = (bucket_size + 1023) & (-1024); if (free_space / (num_buckets - 1) >= bucket_size_1024) { return bucket_size_1024; }
        fast_sint_t bucket_size_16 = (bucket_size + 15) & (-16); if (free_space / (num_buckets - 1) >= bucket_size_16) { return bucket_size_16; }

        return bucket_size;
    }

    static SaTy count_and_gather_lms_suffixes_32s_4k_fs_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy m = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            if (num_threads == 1)
            {
                m = count_and_gather_lms_suffixes_32s_4k(T, SA, n, k, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                fast_sint_t bucket_size = 4 * (fast_sint_t)k;
                fast_sint_t bucket_stride = get_bucket_stride(buckets - &SA[n], bucket_size, num_threads);

                {
                    thread_state[id].state.position = omp_block_start + omp_block_size;
                    thread_state[id].state.count = count_and_gather_lms_suffixes_32s_4k(T, SA, n, k, buckets - (id * bucket_stride), omp_block_start, omp_block_size);
                }

                mp::barrier(barrier);

                if (id == num_threads - 1)
                {
                    fast_sint_t t;
                    for (t = num_threads - 1; t >= 0; --t)
                    {
                        m += (SaTy)thread_state[t].state.count;

                        if (t != num_threads - 1 && thread_state[t].state.count > 0)
                        {
                            std::memcpy(&SA[n - m], &SA[thread_state[t].state.position - thread_state[t].state.count], (size_t)thread_state[t].state.count * sizeof(SaTy));
                        }
                    }
                }
                else
                {
                    num_threads = num_threads - 1;
                    omp_block_stride = (bucket_size / num_threads) & (-16);
                    omp_block_start = id * omp_block_stride;
                    omp_block_size = id < num_threads - 1 ? omp_block_stride : bucket_size - omp_block_start;

                    accumulate_counts_s32(buckets + omp_block_start, omp_block_size, bucket_stride, num_threads + 1);
                }
            }
        }, mp::ParallelCond{n >= 65536});

        return m;
    }

    static SaTy count_and_gather_lms_suffixes_32s_2k_fs_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy m = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            if (num_threads == 1)
            {
                m = count_and_gather_lms_suffixes_32s_2k(T, SA, n, k, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                fast_sint_t bucket_size = 2 * (fast_sint_t)k;
                fast_sint_t bucket_stride = get_bucket_stride(buckets - &SA[n], bucket_size, num_threads);

                {
                    thread_state[id].state.position = omp_block_start + omp_block_size;
                    thread_state[id].state.count = count_and_gather_lms_suffixes_32s_2k(T, SA, n, k, buckets - (id * bucket_stride), omp_block_start, omp_block_size);
                }

                mp::barrier(barrier);

                if (id == num_threads - 1)
                {
                    fast_sint_t t;
                    for (t = num_threads - 1; t >= 0; --t)
                    {
                        m += (SaTy)thread_state[t].state.count;

                        if (t != num_threads - 1 && thread_state[t].state.count > 0)
                        {
                            std::memcpy(&SA[n - m], &SA[thread_state[t].state.position - thread_state[t].state.count], (size_t)thread_state[t].state.count * sizeof(SaTy));
                        }
                    }
                }
                else
                {
                    num_threads = num_threads - 1;
                    omp_block_stride = (bucket_size / num_threads) & (-16);
                    omp_block_start = id * omp_block_stride;
                    omp_block_size = id < num_threads - 1 ? omp_block_stride : bucket_size - omp_block_start;

                    accumulate_counts_s32(buckets + omp_block_start, omp_block_size, bucket_stride, num_threads + 1);
                }
            }
        }, mp::ParallelCond{n >= 65536});

        return m;
    }

    static void count_and_gather_compacted_lms_suffixes_32s_2k_fs_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            if (num_threads == 1)
            {
                count_and_gather_compacted_lms_suffixes_32s_2k(T, SA, n, k, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                fast_sint_t bucket_size = 2 * (fast_sint_t)k;
                fast_sint_t bucket_stride = get_bucket_stride(buckets - &SA[n + n], bucket_size, num_threads);

                thread_state[id].state.position = omp_block_start + omp_block_size;
                thread_state[id].state.count = count_and_gather_compacted_lms_suffixes_32s_2k(T, SA + n, n, k, buckets - (id * bucket_stride), omp_block_start, omp_block_size);

                mp::barrier(barrier);
                fast_sint_t t, m = 0; for (t = num_threads - 1; t >= id; --t) { m += (SaTy)thread_state[t].state.count; }

                if (thread_state[id].state.count > 0)
                {
                    std::memcpy(&SA[n - m], &SA[n + thread_state[id].state.position - thread_state[id].state.count], (size_t)thread_state[id].state.count * sizeof(SaTy));
                }

                omp_block_stride = (bucket_size / num_threads) & (-16);
                omp_block_start = id * omp_block_stride;
                omp_block_size = id < num_threads - 1 ? omp_block_stride : bucket_size - omp_block_start;

                accumulate_counts_s32(buckets + omp_block_start, omp_block_size, bucket_stride, num_threads);
            }
        }, mp::ParallelCond{n >= 65536});
    }

    static SaTy count_and_gather_lms_suffixes_32s_4k_nofs_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool)
    {
        SaTy m = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            if (num_threads == 1)
            {
                m = count_and_gather_lms_suffixes_32s_4k(T, SA, n, k, buckets, 0, n);
            }
            else if (id == 0)
            {
                count_lms_suffixes_32s_4k(T, n, k, buckets);
            }
            else
            {
                m = gather_lms_suffixes_32s(T, SA, n);
            }
        }, mp::MaximumWorkers{ 2 }, mp::ParallelCond{n >= 65536});

        return m;
    }

    static SaTy count_and_gather_lms_suffixes_32s_2k_nofs_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool)
    {
        SaTy m = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            if (num_threads == 1)
            {
                m = count_and_gather_lms_suffixes_32s_2k(T, SA, n, k, buckets, 0, n);
            }
            else if (id == 0)
            {
                count_lms_suffixes_32s_2k(T, n, k, buckets);
            }
            else
            {
                m = gather_lms_suffixes_32s(T, SA, n);
            }
        }, mp::MaximumWorkers{ 2 }, mp::ParallelCond{n >= 65536});

        return m;
    }

    static SaTy count_and_gather_compacted_lms_suffixes_32s_2k_nofs_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool)
    {
        SaTy m = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            if (num_threads == 1)
            {
                m = count_and_gather_compacted_lms_suffixes_32s_2k(T, SA, n, k, buckets, 0, n);
            }
            else if (id == 0)
            {
                count_compacted_lms_suffixes_32s_2k(T, n, k, buckets);
            }
            else
            {
                m = gather_compacted_lms_suffixes_32s(T, SA, n);
            }
        }, mp::MaximumWorkers{ 2 }, mp::ParallelCond{n >= 65536});
        return m;
    }

    static SaTy count_and_gather_lms_suffixes_32s_4k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy max_threads = ((buckets - &SA[n]) / ((4 * (fast_sint_t)k + 15) & (-16))); 
        if (max_threads > 1 && n >= 65536 && n / k >= 2 && pool)
        {
            if (max_threads > n / 16 / k) { max_threads = n / 16 / k; }
            auto ls = mp::OverrideLimitedSize(pool, std::max(max_threads, (SaTy)2));
            return count_and_gather_lms_suffixes_32s_4k_fs_omp(T, SA, n, k, buckets, pool, thread_state);
        }
        else
        {
            return count_and_gather_lms_suffixes_32s_4k_nofs_omp(T, SA, n, k, buckets, pool);
        }
    }

    static SaTy count_and_gather_lms_suffixes_32s_2k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy max_threads = ((buckets - &SA[n]) / ((2 * (fast_sint_t)k + 15) & (-16))); 
        if (max_threads > 1 && n >= 65536 && n / k >= 2 && pool)
        {
            if (max_threads > n / 8 / k) { max_threads = n / 8 / k; }
            auto ls = mp::OverrideLimitedSize(pool, std::max(max_threads, (SaTy)2));
            return count_and_gather_lms_suffixes_32s_2k_fs_omp(T, SA, n, k, buckets, pool, thread_state);
        }
        else
        {
            return count_and_gather_lms_suffixes_32s_2k_nofs_omp(T, SA, n, k, buckets, pool);
        }
    }

    static void count_and_gather_compacted_lms_suffixes_32s_2k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy max_threads = ((buckets - &SA[n + n]) / ((2 * (fast_sint_t)k + 15) & (-16)));
    
        if (max_threads > 1 && n >= 65536 && n / k >= 2 && pool)
        {
            if (max_threads > n / 8 / k) { max_threads = n / 8 / k; }
            auto ls = mp::OverrideLimitedSize(pool, std::max(max_threads, (SaTy)2));
            count_and_gather_compacted_lms_suffixes_32s_2k_fs_omp(T, SA, n, k, buckets, pool, thread_state);
        }
        else
        {
            count_and_gather_compacted_lms_suffixes_32s_2k_nofs_omp(T, SA, n, k, buckets, pool);
        }
    }

    static void count_suffixes_32s(const SaTy* RESTRICT T, SaTy n, SaTy k, SaTy* RESTRICT buckets)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, (size_t)k * sizeof(SaTy));

        fast_sint_t i, j;
        for (i = 0, j = (fast_sint_t)n - 7; i < j; i += 8)
        {
            prefetchr(&T[i + prefetch_distance]);

            buckets[T[i + 0]]++;
            buckets[T[i + 1]]++;
            buckets[T[i + 2]]++;
            buckets[T[i + 3]]++;
            buckets[T[i + 4]]++;
            buckets[T[i + 5]]++;
            buckets[T[i + 6]]++;
            buckets[T[i + 7]]++;
        }

        for (j += 7; i < j; i += 1)
        {
            buckets[T[i]]++;
        }
    }

    static void initialize_buckets_start_and_end_16u(SaTy* RESTRICT buckets, SaTy* RESTRICT freq)
    {
        SaTy* RESTRICT bucket_start = &buckets[6 * alphabet_size];
        SaTy* RESTRICT bucket_end = &buckets[7 * alphabet_size];

        if (freq != nullptr)
        {
            fast_sint_t i, j; SaTy sum = 0;
            for (i = buckets_index4(0, 0), j = 0; i <= buckets_index4(alphabet_size - 1, 0); i += buckets_index4(1, 0), j += 1)
            {
                bucket_start[j] = sum;
                sum += (freq[j] = buckets[i + buckets_index4(0, 0)] + buckets[i + buckets_index4(0, 1)] + buckets[i + buckets_index4(0, 2)] + buckets[i + buckets_index4(0, 3)]);
                bucket_end[j] = sum;
            }
        }
        else
        {
            fast_sint_t i, j; SaTy sum = 0;
            for (i = buckets_index4(0, 0), j = 0; i <= buckets_index4(alphabet_size - 1, 0); i += buckets_index4(1, 0), j += 1)
            {
                bucket_start[j] = sum;
                sum += buckets[i + buckets_index4(0, 0)] + buckets[i + buckets_index4(0, 1)] + buckets[i + buckets_index4(0, 2)] + buckets[i + buckets_index4(0, 3)];
                bucket_end[j] = sum;
            }
        }
    }

    static void initialize_buckets_start_and_end_32s_6k(SaTy k, SaTy* RESTRICT buckets)
    {
        SaTy* RESTRICT bucket_start = &buckets[4 * k];
        SaTy* RESTRICT bucket_end = &buckets[5 * k];

        fast_sint_t i, j; SaTy sum = 0;
        for (i = buckets_index4(0, 0), j = 0; i <= buckets_index4((fast_sint_t)k - 1, 0); i += buckets_index4(1, 0), j += 1)
        {
            bucket_start[j] = sum;
            sum += buckets[i + buckets_index4(0, 0)] + buckets[i + buckets_index4(0, 1)] + buckets[i + buckets_index4(0, 2)] + buckets[i + buckets_index4(0, 3)];
            bucket_end[j] = sum;
        }
    }

    static void initialize_buckets_start_and_end_32s_4k(SaTy k, SaTy* RESTRICT buckets)
    {
        SaTy* RESTRICT bucket_start = &buckets[2 * k];
        SaTy* RESTRICT bucket_end = &buckets[3 * k];

        fast_sint_t i, j; SaTy sum = 0;
        for (i = buckets_index2(0, 0), j = 0; i <= buckets_index2((fast_sint_t)k - 1, 0); i += buckets_index2(1, 0), j += 1)
        {
            bucket_start[j] = sum;
            sum += buckets[i + buckets_index2(0, 0)] + buckets[i + buckets_index2(0, 1)];
            bucket_end[j] = sum;
        }
    }

    static void initialize_buckets_end_32s_2k(SaTy k, SaTy* RESTRICT buckets)
    {
        fast_sint_t i; SaTy sum0 = 0;
        for (i = buckets_index2(0, 0); i <= buckets_index2((fast_sint_t)k - 1, 0); i += buckets_index2(1, 0))
        {
            sum0 += buckets[i + buckets_index2(0, 0)] + buckets[i + buckets_index2(0, 1)]; buckets[i + buckets_index2(0, 0)] = sum0;
        }
    }

    static void initialize_buckets_start_and_end_32s_2k(SaTy k, SaTy* RESTRICT buckets)
    {
        fast_sint_t i, j;
        for (i = buckets_index2(0, 0), j = 0; i <= buckets_index2((fast_sint_t)k - 1, 0); i += buckets_index2(1, 0), j += 1)
        {
            buckets[j] = buckets[i];
        }

        buckets[k] = 0; std::memcpy(&buckets[k + 1], buckets, ((size_t)k - 1) * sizeof(SaTy));
    }

    static void initialize_buckets_start_32s_1k(SaTy k, SaTy* RESTRICT buckets)
    {
        fast_sint_t i; SaTy sum = 0;
        for (i = 0; i <= (fast_sint_t)k - 1; i += 1) { SaTy tmp = buckets[i]; buckets[i] = sum; sum += tmp; }
    }

    static void initialize_buckets_end_32s_1k(SaTy k, SaTy* RESTRICT buckets)
    {
        fast_sint_t i; SaTy sum = 0;
        for (i = 0; i <= (fast_sint_t)k - 1; i += 1) { sum += buckets[i]; buckets[i] = sum; }
    }

    static SaTy initialize_buckets_for_lms_suffixes_radix_sort_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT buckets, SaTy first_lms_suffix)
    {
        {
            fast_uint_t     s = 0;
            fast_sint_t     c0 = T[first_lms_suffix];
            fast_sint_t     c1 = 0;

            for (; --first_lms_suffix >= 0; )
            {
                c1 = c0; c0 = T[first_lms_suffix]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]--;
            }

            buckets[buckets_index4((fast_uint_t)c0, (s << 1) & 3)]--;
        }

        {
            SaTy* RESTRICT temp_bucket = &buckets[4 * alphabet_size];

            fast_sint_t i, j; SaTy sum = 0;
            for (i = buckets_index4(0, 0), j = buckets_index2(0, 0); i <= buckets_index4(alphabet_size - 1, 0); i += buckets_index4(1, 0), j += buckets_index2(1, 0))
            {
                temp_bucket[j + buckets_index2(0, 1)] = sum; sum += buckets[i + buckets_index4(0, 1)] + buckets[i + buckets_index4(0, 3)]; temp_bucket[j] = sum;
            }

            return sum;
        }
    }

    static void initialize_buckets_for_lms_suffixes_radix_sort_32s_2k(const SaTy* RESTRICT T, SaTy k, SaTy* RESTRICT buckets, SaTy first_lms_suffix)
    {
        buckets[buckets_index2(T[first_lms_suffix], 0)]++;
        buckets[buckets_index2(T[first_lms_suffix], 1)]--;

        fast_sint_t i; SaTy sum0 = 0, sum1 = 0;
        for (i = buckets_index2(0, 0); i <= buckets_index2((fast_sint_t)k - 1, 0); i += buckets_index2(1, 0))
        {
            sum0 += buckets[i + buckets_index2(0, 0)] + buckets[i + buckets_index2(0, 1)];
            sum1 += buckets[i + buckets_index2(0, 1)];

            buckets[i + buckets_index2(0, 0)] = sum0;
            buckets[i + buckets_index2(0, 1)] = sum1;
        }
    }

    static SaTy initialize_buckets_for_lms_suffixes_radix_sort_32s_6k(const SaTy* RESTRICT T, SaTy k, SaTy* RESTRICT buckets, SaTy first_lms_suffix)
    {
        {
            fast_uint_t     s = 0;
            fast_sint_t     c0 = T[first_lms_suffix];
            fast_sint_t     c1 = 0;

            for (; --first_lms_suffix >= 0; )
            {
                c1 = c0; c0 = T[first_lms_suffix]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
                buckets[buckets_index4((fast_uint_t)c1, s & 3)]--;
            }

            buckets[buckets_index4((fast_uint_t)c0, (s << 1) & 3)]--;
        }

        {
            SaTy* RESTRICT temp_bucket = &buckets[4 * k];

            fast_sint_t i, j; SaTy sum = 0;
            for (i = buckets_index4(0, 0), j = 0; i <= buckets_index4((fast_sint_t)k - 1, 0); i += buckets_index4(1, 0), j += 1)
            {
                sum += buckets[i + buckets_index4(0, 1)] + buckets[i + buckets_index4(0, 3)]; temp_bucket[j] = sum;
            }

            return sum;
        }
    }

    static void initialize_buckets_for_radix_and_partial_sorting_32s_4k(const SaTy* RESTRICT T, SaTy k, SaTy* RESTRICT buckets, SaTy first_lms_suffix)
    {
        SaTy* RESTRICT bucket_start = &buckets[2 * k];
        SaTy* RESTRICT bucket_end = &buckets[3 * k];

        buckets[buckets_index2(T[first_lms_suffix], 0)]++;
        buckets[buckets_index2(T[first_lms_suffix], 1)]--;

        fast_sint_t i, j; SaTy sum0 = 0, sum1 = 0;
        for (i = buckets_index2(0, 0), j = 0; i <= buckets_index2((fast_sint_t)k - 1, 0); i += buckets_index2(1, 0), j += 1)
        {
            bucket_start[j] = sum1;

            sum0 += buckets[i + buckets_index2(0, 1)];
            sum1 += buckets[i + buckets_index2(0, 0)] + buckets[i + buckets_index2(0, 1)];
            buckets[i + buckets_index2(0, 1)] = sum0;

            bucket_end[j] = sum1;
        }
    }

    static void radix_sort_lms_suffixes_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 3; i >= j; i -= 4)
        {
            prefetchr(&SA[i - 2 * prefetch_distance]);

            prefetchr(&T[SA[i - prefetch_distance - 0]]);
            prefetchr(&T[SA[i - prefetch_distance - 1]]);
            prefetchr(&T[SA[i - prefetch_distance - 2]]);
            prefetchr(&T[SA[i - prefetch_distance - 3]]);

            SaTy p0 = SA[i - 0]; SA[--induction_bucket[buckets_index2(T[p0], 0)]] = p0;
            SaTy p1 = SA[i - 1]; SA[--induction_bucket[buckets_index2(T[p1], 0)]] = p1;
            SaTy p2 = SA[i - 2]; SA[--induction_bucket[buckets_index2(T[p2], 0)]] = p2;
            SaTy p3 = SA[i - 3]; SA[--induction_bucket[buckets_index2(T[p3], 0)]] = p3;
        }

        for (j -= prefetch_distance + 3; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[--induction_bucket[buckets_index2(T[p], 0)]] = p;
        }
    }

    static void radix_sort_lms_suffixes_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            if (num_threads == 1)
            {
                radix_sort_lms_suffixes_16u(T, SA, &buckets[4 * alphabet_size], (fast_sint_t)n - (fast_sint_t)m + 1, (fast_sint_t)m - 1);
            }
            else
            {
                {
                    SaTy* RESTRICT src_bucket = &buckets[4 * alphabet_size];
                    SaTy* RESTRICT dst_bucket = thread_state[id].state.buckets;

                    fast_sint_t i, j;
                    for (i = buckets_index2(0, 0), j = buckets_index4(0, 1); i <= buckets_index2(alphabet_size - 1, 0); i += buckets_index2(1, 0), j += buckets_index4(1, 0))
                    {
                        dst_bucket[i] = src_bucket[i] - dst_bucket[j];
                    }
                }

                {
                    fast_sint_t t, omp_block_start = 0, omp_block_size = thread_state[id].state.m;
                    for (t = num_threads - 1; t >= id; --t) omp_block_start += thread_state[t].state.m;

                    if (omp_block_start == (fast_sint_t)m && omp_block_size > 0)
                    {
                        omp_block_start -= 1; omp_block_size -= 1;
                    }

                    radix_sort_lms_suffixes_16u(T, SA, thread_state[id].state.buckets, (fast_sint_t)n - omp_block_start, omp_block_size);
                }
            }
        }, mp::ParallelCond{n >= 65536 && m >= 65536});
    }

    static void radix_sort_lms_suffixes_32s_6k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + 2 * prefetch_distance + 3; i >= j; i -= 4)
        {
            prefetchr(&SA[i - 3 * prefetch_distance]);

            prefetchr(&T[SA[i - 2 * prefetch_distance - 0]]);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 1]]);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 2]]);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 3]]);

            prefetchw(&induction_bucket[T[SA[i - prefetch_distance - 0]]]);
            prefetchw(&induction_bucket[T[SA[i - prefetch_distance - 1]]]);
            prefetchw(&induction_bucket[T[SA[i - prefetch_distance - 2]]]);
            prefetchw(&induction_bucket[T[SA[i - prefetch_distance - 3]]]);

            SaTy p0 = SA[i - 0]; SA[--induction_bucket[T[p0]]] = p0;
            SaTy p1 = SA[i - 1]; SA[--induction_bucket[T[p1]]] = p1;
            SaTy p2 = SA[i - 2]; SA[--induction_bucket[T[p2]]] = p2;
            SaTy p3 = SA[i - 3]; SA[--induction_bucket[T[p3]]] = p3;
        }

        for (j -= 2 * prefetch_distance + 3; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[--induction_bucket[T[p]]] = p;
        }
    }

    static void radix_sort_lms_suffixes_32s_2k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + 2 * prefetch_distance + 3; i >= j; i -= 4)
        {
            prefetchr(&SA[i - 3 * prefetch_distance]);

            prefetchr(&T[SA[i - 2 * prefetch_distance - 0]]);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 1]]);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 2]]);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 3]]);

            prefetchw(&induction_bucket[buckets_index2(T[SA[i - prefetch_distance - 0]], 0)]);
            prefetchw(&induction_bucket[buckets_index2(T[SA[i - prefetch_distance - 1]], 0)]);
            prefetchw(&induction_bucket[buckets_index2(T[SA[i - prefetch_distance - 2]], 0)]);
            prefetchw(&induction_bucket[buckets_index2(T[SA[i - prefetch_distance - 3]], 0)]);

            SaTy p0 = SA[i - 0]; SA[--induction_bucket[buckets_index2(T[p0], 0)]] = p0;
            SaTy p1 = SA[i - 1]; SA[--induction_bucket[buckets_index2(T[p1], 0)]] = p1;
            SaTy p2 = SA[i - 2]; SA[--induction_bucket[buckets_index2(T[p2], 0)]] = p2;
            SaTy p3 = SA[i - 3]; SA[--induction_bucket[buckets_index2(T[p3], 0)]] = p3;
        }

        for (j -= 2 * prefetch_distance + 3; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[--induction_bucket[buckets_index2(T[p], 0)]] = p;
        }
    }

    static void radix_sort_lms_suffixes_32s_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchr(&SA[i + 2 * prefetch_distance]);

            prefetchr(&T[SA[i + prefetch_distance + 0]]);
            prefetchr(&T[SA[i + prefetch_distance + 1]]);
            prefetchr(&T[SA[i + prefetch_distance + 2]]);
            prefetchr(&T[SA[i + prefetch_distance + 3]]);

            prefetchw(&cache[i + prefetch_distance]);

            cache[i + 0].symbol = T[cache[i + 0].index = SA[i + 0]];
            cache[i + 1].symbol = T[cache[i + 1].index = SA[i + 1]];
            cache[i + 2].symbol = T[cache[i + 2].index = SA[i + 2]];
            cache[i + 3].symbol = T[cache[i + 3].index = SA[i + 3]];
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            cache[i].symbol = T[cache[i].index = SA[i]];
        }
    }

    static void radix_sort_lms_suffixes_32s_6k_block_sort(SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 3; i >= j; i -= 4)
        {
            prefetchw(&cache[i - 2 * prefetch_distance]);

            prefetchw(&induction_bucket[cache[i - prefetch_distance - 0].symbol]);
            prefetchw(&induction_bucket[cache[i - prefetch_distance - 1].symbol]);
            prefetchw(&induction_bucket[cache[i - prefetch_distance - 2].symbol]);
            prefetchw(&induction_bucket[cache[i - prefetch_distance - 3].symbol]);

            cache[i - 0].symbol = --induction_bucket[cache[i - 0].symbol];
            cache[i - 1].symbol = --induction_bucket[cache[i - 1].symbol];
            cache[i - 2].symbol = --induction_bucket[cache[i - 2].symbol];
            cache[i - 3].symbol = --induction_bucket[cache[i - 3].symbol];
        }

        for (j -= prefetch_distance + 3; i >= j; i -= 1)
        {
            cache[i].symbol = --induction_bucket[cache[i].symbol];
        }
    }

    static void radix_sort_lms_suffixes_32s_2k_block_sort(SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 3; i >= j; i -= 4)
        {
            prefetchw(&cache[i - 2 * prefetch_distance]);

            prefetchw(&induction_bucket[buckets_index2(cache[i - prefetch_distance - 0].symbol, 0)]);
            prefetchw(&induction_bucket[buckets_index2(cache[i - prefetch_distance - 1].symbol, 0)]);
            prefetchw(&induction_bucket[buckets_index2(cache[i - prefetch_distance - 2].symbol, 0)]);
            prefetchw(&induction_bucket[buckets_index2(cache[i - prefetch_distance - 3].symbol, 0)]);

            cache[i - 0].symbol = --induction_bucket[buckets_index2(cache[i - 0].symbol, 0)];
            cache[i - 1].symbol = --induction_bucket[buckets_index2(cache[i - 1].symbol, 0)];
            cache[i - 2].symbol = --induction_bucket[buckets_index2(cache[i - 2].symbol, 0)];
            cache[i - 3].symbol = --induction_bucket[buckets_index2(cache[i - 3].symbol, 0)];
        }

        for (j -= prefetch_distance + 3; i >= j; i -= 1)
        {
            cache[i].symbol = --induction_bucket[buckets_index2(cache[i].symbol, 0)];
        }
    }

    static void radix_sort_lms_suffixes_32s_6k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                radix_sort_lms_suffixes_32s_6k(T, SA, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                radix_sort_lms_suffixes_32s_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    radix_sort_lms_suffixes_32s_6k_block_sort(induction_bucket, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});
    }

    static void radix_sort_lms_suffixes_32s_2k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                radix_sort_lms_suffixes_32s_2k(T, SA, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                radix_sort_lms_suffixes_32s_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    radix_sort_lms_suffixes_32s_2k_block_sort(induction_bucket, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});
    }

    static void radix_sort_lms_suffixes_32s_6k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (mp::getPoolSize(pool) == 1 || m < 65536)
        {
            radix_sort_lms_suffixes_32s_6k(T, SA, induction_bucket, (fast_sint_t)n - (fast_sint_t)m + 1, (fast_sint_t)m - 1);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = 0; block_start < (fast_sint_t)m - 1; block_start = block_end)
            {
                block_end = block_start + (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end >= m) { block_end = (fast_sint_t)m - 1; }

                radix_sort_lms_suffixes_32s_6k_block_omp(T, SA, induction_bucket, thread_state[0].state.cache, (fast_sint_t)n - block_end, block_end - block_start, pool);
            }
        }
    }

    static void radix_sort_lms_suffixes_32s_2k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (mp::getPoolSize(pool) == 1 || m < 65536)
        {
            radix_sort_lms_suffixes_32s_2k(T, SA, induction_bucket, (fast_sint_t)n - (fast_sint_t)m + 1, (fast_sint_t)m - 1);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = 0; block_start < (fast_sint_t)m - 1; block_start = block_end)
            {
                block_end = block_start + (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end >= m) { block_end = (fast_sint_t)m - 1; }

                radix_sort_lms_suffixes_32s_2k_block_omp(T, SA, induction_bucket, thread_state[0].state.cache, (fast_sint_t)n - block_end, block_end - block_start, pool);
            }
        }
    }

    static SaTy radix_sort_lms_suffixes_32s_1k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy             i = n - 2;
        SaTy             m = 0;
        fast_uint_t           s = 1;
        fast_sint_t           c0 = T[n - 1];
        fast_sint_t           c1 = 0;
        fast_sint_t           c2 = 0;

        for (; i >= prefetch_distance + 3; i -= 4)
        {
            prefetchr(&T[i - 2 * prefetch_distance]);

            prefetchw(&buckets[T[i - prefetch_distance - 0]]);
            prefetchw(&buckets[T[i - prefetch_distance - 1]]);
            prefetchw(&buckets[T[i - prefetch_distance - 2]]);
            prefetchw(&buckets[T[i - prefetch_distance - 3]]);

            c1 = T[i - 0]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            if ((s & 3) == 1) { SA[--buckets[c2 = c0]] = i + 1; m++; }

            c0 = T[i - 1]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            if ((s & 3) == 1) { SA[--buckets[c2 = c1]] = i - 0; m++; }

            c1 = T[i - 2]; s = (s << 1) + (fast_uint_t)(c1 > (c0 - (fast_sint_t)(s & 1)));
            if ((s & 3) == 1) { SA[--buckets[c2 = c0]] = i - 1; m++; }

            c0 = T[i - 3]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            if ((s & 3) == 1) { SA[--buckets[c2 = c1]] = i - 2; m++; }
        }

        for (; i >= 0; i -= 1)
        {
            c1 = c0; c0 = T[i]; s = (s << 1) + (fast_uint_t)(c0 > (c1 - (fast_sint_t)(s & 1)));
            if ((s & 3) == 1) { SA[--buckets[c2 = c1]] = i + 1; m++; }
        }

        if (m > 1)
        {
            SA[buckets[c2]] = 0;
        }

        return m;
    }

    static void radix_sort_set_markers_32s_6k(SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchr(&induction_bucket[i + 2 * prefetch_distance]);

            prefetchw(&SA[induction_bucket[i + prefetch_distance + 0]]);
            prefetchw(&SA[induction_bucket[i + prefetch_distance + 1]]);
            prefetchw(&SA[induction_bucket[i + prefetch_distance + 2]]);
            prefetchw(&SA[induction_bucket[i + prefetch_distance + 3]]);

            SA[induction_bucket[i + 0]] |= saint_min;
            SA[induction_bucket[i + 1]] |= saint_min;
            SA[induction_bucket[i + 2]] |= saint_min;
            SA[induction_bucket[i + 3]] |= saint_min;
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            SA[induction_bucket[i]] |= saint_min;
        }
    }

    static void radix_sort_set_markers_32s_4k(SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchr(&induction_bucket[buckets_index2(i + 2 * prefetch_distance, 0)]);

            prefetchw(&SA[induction_bucket[buckets_index2(i + prefetch_distance + 0, 0)]]);
            prefetchw(&SA[induction_bucket[buckets_index2(i + prefetch_distance + 1, 0)]]);
            prefetchw(&SA[induction_bucket[buckets_index2(i + prefetch_distance + 2, 0)]]);
            prefetchw(&SA[induction_bucket[buckets_index2(i + prefetch_distance + 3, 0)]]);

            SA[induction_bucket[buckets_index2(i + 0, 0)]] |= suffix_group_marker;
            SA[induction_bucket[buckets_index2(i + 1, 0)]] |= suffix_group_marker;
            SA[induction_bucket[buckets_index2(i + 2, 0)]] |= suffix_group_marker;
            SA[induction_bucket[buckets_index2(i + 3, 0)]] |= suffix_group_marker;
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            SA[induction_bucket[buckets_index2(i, 0)]] |= suffix_group_marker;
        }
    }

    static void radix_sort_set_markers_32s_6k_omp(SaTy* RESTRICT SA, SaTy k, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (((fast_sint_t)k - 1) / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : (fast_sint_t)k - 1 - omp_block_start;
            radix_sort_set_markers_32s_6k(SA, induction_bucket, omp_block_start, omp_block_size);
        }, mp::ParallelCond{k >= 65536});
    }

    static void radix_sort_set_markers_32s_4k_omp(SaTy* RESTRICT SA, SaTy k, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (((fast_sint_t)k - 1) / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : (fast_sint_t)k - 1 - omp_block_start;

            radix_sort_set_markers_32s_4k(SA, induction_bucket, omp_block_start, omp_block_size);
        }, mp::ParallelCond{k >= 65536});
    }

    static void initialize_buckets_for_partial_sorting_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT buckets, SaTy first_lms_suffix, SaTy left_suffixes_count)
    {
        SaTy* RESTRICT temp_bucket = &buckets[4 * alphabet_size];

        buckets[buckets_index4((fast_uint_t)T[first_lms_suffix], 1)]++;

        fast_sint_t i, j; SaTy sum0 = left_suffixes_count + 1, sum1 = 0;
        for (i = buckets_index4(0, 0), j = buckets_index2(0, 0); i <= buckets_index4(alphabet_size - 1, 0); i += buckets_index4(1, 0), j += buckets_index2(1, 0))
        {
            temp_bucket[j + buckets_index2(0, 0)] = sum0;

            sum0 += buckets[i + buckets_index4(0, 0)] + buckets[i + buckets_index4(0, 2)];
            sum1 += buckets[i + buckets_index4(0, 1)];

            buckets[j + buckets_index2(0, 0)] = sum0;
            buckets[j + buckets_index2(0, 1)] = sum1;
        }
    }

    static void initialize_buckets_for_partial_sorting_32s_6k(const SaTy* RESTRICT T, SaTy k, SaTy* RESTRICT buckets, SaTy first_lms_suffix, SaTy left_suffixes_count)
    {
        SaTy* RESTRICT temp_bucket = &buckets[4 * k];

        fast_sint_t i, j; SaTy sum0 = left_suffixes_count + 1, sum1 = 0, sum2 = 0;
        for (first_lms_suffix = T[first_lms_suffix], i = buckets_index4(0, 0), j = buckets_index2(0, 0); i <= buckets_index4((fast_sint_t)first_lms_suffix - 1, 0); i += buckets_index4(1, 0), j += buckets_index2(1, 0))
        {
            SaTy SS = buckets[i + buckets_index4(0, 0)];
            SaTy LS = buckets[i + buckets_index4(0, 1)];
            SaTy SL = buckets[i + buckets_index4(0, 2)];
            SaTy LL = buckets[i + buckets_index4(0, 3)];

            buckets[i + buckets_index4(0, 0)] = sum0;
            buckets[i + buckets_index4(0, 1)] = sum2;
            buckets[i + buckets_index4(0, 2)] = 0;
            buckets[i + buckets_index4(0, 3)] = 0;

            sum0 += SS + SL; sum1 += LS; sum2 += LS + LL;

            temp_bucket[j + buckets_index2(0, 0)] = sum0;
            temp_bucket[j + buckets_index2(0, 1)] = sum1;
        }

        for (sum1 += 1; i <= buckets_index4((fast_sint_t)k - 1, 0); i += buckets_index4(1, 0), j += buckets_index2(1, 0))
        {
            SaTy SS = buckets[i + buckets_index4(0, 0)];
            SaTy LS = buckets[i + buckets_index4(0, 1)];
            SaTy SL = buckets[i + buckets_index4(0, 2)];
            SaTy LL = buckets[i + buckets_index4(0, 3)];

            buckets[i + buckets_index4(0, 0)] = sum0;
            buckets[i + buckets_index4(0, 1)] = sum2;
            buckets[i + buckets_index4(0, 2)] = 0;
            buckets[i + buckets_index4(0, 3)] = 0;

            sum0 += SS + SL; sum1 += LS; sum2 += LS + LL;

            temp_bucket[j + buckets_index2(0, 0)] = sum0;
            temp_bucket[j + buckets_index2(0, 1)] = sum1;
        }
    }

    static SaTy partial_sorting_scan_left_to_right_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[4 * alphabet_size];
        SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchr(&SA[i + 2 * prefetch_distance]);

            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 2);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 2);

            SaTy p0 = SA[i + 0]; d += (p0 < 0); p0 &= saint_max; SaTy v0 = buckets_index2(T[p0 - 1], T[p0 - 2] >= T[p0 - 1]);
            SA[induction_bucket[v0]++] = (p0 - 1) | ((SaTy)(distinct_names[v0] != d) << (saint_bit - 1)); distinct_names[v0] = d;

            SaTy p1 = SA[i + 1]; d += (p1 < 0); p1 &= saint_max; SaTy v1 = buckets_index2(T[p1 - 1], T[p1 - 2] >= T[p1 - 1]);
            SA[induction_bucket[v1]++] = (p1 - 1) | ((SaTy)(distinct_names[v1] != d) << (saint_bit - 1)); distinct_names[v1] = d;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; d += (p < 0); p &= saint_max; SaTy v = buckets_index2(T[p - 1], T[p - 2] >= T[p - 1]);
            SA[induction_bucket[v]++] = (p - 1) | ((SaTy)(distinct_names[v] != d) << (saint_bit - 1)); distinct_names[v] = d;
        }

        return d;
    }

    static void partial_sorting_scan_left_to_right_16u_block_prepare(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size, ThreadState* RESTRICT state)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[0 * alphabet_size];
        SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

        std::memset(buckets, 0, 4 * alphabet_size * sizeof(SaTy));

        fast_sint_t i, j, count = 0; SaTy d = 1;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchr(&SA[i + 2 * prefetch_distance]);

            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 2);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 2);

            SaTy p0 = cache[count].index = SA[i + 0]; d += (p0 < 0); p0 &= saint_max; SaTy v0 = cache[count++].symbol = buckets_index2(T[p0 - 1], T[p0 - 2] >= T[p0 - 1]); induction_bucket[v0]++; distinct_names[v0] = d;
            SaTy p1 = cache[count].index = SA[i + 1]; d += (p1 < 0); p1 &= saint_max; SaTy v1 = cache[count++].symbol = buckets_index2(T[p1 - 1], T[p1 - 2] >= T[p1 - 1]); induction_bucket[v1]++; distinct_names[v1] = d;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = cache[count].index = SA[i]; d += (p < 0); p &= saint_max; SaTy v = cache[count++].symbol = buckets_index2(T[p - 1], T[p - 2] >= T[p - 1]); induction_bucket[v]++; distinct_names[v] = d;
        }

        state[0].state.position = (fast_sint_t)d - 1;
        state[0].state.count = count;
    }

    static void partial_sorting_scan_left_to_right_16u_block_place(SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t count, SaTy d)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[0 * alphabet_size];
        SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

        fast_sint_t i, j;
        for (i = 0, j = count - 1; i < j; i += 2)
        {
            prefetchr(&cache[i + prefetch_distance]);

            SaTy p0 = cache[i + 0].index; d += (p0 < 0); SaTy v0 = cache[i + 0].symbol;
            SA[induction_bucket[v0]++] = (p0 - 1) | ((SaTy)(distinct_names[v0] != d) << (saint_bit - 1)); distinct_names[v0] = d;

            SaTy p1 = cache[i + 1].index; d += (p1 < 0); SaTy v1 = cache[i + 1].symbol;
            SA[induction_bucket[v1]++] = (p1 - 1) | ((SaTy)(distinct_names[v1] != d) << (saint_bit - 1)); distinct_names[v1] = d;
        }

        for (j += 1; i < j; i += 1)
        {
            SaTy p = cache[i].index; d += (p < 0); SaTy v = cache[i].symbol;
            SA[induction_bucket[v]++] = (p - 1) | ((SaTy)(distinct_names[v] != d) << (saint_bit - 1)); distinct_names[v] = d;
        }
    }

    static SaTy partial_sorting_scan_left_to_right_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                d = partial_sorting_scan_left_to_right_16u(T, SA, buckets, d, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_left_to_right_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size, &thread_state[id]);

                mp::barrier(barrier);
                if (id == 0)
                {
                    SaTy* RESTRICT induction_bucket = &buckets[4 * alphabet_size];
                    SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

                    fast_sint_t t;
                    for (t = 0; t < num_threads; ++t)
                    {
                        SaTy* RESTRICT temp_induction_bucket = &thread_state[t].state.buckets[0 * alphabet_size];
                        SaTy* RESTRICT temp_distinct_names = &thread_state[t].state.buckets[2 * alphabet_size];

                        fast_sint_t c;
                        for (c = 0; c < 2 * alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_induction_bucket[c]; induction_bucket[c] = A + B; temp_induction_bucket[c] = A; }

                        for (d -= 1, c = 0; c < 2 * alphabet_size; c += 1) { SaTy A = distinct_names[c], B = temp_distinct_names[c], D = B + d; distinct_names[c] = B > 0 ? D : A; temp_distinct_names[c] = A; }
                        d += 1 + (SaTy)thread_state[t].state.position; thread_state[t].state.position = (fast_sint_t)d - thread_state[t].state.position;
                    }
                }
                mp::barrier(barrier);

                partial_sorting_scan_left_to_right_16u_block_place(SA, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count, (SaTy)thread_state[id].state.position);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});

        return d;
    }

    static SaTy partial_sorting_scan_left_to_right_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, SaTy left_suffixes_count, SaTy d, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy* RESTRICT induction_bucket = &buckets[4 * alphabet_size];
        SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

        SA[induction_bucket[buckets_index2(T[n - 1], T[n - 2] >= T[n - 1])]++] = (n - 1) | saint_min;
        distinct_names[buckets_index2(T[n - 1], T[n - 2] >= T[n - 1])] = ++d;

        if (mp::getPoolSize(pool) == 1 || left_suffixes_count < 65536)
        {
            d = partial_sorting_scan_left_to_right_16u(T, SA, buckets, d, 0, left_suffixes_count);
        }
        else
        {
            fast_sint_t block_start;
            for (block_start = 0; block_start < left_suffixes_count; )
            {
                if (SA[block_start] == 0)
                {
                    block_start++;
                }
                else
                {
                    fast_sint_t block_max_end = block_start + ((fast_sint_t)mp::getPoolSize(pool)) * (per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)); if (block_max_end > left_suffixes_count) { block_max_end = left_suffixes_count; }
                    fast_sint_t block_end = block_start + 1; while (block_end < block_max_end && SA[block_end] != 0) { block_end++; }
                    fast_sint_t block_size = block_end - block_start;

                    if (block_size < 32)
                    {
                        for (; block_start < block_end; block_start += 1)
                        {
                            SaTy p = SA[block_start]; d += (p < 0); p &= saint_max; SaTy v = buckets_index2(T[p - 1], T[p - 2] >= T[p - 1]);
                            SA[induction_bucket[v]++] = (p - 1) | ((SaTy)(distinct_names[v] != d) << (saint_bit - 1)); distinct_names[v] = d;
                        }
                    }
                    else
                    {
                        d = partial_sorting_scan_left_to_right_16u_block_omp(T, SA, buckets, d, block_start, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }
        return d;
    }

    static SaTy partial_sorting_scan_left_to_right_32s_6k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 2 * prefetch_distance - 1; i < j; i += 2)
        {
            prefetchr(&SA[i + 3 * prefetch_distance]);

            prefetchr(&T[SA[i + 2 * prefetch_distance + 0] & saint_max] - 1);
            prefetchr(&T[SA[i + 2 * prefetch_distance + 0] & saint_max] - 2);
            prefetchr(&T[SA[i + 2 * prefetch_distance + 1] & saint_max] - 1);
            prefetchr(&T[SA[i + 2 * prefetch_distance + 1] & saint_max] - 2);

            SaTy p0 = SA[i + prefetch_distance + 0] & saint_max; SaTy v0 = buckets_index4(T[p0 - (p0 > 0)], 0); prefetchw(&buckets[v0]);
            SaTy p1 = SA[i + prefetch_distance + 1] & saint_max; SaTy v1 = buckets_index4(T[p1 - (p1 > 0)], 0); prefetchw(&buckets[v1]);

            SaTy p2 = SA[i + 0]; d += (p2 < 0); p2 &= saint_max; SaTy v2 = buckets_index4(T[p2 - 1], T[p2 - 2] >= T[p2 - 1]);
            SA[buckets[v2]++] = (p2 - 1) | ((SaTy)(buckets[2 + v2] != d) << (saint_bit - 1)); buckets[2 + v2] = d;

            SaTy p3 = SA[i + 1]; d += (p3 < 0); p3 &= saint_max; SaTy v3 = buckets_index4(T[p3 - 1], T[p3 - 2] >= T[p3 - 1]);
            SA[buckets[v3]++] = (p3 - 1) | ((SaTy)(buckets[2 + v3] != d) << (saint_bit - 1)); buckets[2 + v3] = d;
        }

        for (j += 2 * prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; d += (p < 0); p &= saint_max; SaTy v = buckets_index4(T[p - 1], T[p - 2] >= T[p - 1]);
            SA[buckets[v]++] = (p - 1) | ((SaTy)(buckets[2 + v] != d) << (saint_bit - 1)); buckets[2 + v] = d;
        }

        return d;
    }

    static SaTy partial_sorting_scan_left_to_right_32s_4k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy k, SaTy* RESTRICT buckets, SaTy d, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[2 * k];
        SaTy* RESTRICT distinct_names = &buckets[0 * k];

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 2 * prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 3 * prefetch_distance]);

            SaTy s0 = SA[i + 2 * prefetch_distance + 0]; const SaTy* Ts0 = &T[s0 & ~suffix_group_marker] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + 2 * prefetch_distance + 1]; const SaTy* Ts1 = &T[s1 & ~suffix_group_marker] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);
            SaTy s2 = SA[i + 1 * prefetch_distance + 0]; if (s2 > 0) { const fast_sint_t Ts2 = T[(s2 & ~suffix_group_marker) - 1]; prefetchw(&induction_bucket[Ts2]); prefetchw(&distinct_names[buckets_index2(Ts2, 0)]); }
            SaTy s3 = SA[i + 1 * prefetch_distance + 1]; if (s3 > 0) { const fast_sint_t Ts3 = T[(s3 & ~suffix_group_marker) - 1]; prefetchw(&induction_bucket[Ts3]); prefetchw(&distinct_names[buckets_index2(Ts3, 0)]); }

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 & saint_max;
            if (p0 > 0)
            {
                SA[i + 0] = 0; d += (p0 >> (suffix_group_bit - 1)); p0 &= ~suffix_group_marker; SaTy v0 = buckets_index2(T[p0 - 1], T[p0 - 2] < T[p0 - 1]);
                SA[induction_bucket[T[p0 - 1]]++] = (p0 - 1) | ((SaTy)(T[p0 - 2] < T[p0 - 1]) << (saint_bit - 1)) | ((SaTy)(distinct_names[v0] != d) << (suffix_group_bit - 1)); distinct_names[v0] = d;
            }

            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 & saint_max;
            if (p1 > 0)
            {
                SA[i + 1] = 0; d += (p1 >> (suffix_group_bit - 1)); p1 &= ~suffix_group_marker; SaTy v1 = buckets_index2(T[p1 - 1], T[p1 - 2] < T[p1 - 1]);
                SA[induction_bucket[T[p1 - 1]]++] = (p1 - 1) | ((SaTy)(T[p1 - 2] < T[p1 - 1]) << (saint_bit - 1)) | ((SaTy)(distinct_names[v1] != d) << (suffix_group_bit - 1)); distinct_names[v1] = d;
            }
        }

        for (j += 2 * prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max;
            if (p > 0)
            {
                SA[i] = 0; d += (p >> (suffix_group_bit - 1)); p &= ~suffix_group_marker; SaTy v = buckets_index2(T[p - 1], T[p - 2] < T[p - 1]);
                SA[induction_bucket[T[p - 1]]++] = (p - 1) | ((SaTy)(T[p - 2] < T[p - 1]) << (saint_bit - 1)) | ((SaTy)(distinct_names[v] != d) << (suffix_group_bit - 1)); distinct_names[v] = d;
            }
        }

        return d;
    }

    static void partial_sorting_scan_left_to_right_32s_1k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 2 * prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 3 * prefetch_distance]);

            SaTy s0 = SA[i + 2 * prefetch_distance + 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + 2 * prefetch_distance + 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr);
            SaTy s2 = SA[i + 1 * prefetch_distance + 0]; if (s2 > 0) { prefetchw(&induction_bucket[T[s2 - 1]]); prefetchr(&T[s2] - 2); }
            SaTy s3 = SA[i + 1 * prefetch_distance + 1]; if (s3 > 0) { prefetchw(&induction_bucket[T[s3 - 1]]); prefetchr(&T[s3] - 2); }

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 & saint_max; if (p0 > 0) { SA[i + 0] = 0; SA[induction_bucket[T[p0 - 1]]++] = (p0 - 1) | ((SaTy)(T[p0 - 2] < T[p0 - 1]) << (saint_bit - 1)); }
            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 & saint_max; if (p1 > 0) { SA[i + 1] = 0; SA[induction_bucket[T[p1 - 1]]++] = (p1 - 1) | ((SaTy)(T[p1 - 2] < T[p1 - 1]) << (saint_bit - 1)); }
        }

        for (j += 2 * prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { SA[i] = 0; SA[induction_bucket[T[p - 1]]++] = (p - 1) | ((SaTy)(T[p - 2] < T[p - 1]) << (saint_bit - 1)); }
        }
    }

    static void partial_sorting_scan_left_to_right_32s_6k_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchr(&SA[i + 2 * prefetch_distance]);

            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 2);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 2);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy p0 = cache[i + 0].index = SA[i + 0]; SaTy symbol0 = 0; p0 &= saint_max; if (p0 != 0) { symbol0 = buckets_index4(T[p0 - 1], T[p0 - 2] >= T[p0 - 1]); } cache[i + 0].symbol = symbol0;
            SaTy p1 = cache[i + 1].index = SA[i + 1]; SaTy symbol1 = 0; p1 &= saint_max; if (p1 != 0) { symbol1 = buckets_index4(T[p1 - 1], T[p1 - 2] >= T[p1 - 1]); } cache[i + 1].symbol = symbol1;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = cache[i].index = SA[i]; SaTy symbol = 0; p &= saint_max; if (p != 0) { symbol = buckets_index4(T[p - 1], T[p - 2] >= T[p - 1]); } cache[i].symbol = symbol;
        }
    }

    static void partial_sorting_scan_left_to_right_32s_4k_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const SaTy* Ts0 = &T[s0 & ~suffix_group_marker] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const SaTy* Ts1 = &T[s1 & ~suffix_group_marker] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy symbol0 = saint_min, p0 = SA[i + 0]; if (p0 > 0) { cache[i + 0].index = p0; p0 &= ~suffix_group_marker; symbol0 = buckets_index2(T[p0 - 1], T[p0 - 2] < T[p0 - 1]); p0 = 0; } cache[i + 0].symbol = symbol0; SA[i + 0] = p0 & saint_max;
            SaTy symbol1 = saint_min, p1 = SA[i + 1]; if (p1 > 0) { cache[i + 1].index = p1; p1 &= ~suffix_group_marker; symbol1 = buckets_index2(T[p1 - 1], T[p1 - 2] < T[p1 - 1]); p1 = 0; } cache[i + 1].symbol = symbol1; SA[i + 1] = p1 & saint_max;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy symbol = saint_min, p = SA[i]; if (p > 0) { cache[i].index = p; p &= ~suffix_group_marker; symbol = buckets_index2(T[p - 1], T[p - 2] < T[p - 1]); p = 0; } cache[i].symbol = symbol; SA[i] = p & saint_max;
        }
    }

    static void partial_sorting_scan_left_to_right_32s_1k_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy symbol0 = saint_min, p0 = SA[i + 0]; if (p0 > 0) { cache[i + 0].index = (p0 - 1) | ((SaTy)(T[p0 - 2] < T[p0 - 1]) << (saint_bit - 1)); symbol0 = T[p0 - 1]; p0 = 0; } cache[i + 0].symbol = symbol0; SA[i + 0] = p0 & saint_max;
            SaTy symbol1 = saint_min, p1 = SA[i + 1]; if (p1 > 0) { cache[i + 1].index = (p1 - 1) | ((SaTy)(T[p1 - 2] < T[p1 - 1]) << (saint_bit - 1)); symbol1 = T[p1 - 1]; p1 = 0; } cache[i + 1].symbol = symbol1; SA[i + 1] = p1 & saint_max;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy symbol = saint_min, p = SA[i]; if (p > 0) { cache[i].index = (p - 1) | ((SaTy)(T[p - 2] < T[p - 1]) << (saint_bit - 1)); symbol = T[p - 1]; p = 0; } cache[i].symbol = symbol; SA[i] = p & saint_max;
        }
    }

    static SaTy partial_sorting_scan_left_to_right_32s_6k_block_sort(const SaTy* RESTRICT T, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j, omp_block_end = omp_block_start + omp_block_size;
        for (i = omp_block_start, j = omp_block_end - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&cache[i + 2 * prefetch_distance]);

            prefetchw(&buckets[cache[i + prefetch_distance + 0].symbol]);
            prefetchw(&buckets[cache[i + prefetch_distance + 1].symbol]);

            SaTy v0 = cache[i + 0].symbol, p0 = cache[i + 0].index; d += (p0 < 0); cache[i + 0].symbol = buckets[v0]++; cache[i + 0].index = (p0 - 1) | ((SaTy)(buckets[2 + v0] != d) << (saint_bit - 1)); buckets[2 + v0] = d;
            if (cache[i + 0].symbol < omp_block_end) { SaTy s = cache[i + 0].symbol, q = (cache[s].index = cache[i + 0].index) & saint_max; cache[s].symbol = buckets_index4(T[q - 1], T[q - 2] >= T[q - 1]); }

            SaTy v1 = cache[i + 1].symbol, p1 = cache[i + 1].index; d += (p1 < 0); cache[i + 1].symbol = buckets[v1]++; cache[i + 1].index = (p1 - 1) | ((SaTy)(buckets[2 + v1] != d) << (saint_bit - 1)); buckets[2 + v1] = d;
            if (cache[i + 1].symbol < omp_block_end) { SaTy s = cache[i + 1].symbol, q = (cache[s].index = cache[i + 1].index) & saint_max; cache[s].symbol = buckets_index4(T[q - 1], T[q - 2] >= T[q - 1]); }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy v = cache[i].symbol, p = cache[i].index; d += (p < 0); cache[i].symbol = buckets[v]++; cache[i].index = (p - 1) | ((SaTy)(buckets[2 + v] != d) << (saint_bit - 1)); buckets[2 + v] = d;
            if (cache[i].symbol < omp_block_end) { SaTy s = cache[i].symbol, q = (cache[s].index = cache[i].index) & saint_max; cache[s].symbol = buckets_index4(T[q - 1], T[q - 2] >= T[q - 1]); }
        }

        return d;
    }

    static SaTy partial_sorting_scan_left_to_right_32s_4k_block_sort(const SaTy* RESTRICT T, SaTy k, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[2 * k];
        SaTy* RESTRICT distinct_names = &buckets[0 * k];

        fast_sint_t i, j, omp_block_end = omp_block_start + omp_block_size;
        for (i = omp_block_start, j = omp_block_end - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&cache[i + 2 * prefetch_distance]);

            SaTy s0 = cache[i + prefetch_distance + 0].symbol; const SaTy* Is0 = &induction_bucket[s0 >> 1]; prefetchw(s0 >= 0 ? Is0 : nullptr); const SaTy* Ds0 = &distinct_names[s0]; prefetchw(s0 >= 0 ? Ds0 : nullptr);
            SaTy s1 = cache[i + prefetch_distance + 1].symbol; const SaTy* Is1 = &induction_bucket[s1 >> 1]; prefetchw(s1 >= 0 ? Is1 : nullptr); const SaTy* Ds1 = &distinct_names[s1]; prefetchw(s1 >= 0 ? Ds1 : nullptr);

            SaTy v0 = cache[i + 0].symbol;
            if (v0 >= 0)
            {
                SaTy p0 = cache[i + 0].index; d += (p0 >> (suffix_group_bit - 1)); cache[i + 0].symbol = induction_bucket[v0 >> 1]++; cache[i + 0].index = (p0 - 1) | (v0 << (saint_bit - 1)) | ((SaTy)(distinct_names[v0] != d) << (suffix_group_bit - 1)); distinct_names[v0] = d;
                if (cache[i + 0].symbol < omp_block_end) { SaTy ni = cache[i + 0].symbol, np = cache[i + 0].index; if (np > 0) { cache[ni].index = np; np &= ~suffix_group_marker; cache[ni].symbol = buckets_index2(T[np - 1], T[np - 2] < T[np - 1]); np = 0; } cache[i + 0].index = np & saint_max; }
            }

            SaTy v1 = cache[i + 1].symbol;
            if (v1 >= 0)
            {
                SaTy p1 = cache[i + 1].index; d += (p1 >> (suffix_group_bit - 1)); cache[i + 1].symbol = induction_bucket[v1 >> 1]++; cache[i + 1].index = (p1 - 1) | (v1 << (saint_bit - 1)) | ((SaTy)(distinct_names[v1] != d) << (suffix_group_bit - 1)); distinct_names[v1] = d;
                if (cache[i + 1].symbol < omp_block_end) { SaTy ni = cache[i + 1].symbol, np = cache[i + 1].index; if (np > 0) { cache[ni].index = np; np &= ~suffix_group_marker; cache[ni].symbol = buckets_index2(T[np - 1], T[np - 2] < T[np - 1]); np = 0; } cache[i + 1].index = np & saint_max; }
            }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy v = cache[i].symbol;
            if (v >= 0)
            {
                SaTy p = cache[i].index; d += (p >> (suffix_group_bit - 1)); cache[i].symbol = induction_bucket[v >> 1]++; cache[i].index = (p - 1) | (v << (saint_bit - 1)) | ((SaTy)(distinct_names[v] != d) << (suffix_group_bit - 1)); distinct_names[v] = d;
                if (cache[i].symbol < omp_block_end) { SaTy ni = cache[i].symbol, np = cache[i].index; if (np > 0) { cache[ni].index = np; np &= ~suffix_group_marker; cache[ni].symbol = buckets_index2(T[np - 1], T[np - 2] < T[np - 1]); np = 0; } cache[i].index = np & saint_max; }
            }
        }

        return d;
    }

    static void partial_sorting_scan_left_to_right_32s_1k_block_sort(const SaTy* RESTRICT T, SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j, omp_block_end = omp_block_start + omp_block_size;
        for (i = omp_block_start, j = omp_block_end - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&cache[i + 2 * prefetch_distance]);

            SaTy s0 = cache[i + prefetch_distance + 0].symbol; const SaTy* Is0 = &induction_bucket[s0]; prefetchw(s0 >= 0 ? Is0 : nullptr);
            SaTy s1 = cache[i + prefetch_distance + 1].symbol; const SaTy* Is1 = &induction_bucket[s1]; prefetchw(s1 >= 0 ? Is1 : nullptr);

            SaTy v0 = cache[i + 0].symbol;
            if (v0 >= 0)
            {
                cache[i + 0].symbol = induction_bucket[v0]++;
                if (cache[i + 0].symbol < omp_block_end) { SaTy ni = cache[i + 0].symbol, np = cache[i + 0].index; if (np > 0) { cache[ni].index = (np - 1) | ((SaTy)(T[np - 2] < T[np - 1]) << (saint_bit - 1)); cache[ni].symbol = T[np - 1]; np = 0; } cache[i + 0].index = np & saint_max; }
            }

            SaTy v1 = cache[i + 1].symbol;
            if (v1 >= 0)
            {
                cache[i + 1].symbol = induction_bucket[v1]++;
                if (cache[i + 1].symbol < omp_block_end) { SaTy ni = cache[i + 1].symbol, np = cache[i + 1].index; if (np > 0) { cache[ni].index = (np - 1) | ((SaTy)(T[np - 2] < T[np - 1]) << (saint_bit - 1)); cache[ni].symbol = T[np - 1]; np = 0; } cache[i + 1].index = np & saint_max; }
            }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy v = cache[i].symbol;
            if (v >= 0)
            {
                cache[i].symbol = induction_bucket[v]++;
                if (cache[i].symbol < omp_block_end) { SaTy ni = cache[i].symbol, np = cache[i].index; if (np > 0) { cache[ni].index = (np - 1) | ((SaTy)(T[np - 2] < T[np - 1]) << (saint_bit - 1)); cache[ni].symbol = T[np - 1]; np = 0; } cache[i].index = np & saint_max; }
            }
        }
    }

    static SaTy partial_sorting_scan_left_to_right_32s_6k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                d = partial_sorting_scan_left_to_right_32s_6k(T, SA, buckets, d, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_left_to_right_32s_6k_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if(id == 0)
                {
                    d = partial_sorting_scan_left_to_right_32s_6k_block_sort(T, buckets, d, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});

        return d;
    }

    static SaTy partial_sorting_scan_left_to_right_32s_4k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy k, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                d = partial_sorting_scan_left_to_right_32s_4k(T, SA, k, buckets, d, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_left_to_right_32s_4k_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if(id == 0)
                {
                    d = partial_sorting_scan_left_to_right_32s_4k_block_sort(T, k, buckets, d, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                compact_and_place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});
        return d;
    }

    static void partial_sorting_scan_left_to_right_32s_1k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                partial_sorting_scan_left_to_right_32s_1k(T, SA, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_left_to_right_32s_1k_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if(id == 0)
                {
                    partial_sorting_scan_left_to_right_32s_1k_block_sort(T, buckets, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                compact_and_place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});
    }

    static SaTy partial_sorting_scan_left_to_right_32s_6k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, SaTy left_suffixes_count, SaTy d, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SA[buckets[buckets_index4(T[n - 1], T[n - 2] >= T[n - 1])]++] = (n - 1) | saint_min;
        buckets[2 + buckets_index4(T[n - 1], T[n - 2] >= T[n - 1])] = ++d;

        if (mp::getPoolSize(pool) == 1 || left_suffixes_count < 65536)
        {
            d = partial_sorting_scan_left_to_right_32s_6k(T, SA, buckets, d, 0, left_suffixes_count);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = 0; block_start < left_suffixes_count; block_start = block_end)
            {
                block_end = block_start + (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end > left_suffixes_count) { block_end = left_suffixes_count; }

                d = partial_sorting_scan_left_to_right_32s_6k_block_omp(T, SA, buckets, d, thread_state[0].state.cache, block_start, block_end - block_start, pool);
            }
        }
        return d;
    }

    static SaTy partial_sorting_scan_left_to_right_32s_4k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, SaTy d, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy* RESTRICT induction_bucket = &buckets[2 * k];
        SaTy* RESTRICT distinct_names = &buckets[0 * k];

        SA[induction_bucket[T[n - 1]]++] = (n - 1) | ((SaTy)(T[n - 2] < T[n - 1]) << (saint_bit - 1)) | suffix_group_marker;
        distinct_names[buckets_index2(T[n - 1], T[n - 2] < T[n - 1])] = ++d;

        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            d = partial_sorting_scan_left_to_right_32s_4k(T, SA, k, buckets, d, 0, n);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = 0; block_start < n; block_start = block_end)
            {
                block_end = block_start + (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end > n) { block_end = n; }

                d = partial_sorting_scan_left_to_right_32s_4k_block_omp(T, SA, k, buckets, d, thread_state[0].state.cache, block_start, block_end - block_start, pool);
            }
        }
        return d;
    }

    static void partial_sorting_scan_left_to_right_32s_1k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SA[buckets[T[n - 1]]++] = (n - 1) | ((SaTy)(T[n - 2] < T[n - 1]) << (saint_bit - 1));

        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            partial_sorting_scan_left_to_right_32s_1k(T, SA, buckets, 0, n);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = 0; block_start < n; block_start = block_end)
            {
                block_end = block_start + (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end > n) { block_end = n; }

                partial_sorting_scan_left_to_right_32s_1k_block_omp(T, SA, buckets, thread_state[0].state.cache, block_start, block_end - block_start, pool);
            }
        }
    }

    static void partial_sorting_shift_markers_16u_omp(SaTy* RESTRICT SA, SaTy n, const SaTy* RESTRICT buckets, mp::ThreadPool* pool)
    {
        const fast_sint_t prefetch_distance = 32;
        const SaTy* RESTRICT temp_bucket = &buckets[4 * alphabet_size];
        mp::forParallel(pool, buckets_index2(alphabet_size - 1, 0), buckets_index2(1, 0), buckets_index2(1, 0), [&](size_t id, size_t numThreads, ptrdiff_t start, ptrdiff_t stop, ptrdiff_t step, mp::Barrier* barrier)
        {
            for (auto c = start; c >= stop; c -= buckets_index2(1, 0))
            {
                fast_sint_t i, j; SaTy s = saint_min;
                for (i = (fast_sint_t)temp_bucket[c] - 1, j = (fast_sint_t)buckets[c - buckets_index2(1, 0)] + 3; i >= j; i -= 4)
                {
                    prefetchw(&SA[i - prefetch_distance]);

                    SaTy p0 = SA[i - 0], q0 = (p0 & saint_min) ^ s; s = s ^ q0; SA[i - 0] = p0 ^ q0;
                    SaTy p1 = SA[i - 1], q1 = (p1 & saint_min) ^ s; s = s ^ q1; SA[i - 1] = p1 ^ q1;
                    SaTy p2 = SA[i - 2], q2 = (p2 & saint_min) ^ s; s = s ^ q2; SA[i - 2] = p2 ^ q2;
                    SaTy p3 = SA[i - 3], q3 = (p3 & saint_min) ^ s; s = s ^ q3; SA[i - 3] = p3 ^ q3;
                }

                for (j -= 3; i >= j; i -= 1)
                {
                    SaTy p = SA[i], q = (p & saint_min) ^ s; s = s ^ q; SA[i] = p ^ q;
                }
            }
        }, mp::ParallelCond{n >= 65536});
    }

    static void partial_sorting_shift_markers_32s_6k_omp(SaTy* RESTRICT SA, SaTy k, const SaTy* RESTRICT buckets, mp::ThreadPool* pool)
    {
        const fast_sint_t prefetch_distance = 32;
        const SaTy* RESTRICT temp_bucket = &buckets[4 * k];
        mp::forParallel(pool, k - 1, 1, -1, [&](size_t id, size_t numThreads, ptrdiff_t start, ptrdiff_t stop, ptrdiff_t step, mp::Barrier* barrier)
        {
            for (auto c = start; c >= stop; c -= 1)
            {
                fast_sint_t i, j; SaTy s = saint_min;
                for (i = (fast_sint_t)buckets[buckets_index4(c, 0)] - 1, j = (fast_sint_t)temp_bucket[buckets_index2(c - 1, 0)] + 3; i >= j; i -= 4)
                {
                    prefetchw(&SA[i - prefetch_distance]);

                    SaTy p0 = SA[i - 0], q0 = (p0 & saint_min) ^ s; s = s ^ q0; SA[i - 0] = p0 ^ q0;
                    SaTy p1 = SA[i - 1], q1 = (p1 & saint_min) ^ s; s = s ^ q1; SA[i - 1] = p1 ^ q1;
                    SaTy p2 = SA[i - 2], q2 = (p2 & saint_min) ^ s; s = s ^ q2; SA[i - 2] = p2 ^ q2;
                    SaTy p3 = SA[i - 3], q3 = (p3 & saint_min) ^ s; s = s ^ q3; SA[i - 3] = p3 ^ q3;
                }

                for (j -= 3; i >= j; i -= 1)
                {
                    SaTy p = SA[i], q = (p & saint_min) ^ s; s = s ^ q; SA[i] = p ^ q;
                }
            }
        }, mp::ParallelCond{ k >= 65536 });
    }

    static void partial_sorting_shift_markers_32s_4k(SaTy* RESTRICT SA, SaTy n)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i; SaTy s = suffix_group_marker;
        for (i = (fast_sint_t)n - 1; i >= 3; i -= 4)
        {
            prefetchw(&SA[i - prefetch_distance]);

            SaTy p0 = SA[i - 0], q0 = ((p0 & suffix_group_marker) ^ s) & ((SaTy)(p0 > 0) << ((suffix_group_bit - 1))); s = s ^ q0; SA[i - 0] = p0 ^ q0;
            SaTy p1 = SA[i - 1], q1 = ((p1 & suffix_group_marker) ^ s) & ((SaTy)(p1 > 0) << ((suffix_group_bit - 1))); s = s ^ q1; SA[i - 1] = p1 ^ q1;
            SaTy p2 = SA[i - 2], q2 = ((p2 & suffix_group_marker) ^ s) & ((SaTy)(p2 > 0) << ((suffix_group_bit - 1))); s = s ^ q2; SA[i - 2] = p2 ^ q2;
            SaTy p3 = SA[i - 3], q3 = ((p3 & suffix_group_marker) ^ s) & ((SaTy)(p3 > 0) << ((suffix_group_bit - 1))); s = s ^ q3; SA[i - 3] = p3 ^ q3;
        }

        for (; i >= 0; i -= 1)
        {
            SaTy p = SA[i], q = ((p & suffix_group_marker) ^ s) & ((SaTy)(p > 0) << ((suffix_group_bit - 1))); s = s ^ q; SA[i] = p ^ q;
        }
    }

    static void partial_sorting_shift_buckets_32s_6k(SaTy k, SaTy* RESTRICT buckets)
    {
        SaTy* RESTRICT temp_bucket = &buckets[4 * k];

        fast_sint_t i;
        for (i = buckets_index2(0, 0); i <= buckets_index2((fast_sint_t)k - 1, 0); i += buckets_index2(1, 0))
        {
            buckets[2 * i + buckets_index4(0, 0)] = temp_bucket[i + buckets_index2(0, 0)];
            buckets[2 * i + buckets_index4(0, 1)] = temp_bucket[i + buckets_index2(0, 1)];
        }
    }

    static SaTy partial_sorting_scan_right_to_left_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[0 * alphabet_size];
        SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchr(&SA[i - 2 * prefetch_distance]);

            prefetchr(&T[SA[i - prefetch_distance - 0] & saint_max] - 1);
            prefetchr(&T[SA[i - prefetch_distance - 0] & saint_max] - 2);
            prefetchr(&T[SA[i - prefetch_distance - 1] & saint_max] - 1);
            prefetchr(&T[SA[i - prefetch_distance - 1] & saint_max] - 2);

            SaTy p0 = SA[i - 0]; d += (p0 < 0); p0 &= saint_max; SaTy v0 = buckets_index2(T[p0 - 1], T[p0 - 2] > T[p0 - 1]);
            SA[--induction_bucket[v0]] = (p0 - 1) | ((SaTy)(distinct_names[v0] != d) << (saint_bit - 1)); distinct_names[v0] = d;

            SaTy p1 = SA[i - 1]; d += (p1 < 0); p1 &= saint_max; SaTy v1 = buckets_index2(T[p1 - 1], T[p1 - 2] > T[p1 - 1]);
            SA[--induction_bucket[v1]] = (p1 - 1) | ((SaTy)(distinct_names[v1] != d) << (saint_bit - 1)); distinct_names[v1] = d;
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; d += (p < 0); p &= saint_max; SaTy v = buckets_index2(T[p - 1], T[p - 2] > T[p - 1]);
            SA[--induction_bucket[v]] = (p - 1) | ((SaTy)(distinct_names[v] != d) << (saint_bit - 1)); distinct_names[v] = d;
        }

        return d;
    }

    static void partial_sorting_scan_right_to_left_16u_block_prepare(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size, ThreadState* RESTRICT state)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[0 * alphabet_size];
        SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

        std::memset(buckets, 0, 4 * alphabet_size * sizeof(SaTy));

        fast_sint_t i, j, count = 0; SaTy d = 1;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchr(&SA[i - 2 * prefetch_distance]);

            prefetchr(&T[SA[i - prefetch_distance - 0] & saint_max] - 1);
            prefetchr(&T[SA[i - prefetch_distance - 0] & saint_max] - 2);
            prefetchr(&T[SA[i - prefetch_distance - 1] & saint_max] - 1);
            prefetchr(&T[SA[i - prefetch_distance - 1] & saint_max] - 2);

            SaTy p0 = cache[count].index = SA[i - 0]; d += (p0 < 0); p0 &= saint_max; SaTy v0 = cache[count++].symbol = buckets_index2(T[p0 - 1], T[p0 - 2] > T[p0 - 1]); induction_bucket[v0]++; distinct_names[v0] = d;
            SaTy p1 = cache[count].index = SA[i - 1]; d += (p1 < 0); p1 &= saint_max; SaTy v1 = cache[count++].symbol = buckets_index2(T[p1 - 1], T[p1 - 2] > T[p1 - 1]); induction_bucket[v1]++; distinct_names[v1] = d;
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = cache[count].index = SA[i]; d += (p < 0); p &= saint_max; SaTy v = cache[count++].symbol = buckets_index2(T[p - 1], T[p - 2] > T[p - 1]); induction_bucket[v]++; distinct_names[v] = d;
        }

        state[0].state.position = (fast_sint_t)d - 1;
        state[0].state.count = count;
    }

    static void partial_sorting_scan_right_to_left_16u_block_place(SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t count, SaTy d)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[0 * alphabet_size];
        SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

        fast_sint_t i, j;
        for (i = 0, j = count - 1; i < j; i += 2)
        {
            prefetchr(&cache[i + prefetch_distance]);

            SaTy p0 = cache[i + 0].index; d += (p0 < 0); SaTy v0 = cache[i + 0].symbol;
            SA[--induction_bucket[v0]] = (p0 - 1) | ((SaTy)(distinct_names[v0] != d) << (saint_bit - 1)); distinct_names[v0] = d;

            SaTy p1 = cache[i + 1].index; d += (p1 < 0); SaTy v1 = cache[i + 1].symbol;
            SA[--induction_bucket[v1]] = (p1 - 1) | ((SaTy)(distinct_names[v1] != d) << (saint_bit - 1)); distinct_names[v1] = d;
        }

        for (j += 1; i < j; i += 1)
        {
            SaTy p = cache[i].index; d += (p < 0); SaTy v = cache[i].symbol;
            SA[--induction_bucket[v]] = (p - 1) | ((SaTy)(distinct_names[v] != d) << (saint_bit - 1)); distinct_names[v] = d;
        }
    }

    static SaTy partial_sorting_scan_right_to_left_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                d = partial_sorting_scan_right_to_left_16u(T, SA, buckets, d, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_right_to_left_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size, &thread_state[id]);

                mp::barrier(barrier);
                if (id == 0)
                {
                    SaTy* RESTRICT induction_bucket = &buckets[0 * alphabet_size];
                    SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

                    fast_sint_t t;
                    for (t = num_threads - 1; t >= 0; --t)
                    {
                        SaTy* RESTRICT temp_induction_bucket = &thread_state[t].state.buckets[0 * alphabet_size];
                        SaTy* RESTRICT temp_distinct_names = &thread_state[t].state.buckets[2 * alphabet_size];

                        fast_sint_t c;
                        for (c = 0; c < 2 * alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_induction_bucket[c]; induction_bucket[c] = A - B; temp_induction_bucket[c] = A; }

                        for (d -= 1, c = 0; c < 2 * alphabet_size; c += 1) { SaTy A = distinct_names[c], B = temp_distinct_names[c], D = B + d; distinct_names[c] = B > 0 ? D : A; temp_distinct_names[c] = A; }
                        d += 1 + (SaTy)thread_state[t].state.position; thread_state[t].state.position = (fast_sint_t)d - thread_state[t].state.position;
                    }
                }
                mp::barrier(barrier);

                partial_sorting_scan_right_to_left_16u_block_place(SA, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count, (SaTy)thread_state[id].state.position);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});

        return d;
    }


    static void partial_sorting_scan_right_to_left_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, SaTy first_lms_suffix, SaTy left_suffixes_count, SaTy d, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        fast_sint_t scan_start = (fast_sint_t)left_suffixes_count + 1;
        fast_sint_t scan_end = (fast_sint_t)n - (fast_sint_t)first_lms_suffix;

        if (mp::getPoolSize(pool) == 1 || (scan_end - scan_start) < 65536)
        {
            partial_sorting_scan_right_to_left_16u(T, SA, buckets, d, scan_start, scan_end - scan_start);
        }
        else
        {
            SaTy* RESTRICT induction_bucket = &buckets[0 * alphabet_size];
            SaTy* RESTRICT distinct_names = &buckets[2 * alphabet_size];

            fast_sint_t block_start;
            for (block_start = scan_end - 1; block_start >= scan_start; )
            {
                if (SA[block_start] == 0)
                {
                    block_start--;
                }
                else
                {
                    fast_sint_t block_max_end = block_start - ((fast_sint_t)mp::getPoolSize(pool)) * (per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)); if (block_max_end < scan_start) { block_max_end = scan_start - 1; }
                    fast_sint_t block_end = block_start - 1; while (block_end > block_max_end && SA[block_end] != 0) { block_end--; }
                    fast_sint_t block_size = block_start - block_end;

                    if (block_size < 32)
                    {
                        for (; block_start > block_end; block_start -= 1)
                        {
                            SaTy p = SA[block_start]; d += (p < 0); p &= saint_max; SaTy v = buckets_index2(T[p - 1], T[p - 2] > T[p - 1]);
                            SA[--induction_bucket[v]] = (p - 1) | ((SaTy)(distinct_names[v] != d) << (saint_bit - 1)); distinct_names[v] = d;
                        }
                    }
                    else
                    {
                        d = partial_sorting_scan_right_to_left_16u_block_omp(T, SA, buckets, d, block_end + 1, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }
    }

    static SaTy partial_sorting_scan_right_to_left_32s_6k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + 2 * prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchr(&SA[i - 3 * prefetch_distance]);

            prefetchr(&T[SA[i - 2 * prefetch_distance - 0] & saint_max] - 1);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 0] & saint_max] - 2);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 1] & saint_max] - 1);
            prefetchr(&T[SA[i - 2 * prefetch_distance - 1] & saint_max] - 2);

            SaTy p0 = SA[i - prefetch_distance - 0] & saint_max; SaTy v0 = buckets_index4(T[p0 - (p0 > 0)], 0); prefetchw(&buckets[v0]);
            SaTy p1 = SA[i - prefetch_distance - 1] & saint_max; SaTy v1 = buckets_index4(T[p1 - (p1 > 0)], 0); prefetchw(&buckets[v1]);

            SaTy p2 = SA[i - 0]; d += (p2 < 0); p2 &= saint_max; SaTy v2 = buckets_index4(T[p2 - 1], T[p2 - 2] > T[p2 - 1]);
            SA[--buckets[v2]] = (p2 - 1) | ((SaTy)(buckets[2 + v2] != d) << (saint_bit - 1)); buckets[2 + v2] = d;

            SaTy p3 = SA[i - 1]; d += (p3 < 0); p3 &= saint_max; SaTy v3 = buckets_index4(T[p3 - 1], T[p3 - 2] > T[p3 - 1]);
            SA[--buckets[v3]] = (p3 - 1) | ((SaTy)(buckets[2 + v3] != d) << (saint_bit - 1)); buckets[2 + v3] = d;
        }

        for (j -= 2 * prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; d += (p < 0); p &= saint_max; SaTy v = buckets_index4(T[p - 1], T[p - 2] > T[p - 1]);
            SA[--buckets[v]] = (p - 1) | ((SaTy)(buckets[2 + v] != d) << (saint_bit - 1)); buckets[2 + v] = d;
        }

        return d;
    }

    static SaTy partial_sorting_scan_right_to_left_32s_4k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy k, SaTy* RESTRICT buckets, SaTy d, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[3 * k];
        SaTy* RESTRICT distinct_names = &buckets[0 * k];

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + 2 * prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 3 * prefetch_distance]);

            SaTy s0 = SA[i - 2 * prefetch_distance - 0]; const SaTy* Ts0 = &T[s0 & ~suffix_group_marker] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - 2 * prefetch_distance - 1]; const SaTy* Ts1 = &T[s1 & ~suffix_group_marker] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);
            SaTy s2 = SA[i - 1 * prefetch_distance - 0]; if (s2 > 0) { const fast_sint_t Ts2 = T[(s2 & ~suffix_group_marker) - 1]; prefetchw(&induction_bucket[Ts2]); prefetchw(&distinct_names[buckets_index2(Ts2, 0)]); }
            SaTy s3 = SA[i - 1 * prefetch_distance - 1]; if (s3 > 0) { const fast_sint_t Ts3 = T[(s3 & ~suffix_group_marker) - 1]; prefetchw(&induction_bucket[Ts3]); prefetchw(&distinct_names[buckets_index2(Ts3, 0)]); }

            SaTy p0 = SA[i - 0];
            if (p0 > 0)
            {
                SA[i - 0] = 0; d += (p0 >> (suffix_group_bit - 1)); p0 &= ~suffix_group_marker; SaTy v0 = buckets_index2(T[p0 - 1], T[p0 - 2] > T[p0 - 1]);
                SA[--induction_bucket[T[p0 - 1]]] = (p0 - 1) | ((SaTy)(T[p0 - 2] > T[p0 - 1]) << (saint_bit - 1)) | ((SaTy)(distinct_names[v0] != d) << (suffix_group_bit - 1)); distinct_names[v0] = d;
            }

            SaTy p1 = SA[i - 1];
            if (p1 > 0)
            {
                SA[i - 1] = 0; d += (p1 >> (suffix_group_bit - 1)); p1 &= ~suffix_group_marker; SaTy v1 = buckets_index2(T[p1 - 1], T[p1 - 2] > T[p1 - 1]);
                SA[--induction_bucket[T[p1 - 1]]] = (p1 - 1) | ((SaTy)(T[p1 - 2] > T[p1 - 1]) << (saint_bit - 1)) | ((SaTy)(distinct_names[v1] != d) << (suffix_group_bit - 1)); distinct_names[v1] = d;
            }
        }

        for (j -= 2 * prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i];
            if (p > 0)
            {
                SA[i] = 0; d += (p >> (suffix_group_bit - 1)); p &= ~suffix_group_marker; SaTy v = buckets_index2(T[p - 1], T[p - 2] > T[p - 1]);
                SA[--induction_bucket[T[p - 1]]] = (p - 1) | ((SaTy)(T[p - 2] > T[p - 1]) << (saint_bit - 1)) | ((SaTy)(distinct_names[v] != d) << (suffix_group_bit - 1)); distinct_names[v] = d;
            }
        }

        return d;
    }

    static void partial_sorting_scan_right_to_left_32s_1k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + 2 * prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 3 * prefetch_distance]);

            SaTy s0 = SA[i - 2 * prefetch_distance - 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - 2 * prefetch_distance - 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr);
            SaTy s2 = SA[i - 1 * prefetch_distance - 0]; if (s2 > 0) { prefetchw(&induction_bucket[T[s2 - 1]]); prefetchr(&T[s2] - 2); }
            SaTy s3 = SA[i - 1 * prefetch_distance - 1]; if (s3 > 0) { prefetchw(&induction_bucket[T[s3 - 1]]); prefetchr(&T[s3] - 2); }

            SaTy p0 = SA[i - 0]; if (p0 > 0) { SA[i - 0] = 0; SA[--induction_bucket[T[p0 - 1]]] = (p0 - 1) | ((SaTy)(T[p0 - 2] > T[p0 - 1]) << (saint_bit - 1)); }
            SaTy p1 = SA[i - 1]; if (p1 > 0) { SA[i - 1] = 0; SA[--induction_bucket[T[p1 - 1]]] = (p1 - 1) | ((SaTy)(T[p1 - 2] > T[p1 - 1]) << (saint_bit - 1)); }
        }

        for (j -= 2 * prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; if (p > 0) { SA[i] = 0; SA[--induction_bucket[T[p - 1]]] = (p - 1) | ((SaTy)(T[p - 2] > T[p - 1]) << (saint_bit - 1)); }
        }
    }

    static void partial_sorting_scan_right_to_left_32s_6k_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchr(&SA[i + 2 * prefetch_distance]);

            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 0] & saint_max] - 2);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 1);
            prefetchr(&T[SA[i + prefetch_distance + 1] & saint_max] - 2);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy p0 = cache[i + 0].index = SA[i + 0]; SaTy symbol0 = 0; p0 &= saint_max; if (p0 != 0) { symbol0 = buckets_index4(T[p0 - 1], T[p0 - 2] > T[p0 - 1]); } cache[i + 0].symbol = symbol0;
            SaTy p1 = cache[i + 1].index = SA[i + 1]; SaTy symbol1 = 0; p1 &= saint_max; if (p1 != 0) { symbol1 = buckets_index4(T[p1 - 1], T[p1 - 2] > T[p1 - 1]); } cache[i + 1].symbol = symbol1;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = cache[i].index = SA[i]; SaTy symbol = 0; p &= saint_max; if (p != 0) { symbol = buckets_index4(T[p - 1], T[p - 2] > T[p - 1]); } cache[i].symbol = symbol;
        }
    }

    static void partial_sorting_scan_right_to_left_32s_4k_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const SaTy* Ts0 = &T[s0 & ~suffix_group_marker] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const SaTy* Ts1 = &T[s1 & ~suffix_group_marker] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy symbol0 = saint_min, p0 = SA[i + 0]; if (p0 > 0) { SA[i + 0] = 0; cache[i + 0].index = p0; p0 &= ~suffix_group_marker; symbol0 = buckets_index2(T[p0 - 1], T[p0 - 2] > T[p0 - 1]); } cache[i + 0].symbol = symbol0;
            SaTy symbol1 = saint_min, p1 = SA[i + 1]; if (p1 > 0) { SA[i + 1] = 0; cache[i + 1].index = p1; p1 &= ~suffix_group_marker; symbol1 = buckets_index2(T[p1 - 1], T[p1 - 2] > T[p1 - 1]); } cache[i + 1].symbol = symbol1;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy symbol = saint_min, p = SA[i]; if (p > 0) { SA[i] = 0; cache[i].index = p; p &= ~suffix_group_marker; symbol = buckets_index2(T[p - 1], T[p - 2] > T[p - 1]); } cache[i].symbol = symbol;
        }
    }

    static void partial_sorting_scan_right_to_left_32s_1k_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy symbol0 = saint_min, p0 = SA[i + 0]; if (p0 > 0) { SA[i + 0] = 0; cache[i + 0].index = (p0 - 1) | ((SaTy)(T[p0 - 2] > T[p0 - 1]) << (saint_bit - 1)); symbol0 = T[p0 - 1]; } cache[i + 0].symbol = symbol0;
            SaTy symbol1 = saint_min, p1 = SA[i + 1]; if (p1 > 0) { SA[i + 1] = 0; cache[i + 1].index = (p1 - 1) | ((SaTy)(T[p1 - 2] > T[p1 - 1]) << (saint_bit - 1)); symbol1 = T[p1 - 1]; } cache[i + 1].symbol = symbol1;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy symbol = saint_min, p = SA[i]; if (p > 0) { SA[i] = 0; cache[i].index = (p - 1) | ((SaTy)(T[p - 2] > T[p - 1]) << (saint_bit - 1)); symbol = T[p - 1]; } cache[i].symbol = symbol;
        }
    }

    static SaTy partial_sorting_scan_right_to_left_32s_6k_block_sort(const SaTy* RESTRICT T, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&cache[i - 2 * prefetch_distance]);

            prefetchw(&buckets[cache[i - prefetch_distance - 0].symbol]);
            prefetchw(&buckets[cache[i - prefetch_distance - 1].symbol]);

            SaTy v0 = cache[i - 0].symbol, p0 = cache[i - 0].index; d += (p0 < 0); cache[i - 0].symbol = --buckets[v0]; cache[i - 0].index = (p0 - 1) | ((SaTy)(buckets[2 + v0] != d) << (saint_bit - 1)); buckets[2 + v0] = d;
            if (cache[i - 0].symbol >= omp_block_start) { SaTy s = cache[i - 0].symbol, q = (cache[s].index = cache[i - 0].index) & saint_max; cache[s].symbol = buckets_index4(T[q - 1], T[q - 2] > T[q - 1]); }

            SaTy v1 = cache[i - 1].symbol, p1 = cache[i - 1].index; d += (p1 < 0); cache[i - 1].symbol = --buckets[v1]; cache[i - 1].index = (p1 - 1) | ((SaTy)(buckets[2 + v1] != d) << (saint_bit - 1)); buckets[2 + v1] = d;
            if (cache[i - 1].symbol >= omp_block_start) { SaTy s = cache[i - 1].symbol, q = (cache[s].index = cache[i - 1].index) & saint_max; cache[s].symbol = buckets_index4(T[q - 1], T[q - 2] > T[q - 1]); }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy v = cache[i].symbol, p = cache[i].index; d += (p < 0); cache[i].symbol = --buckets[v]; cache[i].index = (p - 1) | ((SaTy)(buckets[2 + v] != d) << (saint_bit - 1)); buckets[2 + v] = d;
            if (cache[i].symbol >= omp_block_start) { SaTy s = cache[i].symbol, q = (cache[s].index = cache[i].index) & saint_max; cache[s].symbol = buckets_index4(T[q - 1], T[q - 2] > T[q - 1]); }
        }

        return d;
    }

    static SaTy partial_sorting_scan_right_to_left_32s_4k_block_sort(const SaTy* RESTRICT T, SaTy k, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT induction_bucket = &buckets[3 * k];
        SaTy* RESTRICT distinct_names = &buckets[0 * k];

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&cache[i - 2 * prefetch_distance]);

            SaTy s0 = cache[i - prefetch_distance - 0].symbol; const SaTy* Is0 = &induction_bucket[s0 >> 1]; prefetchw(s0 >= 0 ? Is0 : nullptr); const SaTy* Ds0 = &distinct_names[s0]; prefetchw(s0 >= 0 ? Ds0 : nullptr);
            SaTy s1 = cache[i - prefetch_distance - 1].symbol; const SaTy* Is1 = &induction_bucket[s1 >> 1]; prefetchw(s1 >= 0 ? Is1 : nullptr); const SaTy* Ds1 = &distinct_names[s1]; prefetchw(s1 >= 0 ? Ds1 : nullptr);

            SaTy v0 = cache[i - 0].symbol;
            if (v0 >= 0)
            {
                SaTy p0 = cache[i - 0].index; d += (p0 >> (suffix_group_bit - 1)); cache[i - 0].symbol = --induction_bucket[v0 >> 1]; cache[i - 0].index = (p0 - 1) | (v0 << (saint_bit - 1)) | ((SaTy)(distinct_names[v0] != d) << (suffix_group_bit - 1)); distinct_names[v0] = d;
                if (cache[i - 0].symbol >= omp_block_start) { SaTy ni = cache[i - 0].symbol, np = cache[i - 0].index; if (np > 0) { cache[i - 0].index = 0; cache[ni].index = np; np &= ~suffix_group_marker; cache[ni].symbol = buckets_index2(T[np - 1], T[np - 2] > T[np - 1]); } }
            }

            SaTy v1 = cache[i - 1].symbol;
            if (v1 >= 0)
            {
                SaTy p1 = cache[i - 1].index; d += (p1 >> (suffix_group_bit - 1)); cache[i - 1].symbol = --induction_bucket[v1 >> 1]; cache[i - 1].index = (p1 - 1) | (v1 << (saint_bit - 1)) | ((SaTy)(distinct_names[v1] != d) << (suffix_group_bit - 1)); distinct_names[v1] = d;
                if (cache[i - 1].symbol >= omp_block_start) { SaTy ni = cache[i - 1].symbol, np = cache[i - 1].index; if (np > 0) { cache[i - 1].index = 0; cache[ni].index = np; np &= ~suffix_group_marker; cache[ni].symbol = buckets_index2(T[np - 1], T[np - 2] > T[np - 1]); } }
            }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy v = cache[i].symbol;
            if (v >= 0)
            {
                SaTy p = cache[i].index; d += (p >> (suffix_group_bit - 1)); cache[i].symbol = --induction_bucket[v >> 1]; cache[i].index = (p - 1) | (v << (saint_bit - 1)) | ((SaTy)(distinct_names[v] != d) << (suffix_group_bit - 1)); distinct_names[v] = d;
                if (cache[i].symbol >= omp_block_start) { SaTy ni = cache[i].symbol, np = cache[i].index; if (np > 0) { cache[i].index = 0; cache[ni].index = np; np &= ~suffix_group_marker; cache[ni].symbol = buckets_index2(T[np - 1], T[np - 2] > T[np - 1]); } }
            }
        }

        return d;
    }

    static void partial_sorting_scan_right_to_left_32s_1k_block_sort(const SaTy* RESTRICT T, SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&cache[i - 2 * prefetch_distance]);

            SaTy s0 = cache[i - prefetch_distance - 0].symbol; const SaTy* Is0 = &induction_bucket[s0]; prefetchw(s0 >= 0 ? Is0 : nullptr);
            SaTy s1 = cache[i - prefetch_distance - 1].symbol; const SaTy* Is1 = &induction_bucket[s1]; prefetchw(s1 >= 0 ? Is1 : nullptr);

            SaTy v0 = cache[i - 0].symbol;
            if (v0 >= 0)
            {
                cache[i - 0].symbol = --induction_bucket[v0];
                if (cache[i - 0].symbol >= omp_block_start) { SaTy ni = cache[i - 0].symbol, np = cache[i - 0].index; if (np > 0) { cache[i - 0].index = 0; cache[ni].index = (np - 1) | ((SaTy)(T[np - 2] > T[np - 1]) << (saint_bit - 1)); cache[ni].symbol = T[np - 1]; } }
            }

            SaTy v1 = cache[i - 1].symbol;
            if (v1 >= 0)
            {
                cache[i - 1].symbol = --induction_bucket[v1];
                if (cache[i - 1].symbol >= omp_block_start) { SaTy ni = cache[i - 1].symbol, np = cache[i - 1].index; if (np > 0) { cache[i - 1].index = 0; cache[ni].index = (np - 1) | ((SaTy)(T[np - 2] > T[np - 1]) << (saint_bit - 1)); cache[ni].symbol = T[np - 1]; } }
            }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy v = cache[i].symbol;
            if (v >= 0)
            {
                cache[i].symbol = --induction_bucket[v];
                if (cache[i].symbol >= omp_block_start) { SaTy ni = cache[i].symbol, np = cache[i].index; if (np > 0) { cache[i].index = 0; cache[ni].index = (np - 1) | ((SaTy)(T[np - 2] > T[np - 1]) << (saint_bit - 1)); cache[ni].symbol = T[np - 1]; } }
            }
        }
    }

    static SaTy partial_sorting_scan_right_to_left_32s_6k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                d = partial_sorting_scan_right_to_left_32s_6k(T, SA, buckets, d, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_right_to_left_32s_6k_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    d = partial_sorting_scan_right_to_left_32s_6k_block_sort(T, buckets, d, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});

        return d;
    }

    static SaTy partial_sorting_scan_right_to_left_32s_4k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy k, SaTy* RESTRICT buckets, SaTy d, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                d = partial_sorting_scan_right_to_left_32s_4k(T, SA, k, buckets, d, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_right_to_left_32s_4k_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    d = partial_sorting_scan_right_to_left_32s_4k_block_sort(T, k, buckets, d, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                compact_and_place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});

        return d;
    }

    static void partial_sorting_scan_right_to_left_32s_1k_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                partial_sorting_scan_right_to_left_32s_1k(T, SA, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                partial_sorting_scan_right_to_left_32s_1k_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    partial_sorting_scan_right_to_left_32s_1k_block_sort(T, buckets, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                compact_and_place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});
    }

    static SaTy partial_sorting_scan_right_to_left_32s_6k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, SaTy first_lms_suffix, SaTy left_suffixes_count, SaTy d, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        fast_sint_t scan_start = (fast_sint_t)left_suffixes_count + 1;
        fast_sint_t scan_end = (fast_sint_t)n - (fast_sint_t)first_lms_suffix;

        if (mp::getPoolSize(pool) == 1 || (scan_end - scan_start) < 65536)
        {
            d = partial_sorting_scan_right_to_left_32s_6k(T, SA, buckets, d, scan_start, scan_end - scan_start);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = scan_end - 1; block_start >= scan_start; block_start = block_end)
            {
                block_end = block_start - (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end < scan_start) { block_end = scan_start - 1; }

                d = partial_sorting_scan_right_to_left_32s_6k_block_omp(T, SA, buckets, d, thread_state[0].state.cache, block_end + 1, block_start - block_end, pool);
            }
        }
        return d;
    }

    static SaTy partial_sorting_scan_right_to_left_32s_4k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, SaTy d, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            d = partial_sorting_scan_right_to_left_32s_4k(T, SA, k, buckets, d, 0, n);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = (fast_sint_t)n - 1; block_start >= 0; block_start = block_end)
            {
                block_end = block_start - (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end < 0) { block_end = -1; }

                d = partial_sorting_scan_right_to_left_32s_4k_block_omp(T, SA, k, buckets, d, thread_state[0].state.cache, block_end + 1, block_start - block_end, pool);
            }
        }
        return d;
    }

    static void partial_sorting_scan_right_to_left_32s_1k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            partial_sorting_scan_right_to_left_32s_1k(T, SA, buckets, 0, n);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = (fast_sint_t)n - 1; block_start >= 0; block_start = block_end)
            {
                block_end = block_start - (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end < 0) { block_end = -1; }

                partial_sorting_scan_right_to_left_32s_1k_block_omp(T, SA, buckets, thread_state[0].state.cache, block_end + 1, block_start - block_end, pool);
            }
        }
    }

    static fast_sint_t partial_sorting_gather_lms_suffixes_32s_4k(SaTy* RESTRICT SA, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j, l;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 3, l = omp_block_start; i < j; i += 4)
        {
            prefetchr(&SA[i + prefetch_distance]);

            SaTy s0 = SA[i + 0]; SA[l] = (s0 - suffix_group_marker) & (~suffix_group_marker); l += (s0 < 0);
            SaTy s1 = SA[i + 1]; SA[l] = (s1 - suffix_group_marker) & (~suffix_group_marker); l += (s1 < 0);
            SaTy s2 = SA[i + 2]; SA[l] = (s2 - suffix_group_marker) & (~suffix_group_marker); l += (s2 < 0);
            SaTy s3 = SA[i + 3]; SA[l] = (s3 - suffix_group_marker) & (~suffix_group_marker); l += (s3 < 0);
        }

        for (j += 3; i < j; i += 1)
        {
            SaTy s = SA[i]; SA[l] = (s - suffix_group_marker) & (~suffix_group_marker); l += (s < 0);
        }

        return l;
    }

    static fast_sint_t partial_sorting_gather_lms_suffixes_32s_1k(SaTy* RESTRICT SA, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j, l;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 3, l = omp_block_start; i < j; i += 4)
        {
            prefetchr(&SA[i + prefetch_distance]);

            SaTy s0 = SA[i + 0]; SA[l] = s0 & saint_max; l += (s0 < 0);
            SaTy s1 = SA[i + 1]; SA[l] = s1 & saint_max; l += (s1 < 0);
            SaTy s2 = SA[i + 2]; SA[l] = s2 & saint_max; l += (s2 < 0);
            SaTy s3 = SA[i + 3]; SA[l] = s3 & saint_max; l += (s3 < 0);
        }

        for (j += 3; i < j; i += 1)
        {
            SaTy s = SA[i]; SA[l] = s & saint_max; l += (s < 0);
        }

        return l;
    }

    static void partial_sorting_gather_lms_suffixes_32s_4k_omp(SaTy* RESTRICT SA, SaTy n, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            if (num_threads == 1)
            {
                partial_sorting_gather_lms_suffixes_32s_4k(SA, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.position = omp_block_start;
                thread_state[id].state.count = partial_sorting_gather_lms_suffixes_32s_4k(SA, omp_block_start, omp_block_size) - omp_block_start;
            }
        }, mp::parallelFinal([&]()
        {
            fast_sint_t t, position = 0;
            for (t = 0; t < mp::getPoolSize(pool); ++t)
            {
                if (t > 0 && thread_state[t].state.count > 0)
                {
                    memmove(&SA[position], &SA[thread_state[t].state.position], (size_t)thread_state[t].state.count * sizeof(SaTy));
                }

                position += thread_state[t].state.count;
            }
        }), mp::ParallelCond{n >= 65536});
    }

    static void partial_sorting_gather_lms_suffixes_32s_1k_omp(SaTy* RESTRICT SA, SaTy n, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            if (num_threads == 1)
            {
                partial_sorting_gather_lms_suffixes_32s_1k(SA, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.position = omp_block_start;
                thread_state[id].state.count = partial_sorting_gather_lms_suffixes_32s_1k(SA, omp_block_start, omp_block_size) - omp_block_start;
            }
        }, mp::parallelFinal([&]()
        {
            fast_sint_t t, position = 0;
            for (t = 0; t < mp::getPoolSize(pool); ++t)
            {
                if (t > 0 && thread_state[t].state.count > 0)
                {
                    memmove(&SA[position], &SA[thread_state[t].state.position], (size_t)thread_state[t].state.count * sizeof(SaTy));
                }

                position += thread_state[t].state.count;
            }
        }), mp::ParallelCond{n >= 65536});
    }

    static void induce_partial_order_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT buckets, SaTy first_lms_suffix, SaTy left_suffixes_count, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        std::memset(&buckets[2 * alphabet_size], 0, 2 * alphabet_size * sizeof(SaTy));

        SaTy d = partial_sorting_scan_left_to_right_16u_omp(T, SA, n, buckets, left_suffixes_count, 0, pool, thread_state);
        partial_sorting_shift_markers_16u_omp(SA, n, buckets, pool);
        partial_sorting_scan_right_to_left_16u_omp(T, SA, n, buckets, first_lms_suffix, left_suffixes_count, d, pool, thread_state);
    }

    static void induce_partial_order_32s_6k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, SaTy first_lms_suffix, SaTy left_suffixes_count, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy d = partial_sorting_scan_left_to_right_32s_6k_omp(T, SA, n, buckets, left_suffixes_count, 0, pool, thread_state);
        partial_sorting_shift_markers_32s_6k_omp(SA, k, buckets, pool);
        partial_sorting_shift_buckets_32s_6k(k, buckets);
        partial_sorting_scan_right_to_left_32s_6k_omp(T, SA, n, buckets, first_lms_suffix, left_suffixes_count, d, pool, thread_state);
    }

    static void induce_partial_order_32s_4k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        std::memset(buckets, 0, 2 * (size_t)k * sizeof(SaTy));

        SaTy d = partial_sorting_scan_left_to_right_32s_4k_omp(T, SA, n, k, buckets, 0, pool, thread_state);
        partial_sorting_shift_markers_32s_4k(SA, n);
        partial_sorting_scan_right_to_left_32s_4k_omp(T, SA, n, k, buckets, d, pool, thread_state);
        partial_sorting_gather_lms_suffixes_32s_4k_omp(SA, n, pool, thread_state);
    }

    static void induce_partial_order_32s_2k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        partial_sorting_scan_left_to_right_32s_1k_omp(T, SA, n, &buckets[1 * k], pool, thread_state);
        partial_sorting_scan_right_to_left_32s_1k_omp(T, SA, n, &buckets[0 * k], pool, thread_state);
        partial_sorting_gather_lms_suffixes_32s_1k_omp(SA, n, pool, thread_state);
    }

    static void induce_partial_order_32s_1k_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        count_suffixes_32s(T, n, k, buckets);
        initialize_buckets_start_32s_1k(k, buckets);
        partial_sorting_scan_left_to_right_32s_1k_omp(T, SA, n, buckets, pool, thread_state);

        count_suffixes_32s(T, n, k, buckets);
        initialize_buckets_end_32s_1k(k, buckets);
        partial_sorting_scan_right_to_left_32s_1k_omp(T, SA, n, buckets, pool, thread_state);

        partial_sorting_gather_lms_suffixes_32s_1k_omp(SA, n, pool, thread_state);
    }

    static SaTy renumber_lms_suffixes_16u(SaTy* RESTRICT SA, SaTy m, SaTy name, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT SAm = &SA[m];

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchr(&SA[i + 2 * prefetch_distance]);

            prefetchw(&SAm[(SA[i + prefetch_distance + 0] & saint_max) >> 1]);
            prefetchw(&SAm[(SA[i + prefetch_distance + 1] & saint_max) >> 1]);
            prefetchw(&SAm[(SA[i + prefetch_distance + 2] & saint_max) >> 1]);
            prefetchw(&SAm[(SA[i + prefetch_distance + 3] & saint_max) >> 1]);

            SaTy p0 = SA[i + 0]; SAm[(p0 & saint_max) >> 1] = name | saint_min; name += p0 < 0;
            SaTy p1 = SA[i + 1]; SAm[(p1 & saint_max) >> 1] = name | saint_min; name += p1 < 0;
            SaTy p2 = SA[i + 2]; SAm[(p2 & saint_max) >> 1] = name | saint_min; name += p2 < 0;
            SaTy p3 = SA[i + 3]; SAm[(p3 & saint_max) >> 1] = name | saint_min; name += p3 < 0;
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            SaTy p = SA[i]; SAm[(p & saint_max) >> 1] = name | saint_min; name += p < 0;
        }

        return name;
    }

    static fast_sint_t gather_marked_suffixes_16u(SaTy* RESTRICT SA, SaTy m, fast_sint_t l, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        l -= 1;

        fast_sint_t i, j;
        for (i = (fast_sint_t)m + omp_block_start + omp_block_size - 1, j = (fast_sint_t)m + omp_block_start + 3; i >= j; i -= 4)
        {
            prefetchr(&SA[i - prefetch_distance]);

            SaTy s0 = SA[i - 0]; SA[l] = s0 & saint_max; l -= s0 < 0;
            SaTy s1 = SA[i - 1]; SA[l] = s1 & saint_max; l -= s1 < 0;
            SaTy s2 = SA[i - 2]; SA[l] = s2 & saint_max; l -= s2 < 0;
            SaTy s3 = SA[i - 3]; SA[l] = s3 & saint_max; l -= s3 < 0;
        }

        for (j -= 3; i >= j; i -= 1)
        {
            SaTy s = SA[i]; SA[l] = s & saint_max; l -= s < 0;
        }

        l += 1;

        return l;
    }

    static SaTy renumber_lms_suffixes_16u_omp(SaTy* RESTRICT SA, SaTy m, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy name = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (m / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : m - omp_block_start;

            if (num_threads == 1)
            {
                name = renumber_lms_suffixes_16u(SA, m, 0, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = count_negative_marked_suffixes(SA, omp_block_start, omp_block_size);

                mp::barrier(barrier);

                fast_sint_t t, count = 0; for (t = 0; t < id; ++t) { count += thread_state[t].state.count; }

                if (id == num_threads - 1)
                {
                    name = (SaTy)(count + thread_state[id].state.count);
                }

                renumber_lms_suffixes_16u(SA, m, (SaTy)count, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{m >= 65536});

        return name;
    }

    static void gather_marked_lms_suffixes_16u_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy fs, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (((fast_sint_t)n >> 1) / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : ((fast_sint_t)n >> 1) - omp_block_start;

            if (num_threads == 1)
            {
                gather_marked_suffixes_16u(SA, m, (fast_sint_t)n + (fast_sint_t)fs, omp_block_start, omp_block_size);
            }
            else
            {
                if (id < num_threads - 1)
                {
                    thread_state[id].state.position = gather_marked_suffixes_16u(SA, m, (fast_sint_t)m + omp_block_start + omp_block_size, omp_block_start, omp_block_size);
                    thread_state[id].state.count = (fast_sint_t)m + omp_block_start + omp_block_size - thread_state[id].state.position;
                }
                else
                {
                    thread_state[id].state.position = gather_marked_suffixes_16u(SA, m, (fast_sint_t)n + (fast_sint_t)fs, omp_block_start, omp_block_size);
                    thread_state[id].state.count = (fast_sint_t)n + (fast_sint_t)fs - thread_state[id].state.position;
                }
            }
        }, mp::parallelFinal([&]()
        {
            fast_sint_t t, position = (fast_sint_t)n + (fast_sint_t)fs;

            for (t = mp::getPoolSize(pool) - 1; t >= 0; --t)
            {
                position -= thread_state[t].state.count;
                if (t != mp::getPoolSize(pool) - 1 && thread_state[t].state.count > 0)
                {
                    memmove(&SA[position], &SA[thread_state[t].state.position], (size_t)thread_state[t].state.count * sizeof(SaTy));
                }
            }
        }), mp::ParallelCond{n >= 131072});
    }

    static SaTy renumber_and_gather_lms_suffixes_16u_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy fs, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        std::memset(&SA[m], 0, ((size_t)n >> 1) * sizeof(SaTy));

        SaTy name = renumber_lms_suffixes_16u_omp(SA, m, pool, thread_state);
        if (name < m)
        {
            gather_marked_lms_suffixes_16u_omp(SA, n, m, fs, pool, thread_state);
        }
        else
        {
            fast_sint_t i; for (i = 0; i < m; i += 1) { SA[i] &= saint_max; }
        }

        return name;
    }

    static SaTy renumber_distinct_lms_suffixes_32s_4k(SaTy* RESTRICT SA, SaTy m, SaTy name, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT SAm = &SA[m];

        fast_sint_t i, j; SaTy p0, p1, p2, p3 = 0;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            prefetchw(&SAm[(SA[i + prefetch_distance + 0] & saint_max) >> 1]);
            prefetchw(&SAm[(SA[i + prefetch_distance + 1] & saint_max) >> 1]);
            prefetchw(&SAm[(SA[i + prefetch_distance + 2] & saint_max) >> 1]);
            prefetchw(&SAm[(SA[i + prefetch_distance + 3] & saint_max) >> 1]);

            p0 = SA[i + 0]; SAm[(SA[i + 0] = p0 & saint_max) >> 1] = name | (p0 & p3 & saint_min); name += p0 < 0;
            p1 = SA[i + 1]; SAm[(SA[i + 1] = p1 & saint_max) >> 1] = name | (p1 & p0 & saint_min); name += p1 < 0;
            p2 = SA[i + 2]; SAm[(SA[i + 2] = p2 & saint_max) >> 1] = name | (p2 & p1 & saint_min); name += p2 < 0;
            p3 = SA[i + 3]; SAm[(SA[i + 3] = p3 & saint_max) >> 1] = name | (p3 & p2 & saint_min); name += p3 < 0;
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            p2 = p3; p3 = SA[i]; SAm[(SA[i] = p3 & saint_max) >> 1] = name | (p3 & p2 & saint_min); name += p3 < 0;
        }

        return name;
    }

    static void mark_distinct_lms_suffixes_32s(SaTy* RESTRICT SA, SaTy m, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j; SaTy p0, p1, p2, p3 = 0;
        for (i = (fast_sint_t)m + omp_block_start, j = (fast_sint_t)m + omp_block_start + omp_block_size - 3; i < j; i += 4)
        {
            prefetchw(&SA[i + prefetch_distance]);

            p0 = SA[i + 0]; SA[i + 0] = p0 & (p3 | saint_max); p0 = (p0 == 0) ? p3 : p0;
            p1 = SA[i + 1]; SA[i + 1] = p1 & (p0 | saint_max); p1 = (p1 == 0) ? p0 : p1;
            p2 = SA[i + 2]; SA[i + 2] = p2 & (p1 | saint_max); p2 = (p2 == 0) ? p1 : p2;
            p3 = SA[i + 3]; SA[i + 3] = p3 & (p2 | saint_max); p3 = (p3 == 0) ? p2 : p3;
        }

        for (j += 3; i < j; i += 1)
        {
            p2 = p3; p3 = SA[i]; SA[i] = p3 & (p2 | saint_max); p3 = (p3 == 0) ? p2 : p3;
        }
    }

    static void clamp_lms_suffixes_length_32s(SaTy* RESTRICT SA, SaTy m, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT SAm = &SA[m];

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 3; i < j; i += 4)
        {
            prefetchw(&SAm[i + prefetch_distance]);

            SAm[i + 0] = (SAm[i + 0] < 0 ? SAm[i + 0] : 0) & saint_max;
            SAm[i + 1] = (SAm[i + 1] < 0 ? SAm[i + 1] : 0) & saint_max;
            SAm[i + 2] = (SAm[i + 2] < 0 ? SAm[i + 2] : 0) & saint_max;
            SAm[i + 3] = (SAm[i + 3] < 0 ? SAm[i + 3] : 0) & saint_max;
        }

        for (j += 3; i < j; i += 1)
        {
            SAm[i] = (SAm[i] < 0 ? SAm[i] : 0) & saint_max;
        }
    }

    static SaTy renumber_distinct_lms_suffixes_32s_4k_omp(SaTy* RESTRICT SA, SaTy m, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy name = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (m / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : m - omp_block_start;

            if (num_threads == 1)
            {
                name = renumber_distinct_lms_suffixes_32s_4k(SA, m, 1, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = count_negative_marked_suffixes(SA, omp_block_start, omp_block_size);

                mp::barrier(barrier);

                fast_sint_t t, count = 1; for (t = 0; t < id; ++t) { count += thread_state[t].state.count; }

                if (id == num_threads - 1)
                {
                    name = (SaTy)(count + thread_state[id].state.count);
                }

                renumber_distinct_lms_suffixes_32s_4k(SA, m, (SaTy)count, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{m >= 65536});

        return name - 1;
    }

    static void mark_distinct_lms_suffixes_32s_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_start = 0;
            fast_sint_t omp_block_size = (fast_sint_t)n >> 1;
            if (num_threads > 1)
            {
                fast_sint_t omp_block_stride = (((fast_sint_t)n >> 1) / num_threads) & (-16);
                omp_block_start = id * omp_block_stride;
                omp_block_size = id < num_threads - 1 ? omp_block_stride : ((fast_sint_t)n >> 1) - omp_block_start;
            }

            mark_distinct_lms_suffixes_32s(SA, m, omp_block_start, omp_block_size);
        }, mp::ParallelCond{n >= 131072});
    }

    static void clamp_lms_suffixes_length_32s_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_start = 0;
            fast_sint_t omp_block_size = (fast_sint_t)n >> 1;
            if (num_threads > 1)
            {
                fast_sint_t omp_block_stride = (((fast_sint_t)n >> 1) / num_threads) & (-16);
                omp_block_start = id * omp_block_stride;
                omp_block_size = id < num_threads - 1 ? omp_block_stride : ((fast_sint_t)n >> 1) - omp_block_start;
            }

            clamp_lms_suffixes_length_32s(SA, m, omp_block_start, omp_block_size);
        }, mp::ParallelCond{n >= 131072});
    }

    static SaTy renumber_and_mark_distinct_lms_suffixes_32s_4k_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        std::memset(&SA[m], 0, ((size_t)n >> 1) * sizeof(SaTy));

        SaTy name = renumber_distinct_lms_suffixes_32s_4k_omp(SA, m, pool, thread_state);
        if (name < m)
        {
            mark_distinct_lms_suffixes_32s_omp(SA, n, m, pool);
        }

        return name;
    }

    static SaTy renumber_and_mark_distinct_lms_suffixes_32s_1k_omp(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, mp::ThreadPool* pool)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT SAm = &SA[m];

        {
            gather_lms_suffixes_32s(T, SA, n);

            std::memset(&SA[m], 0, ((size_t)n - (size_t)m - (size_t)m) * sizeof(SaTy));

            fast_sint_t i, j;
            for (i = (fast_sint_t)n - (fast_sint_t)m, j = (fast_sint_t)n - 1 - prefetch_distance - 3; i < j; i += 4)
            {
                prefetchr(&SA[i + 2 * prefetch_distance]);

                prefetchw(&SAm[to_unsigned(SA[i + prefetch_distance + 0]) >> 1]);
                prefetchw(&SAm[to_unsigned(SA[i + prefetch_distance + 1]) >> 1]);
                prefetchw(&SAm[to_unsigned(SA[i + prefetch_distance + 2]) >> 1]);
                prefetchw(&SAm[to_unsigned(SA[i + prefetch_distance + 3]) >> 1]);

                SAm[to_unsigned(SA[i + 0]) >> 1] = SA[i + 1] - SA[i + 0] + 1 + saint_min;
                SAm[to_unsigned(SA[i + 1]) >> 1] = SA[i + 2] - SA[i + 1] + 1 + saint_min;
                SAm[to_unsigned(SA[i + 2]) >> 1] = SA[i + 3] - SA[i + 2] + 1 + saint_min;
                SAm[to_unsigned(SA[i + 3]) >> 1] = SA[i + 4] - SA[i + 3] + 1 + saint_min;
            }

            for (j += prefetch_distance + 3; i < j; i += 1)
            {
                SAm[to_unsigned(SA[i]) >> 1] = SA[i + 1] - SA[i] + 1 + saint_min;
            }

            SAm[to_unsigned(SA[n - 1]) >> 1] = 1 + saint_min;
        }

        {
            clamp_lms_suffixes_length_32s_omp(SA, n, m, pool);
        }

        SaTy name = 1;

        {
            fast_sint_t i, j, p = SA[0], plen = SAm[p >> 1]; SaTy pdiff = saint_min;
            for (i = 1, j = m - prefetch_distance - 1; i < j; i += 2)
            {
                prefetchr(&SA[i + 2 * prefetch_distance]);

                prefetchw(&SAm[to_unsigned(SA[i + prefetch_distance + 0]) >> 1]); prefetchr(&T[to_unsigned(SA[i + prefetch_distance + 0])]);
                prefetchw(&SAm[to_unsigned(SA[i + prefetch_distance + 1]) >> 1]); prefetchr(&T[to_unsigned(SA[i + prefetch_distance + 1])]);

                fast_sint_t q = SA[i + 0], qlen = SAm[q >> 1]; SaTy qdiff = saint_min;
                if (plen == qlen) { fast_sint_t l = 0; do { if (T[p + l] != T[q + l]) { break; } } while (++l < qlen); qdiff = (SaTy)(l - qlen) & saint_min; }
                SAm[p >> 1] = name | (pdiff & qdiff); name += (qdiff < 0);

                p = SA[i + 1]; plen = SAm[p >> 1]; pdiff = saint_min;
                if (qlen == plen) { fast_sint_t l = 0; do { if (T[q + l] != T[p + l]) { break; } } while (++l < plen); pdiff = (SaTy)(l - plen) & saint_min; }
                SAm[q >> 1] = name | (qdiff & pdiff); name += (pdiff < 0);
            }

            for (j += prefetch_distance + 1; i < j; i += 1)
            {
                fast_sint_t q = SA[i], qlen = SAm[q >> 1]; SaTy qdiff = saint_min;
                if (plen == qlen) { fast_sint_t l = 0; do { if (T[p + l] != T[q + l]) { break; } } while (++l < plen); qdiff = (SaTy)(l - plen) & saint_min; }
                SAm[p >> 1] = name | (pdiff & qdiff); name += (qdiff < 0);

                p = q; plen = qlen; pdiff = qdiff;
            }

            SAm[p >> 1] = name | pdiff; name++;
        }

        if (name <= m)
        {
            mark_distinct_lms_suffixes_32s_omp(SA, n, m, pool);
        }

        return name - 1;
    }

    static void reconstruct_lms_suffixes(SaTy* RESTRICT SA, SaTy n, SaTy m, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        const SaTy* RESTRICT SAnm = &SA[n - m];

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            prefetchr(&SAnm[SA[i + prefetch_distance + 0]]);
            prefetchr(&SAnm[SA[i + prefetch_distance + 1]]);
            prefetchr(&SAnm[SA[i + prefetch_distance + 2]]);
            prefetchr(&SAnm[SA[i + prefetch_distance + 3]]);

            SA[i + 0] = SAnm[SA[i + 0]];
            SA[i + 1] = SAnm[SA[i + 1]];
            SA[i + 2] = SAnm[SA[i + 2]];
            SA[i + 3] = SAnm[SA[i + 3]];
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            SA[i] = SAnm[SA[i]];
        }
    }

    static void reconstruct_lms_suffixes_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_start = 0;
            fast_sint_t omp_block_size = m;
            if (num_threads > 1)
            {
                fast_sint_t omp_block_stride = (m / num_threads) & (-16);
                omp_block_start = id * omp_block_stride;
                omp_block_size = id < num_threads - 1 ? omp_block_stride : m - omp_block_start;
            }

            reconstruct_lms_suffixes(SA, n, m, omp_block_start, omp_block_size);
        }, mp::ParallelCond{m >= 65536});
    }

    static void place_lms_suffixes_interval_16u(SaTy* RESTRICT SA, SaTy n, SaTy m, const SaTy* RESTRICT buckets)
    {
        const SaTy* RESTRICT bucket_end = &buckets[7 * alphabet_size];

        fast_sint_t c, j = n;
        for (c = alphabet_size - 2; c >= 0; --c)
        {
            fast_sint_t l = (fast_sint_t)buckets[buckets_index2(c, 1) + buckets_index2(1, 0)] - (fast_sint_t)buckets[buckets_index2(c, 1)];
            if (l > 0)
            {
                fast_sint_t i = bucket_end[c];
                if (j - i > 0)
                {
                    std::memset(&SA[i], 0, (size_t)(j - i) * sizeof(SaTy));
                }

                memmove(&SA[j = (i - l)], &SA[m -= (SaTy)l], (size_t)l * sizeof(SaTy));
            }
        }

        std::memset(&SA[0], 0, (size_t)j * sizeof(SaTy));
    }

    static void place_lms_suffixes_interval_32s_4k(SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy m, const SaTy* RESTRICT buckets)
    {
        const SaTy* RESTRICT bucket_end = &buckets[3 * k];

        fast_sint_t c, j = n;
        for (c = (fast_sint_t)k - 2; c >= 0; --c)
        {
            fast_sint_t l = (fast_sint_t)buckets[buckets_index2(c, 1) + buckets_index2(1, 0)] - (fast_sint_t)buckets[buckets_index2(c, 1)];
            if (l > 0)
            {
                fast_sint_t i = bucket_end[c];
                if (j - i > 0)
                {
                    std::memset(&SA[i], 0, (size_t)(j - i) * sizeof(SaTy));
                }

                memmove(&SA[j = (i - l)], &SA[m -= (SaTy)l], (size_t)l * sizeof(SaTy));
            }
        }

        std::memset(&SA[0], 0, (size_t)j * sizeof(SaTy));
    }

    static void place_lms_suffixes_interval_32s_2k(SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy m, const SaTy* RESTRICT buckets)
    {
        fast_sint_t j = n;

        if (k > 1)
        {
            fast_sint_t c;
            for (c = buckets_index2((fast_sint_t)k - 2, 0); c >= buckets_index2(0, 0); c -= buckets_index2(1, 0))
            {
                fast_sint_t l = (fast_sint_t)buckets[c + buckets_index2(1, 1)] - (fast_sint_t)buckets[c + buckets_index2(0, 1)];
                if (l > 0)
                {
                    fast_sint_t i = buckets[c];
                    if (j - i > 0)
                    {
                        std::memset(&SA[i], 0, (size_t)(j - i) * sizeof(SaTy));
                    }

                    memmove(&SA[j = (i - l)], &SA[m -= (SaTy)l], (size_t)l * sizeof(SaTy));
                }
            }
        }

        std::memset(&SA[0], 0, (size_t)j * sizeof(SaTy));
    }

    static void place_lms_suffixes_interval_32s_1k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy k, SaTy m, SaTy* RESTRICT buckets)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy c = k - 1; fast_sint_t i, l = buckets[c];
        for (i = (fast_sint_t)m - 1; i >= prefetch_distance + 3; i -= 4)
        {
            prefetchr(&SA[i - 2 * prefetch_distance]);

            prefetchr(&T[SA[i - prefetch_distance - 0]]);
            prefetchr(&T[SA[i - prefetch_distance - 1]]);
            prefetchr(&T[SA[i - prefetch_distance - 2]]);
            prefetchr(&T[SA[i - prefetch_distance - 3]]);

            SaTy p0 = SA[i - 0]; if (T[p0] != c) { c = T[p0]; std::memset(&SA[buckets[c]], 0, (size_t)(l - buckets[c]) * sizeof(SaTy)); l = buckets[c]; } SA[--l] = p0;
            SaTy p1 = SA[i - 1]; if (T[p1] != c) { c = T[p1]; std::memset(&SA[buckets[c]], 0, (size_t)(l - buckets[c]) * sizeof(SaTy)); l = buckets[c]; } SA[--l] = p1;
            SaTy p2 = SA[i - 2]; if (T[p2] != c) { c = T[p2]; std::memset(&SA[buckets[c]], 0, (size_t)(l - buckets[c]) * sizeof(SaTy)); l = buckets[c]; } SA[--l] = p2;
            SaTy p3 = SA[i - 3]; if (T[p3] != c) { c = T[p3]; std::memset(&SA[buckets[c]], 0, (size_t)(l - buckets[c]) * sizeof(SaTy)); l = buckets[c]; } SA[--l] = p3;
        }

        for (; i >= 0; i -= 1)
        {
            SaTy p = SA[i]; if (T[p] != c) { c = T[p]; std::memset(&SA[buckets[c]], 0, (size_t)(l - buckets[c]) * sizeof(SaTy)); l = buckets[c]; } SA[--l] = p;
        }

        std::memset(&SA[0], 0, (size_t)l * sizeof(SaTy));
    }

    static void place_lms_suffixes_histogram_32s_6k(SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy m, const SaTy* RESTRICT buckets)
    {
        const SaTy* RESTRICT bucket_end = &buckets[5 * k];

        fast_sint_t c, j = n;
        for (c = (fast_sint_t)k - 2; c >= 0; --c)
        {
            fast_sint_t l = (fast_sint_t)buckets[buckets_index4(c, 1)];
            if (l > 0)
            {
                fast_sint_t i = bucket_end[c];
                if (j - i > 0)
                {
                    std::memset(&SA[i], 0, (size_t)(j - i) * sizeof(SaTy));
                }

                memmove(&SA[j = (i - l)], &SA[m -= (SaTy)l], (size_t)l * sizeof(SaTy));
            }
        }

        std::memset(&SA[0], 0, (size_t)j * sizeof(SaTy));
    }

    static void place_lms_suffixes_histogram_32s_4k(SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy m, const SaTy* RESTRICT buckets)
    {
        const SaTy* RESTRICT bucket_end = &buckets[3 * k];

        fast_sint_t c, j = n;
        for (c = (fast_sint_t)k - 2; c >= 0; --c)
        {
            fast_sint_t l = (fast_sint_t)buckets[buckets_index2(c, 1)];
            if (l > 0)
            {
                fast_sint_t i = bucket_end[c];
                if (j - i > 0)
                {
                    std::memset(&SA[i], 0, (size_t)(j - i) * sizeof(SaTy));
                }

                memmove(&SA[j = (i - l)], &SA[m -= (SaTy)l], (size_t)l * sizeof(SaTy));
            }
        }

        std::memset(&SA[0], 0, (size_t)j * sizeof(SaTy));
    }

    static void place_lms_suffixes_histogram_32s_2k(SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy m, const SaTy* RESTRICT buckets)
    {
        fast_sint_t j = n;

        if (k > 1)
        {
            fast_sint_t c;
            for (c = buckets_index2((fast_sint_t)k - 2, 0); c >= buckets_index2(0, 0); c -= buckets_index2(1, 0))
            {
                fast_sint_t l = (fast_sint_t)buckets[c + buckets_index2(0, 1)];
                if (l > 0)
                {
                    fast_sint_t i = buckets[c];
                    if (j - i > 0)
                    {
                        std::memset(&SA[i], 0, (size_t)(j - i) * sizeof(SaTy));
                    }

                    memmove(&SA[j = (i - l)], &SA[m -= (SaTy)l], (size_t)l * sizeof(SaTy));
                }
            }
        }

        std::memset(&SA[0], 0, (size_t)j * sizeof(SaTy));
    }

    static void final_bwt_scan_left_to_right_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 & saint_max; if (p0 > 0) { p0--; SA[i + 0] = T[p0] | saint_min; SA[induction_bucket[T[p0]]++] = p0 | ((SaTy)(T[p0 - (p0 > 0)] < T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 & saint_max; if (p1 > 0) { p1--; SA[i + 1] = T[p1] | saint_min; SA[induction_bucket[T[p1]]++] = p1 | ((SaTy)(T[p1 - (p1 > 0)] < T[p1]) << (saint_bit - 1)); }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; SA[i] = T[p] | saint_min; SA[induction_bucket[T[p]]++] = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); }
        }
    }

    static void final_bwt_aux_scan_left_to_right_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 & saint_max; if (p0 > 0) { p0--; SA[i + 0] = T[p0] | saint_min; SA[induction_bucket[T[p0]]++] = p0 | ((SaTy)(T[p0 - (p0 > 0)] < T[p0]) << (saint_bit - 1)); if ((p0 & rm) == 0) { I[p0 / (rm + 1)] = induction_bucket[T[p0]]; } }
            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 & saint_max; if (p1 > 0) { p1--; SA[i + 1] = T[p1] | saint_min; SA[induction_bucket[T[p1]]++] = p1 | ((SaTy)(T[p1 - (p1 > 0)] < T[p1]) << (saint_bit - 1)); if ((p1 & rm) == 0) { I[p1 / (rm + 1)] = induction_bucket[T[p1]]; } }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; SA[i] = T[p] | saint_min; SA[induction_bucket[T[p]]++] = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); if ((p & rm) == 0) { I[p / (rm + 1)] = induction_bucket[T[p]]; } }
        }
    }

    static void final_sorting_scan_left_to_right_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 ^ saint_min; if (p0 > 0) { p0--; SA[induction_bucket[T[p0]]++] = p0 | ((SaTy)(T[p0 - (p0 > 0)] < T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 ^ saint_min; if (p1 > 0) { p1--; SA[induction_bucket[T[p1]]++] = p1 | ((SaTy)(T[p1 - (p1 > 0)] < T[p1]) << (saint_bit - 1)); }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p ^ saint_min; if (p > 0) { p--; SA[induction_bucket[T[p]]++] = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); }
        }
    }

    static void final_sorting_scan_left_to_right_32s(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 2 * prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 3 * prefetch_distance]);

            SaTy s0 = SA[i + 2 * prefetch_distance + 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + 2 * prefetch_distance + 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr);
            SaTy s2 = SA[i + 1 * prefetch_distance + 0]; if (s2 > 0) { prefetchw(&induction_bucket[T[s2 - 1]]); prefetchr(&T[s2] - 2); }
            SaTy s3 = SA[i + 1 * prefetch_distance + 1]; if (s3 > 0) { prefetchw(&induction_bucket[T[s3 - 1]]); prefetchr(&T[s3] - 2); }

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 ^ saint_min; if (p0 > 0) { p0--; SA[induction_bucket[T[p0]]++] = p0 | ((SaTy)(T[p0 - (p0 > 0)] < T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 ^ saint_min; if (p1 > 0) { p1--; SA[induction_bucket[T[p1]]++] = p1 | ((SaTy)(T[p1 - (p1 > 0)] < T[p1]) << (saint_bit - 1)); }
        }

        for (j += 2 * prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p ^ saint_min; if (p > 0) { p--; SA[induction_bucket[T[p]]++] = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); }
        }
    }

    static fast_sint_t final_bwt_scan_left_to_right_16u_block_prepare(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, alphabet_size * sizeof(SaTy));

        fast_sint_t i, j, count = 0;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 & saint_max; if (p0 > 0) { p0--; SA[i + 0] = T[p0] | saint_min; buckets[cache[count].symbol = T[p0]]++; cache[count++].index = p0 | ((SaTy)(T[p0 - (p0 > 0)] < T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 & saint_max; if (p1 > 0) { p1--; SA[i + 1] = T[p1] | saint_min; buckets[cache[count].symbol = T[p1]]++; cache[count++].index = p1 | ((SaTy)(T[p1 - (p1 > 0)] < T[p1]) << (saint_bit - 1)); }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; SA[i] = T[p] | saint_min; buckets[cache[count].symbol = T[p]]++; cache[count++].index = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); }
        }

        return count;
    }

    static fast_sint_t final_sorting_scan_left_to_right_16u_block_prepare(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, alphabet_size * sizeof(SaTy));

        fast_sint_t i, j, count = 0;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i + 0]; SA[i + 0] = p0 ^ saint_min; if (p0 > 0) { p0--; buckets[cache[count].symbol = T[p0]]++; cache[count++].index = p0 | ((SaTy)(T[p0 - (p0 > 0)] < T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i + 1]; SA[i + 1] = p1 ^ saint_min; if (p1 > 0) { p1--; buckets[cache[count].symbol = T[p1]]++; cache[count++].index = p1 | ((SaTy)(T[p1 - (p1 > 0)] < T[p1]) << (saint_bit - 1)); }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy p = SA[i]; SA[i] = p ^ saint_min; if (p > 0) { p--; buckets[cache[count].symbol = T[p]]++; cache[count++].index = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); }
        }

        return count;
    }

    static void final_order_scan_left_to_right_16u_block_place(SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t count)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = 0, j = count - 3; i < j; i += 4)
        {
            prefetchr(&cache[i + prefetch_distance]);

            SA[buckets[cache[i + 0].symbol]++] = cache[i + 0].index;
            SA[buckets[cache[i + 1].symbol]++] = cache[i + 1].index;
            SA[buckets[cache[i + 2].symbol]++] = cache[i + 2].index;
            SA[buckets[cache[i + 3].symbol]++] = cache[i + 3].index;
        }

        for (j += 3; i < j; i += 1)
        {
            SA[buckets[cache[i].symbol]++] = cache[i].index;
        }
    }

    static void final_bwt_aux_scan_left_to_right_16u_block_place(SaTy* RESTRICT SA, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t count)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = 0, j = count - 3; i < j; i += 4)
        {
            prefetchr(&cache[i + prefetch_distance]);

            SA[buckets[cache[i + 0].symbol]++] = cache[i + 0].index; if ((cache[i + 0].index & rm) == 0) { I[(cache[i + 0].index & saint_max) / (rm + 1)] = buckets[cache[i + 0].symbol]; }
            SA[buckets[cache[i + 1].symbol]++] = cache[i + 1].index; if ((cache[i + 1].index & rm) == 0) { I[(cache[i + 1].index & saint_max) / (rm + 1)] = buckets[cache[i + 1].symbol]; }
            SA[buckets[cache[i + 2].symbol]++] = cache[i + 2].index; if ((cache[i + 2].index & rm) == 0) { I[(cache[i + 2].index & saint_max) / (rm + 1)] = buckets[cache[i + 2].symbol]; }
            SA[buckets[cache[i + 3].symbol]++] = cache[i + 3].index; if ((cache[i + 3].index & rm) == 0) { I[(cache[i + 3].index & saint_max) / (rm + 1)] = buckets[cache[i + 3].symbol]; }
        }

        for (j += 3; i < j; i += 1)
        {
            SA[buckets[cache[i].symbol]++] = cache[i].index; if ((cache[i].index & rm) == 0) { I[(cache[i].index & saint_max) / (rm + 1)] = buckets[cache[i].symbol]; }
        }
    }

    static void final_sorting_scan_left_to_right_32s_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy symbol0 = saint_min, p0 = SA[i + 0]; SA[i + 0] = p0 ^ saint_min; if (p0 > 0) { p0--; cache[i + 0].index = p0 | ((SaTy)(T[p0 - (p0 > 0)] < T[p0]) << (saint_bit - 1)); symbol0 = T[p0]; } cache[i + 0].symbol = symbol0;
            SaTy symbol1 = saint_min, p1 = SA[i + 1]; SA[i + 1] = p1 ^ saint_min; if (p1 > 0) { p1--; cache[i + 1].index = p1 | ((SaTy)(T[p1 - (p1 > 0)] < T[p1]) << (saint_bit - 1)); symbol1 = T[p1]; } cache[i + 1].symbol = symbol1;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy symbol = saint_min, p = SA[i]; SA[i] = p ^ saint_min; if (p > 0) { p--; cache[i].index = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); symbol = T[p]; } cache[i].symbol = symbol;
        }
    }

    static void final_sorting_scan_left_to_right_32s_block_sort(const SaTy* RESTRICT T, SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j, omp_block_end = omp_block_start + omp_block_size;
        for (i = omp_block_start, j = omp_block_end - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&cache[i + 2 * prefetch_distance]);

            SaTy s0 = cache[i + prefetch_distance + 0].symbol; const SaTy* Is0 = &induction_bucket[s0]; prefetchw(s0 >= 0 ? Is0 : nullptr);
            SaTy s1 = cache[i + prefetch_distance + 1].symbol; const SaTy* Is1 = &induction_bucket[s1]; prefetchw(s1 >= 0 ? Is1 : nullptr);

            SaTy v0 = cache[i + 0].symbol;
            if (v0 >= 0)
            {
                cache[i + 0].symbol = induction_bucket[v0]++;
                if (cache[i + 0].symbol < omp_block_end) { SaTy ni = cache[i + 0].symbol, np = cache[i + 0].index; cache[i + 0].index = np ^ saint_min; if (np > 0) { np--; cache[ni].index = np | ((SaTy)(T[np - (np > 0)] < T[np]) << (saint_bit - 1)); cache[ni].symbol = T[np]; } }
            }

            SaTy v1 = cache[i + 1].symbol;
            if (v1 >= 0)
            {
                cache[i + 1].symbol = induction_bucket[v1]++;
                if (cache[i + 1].symbol < omp_block_end) { SaTy ni = cache[i + 1].symbol, np = cache[i + 1].index; cache[i + 1].index = np ^ saint_min; if (np > 0) { np--; cache[ni].index = np | ((SaTy)(T[np - (np > 0)] < T[np]) << (saint_bit - 1)); cache[ni].symbol = T[np]; } }
            }
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy v = cache[i].symbol;
            if (v >= 0)
            {
                cache[i].symbol = induction_bucket[v]++;
                if (cache[i].symbol < omp_block_end) { SaTy ni = cache[i].symbol, np = cache[i].index; cache[i].index = np ^ saint_min; if (np > 0) { np--; cache[ni].index = np | ((SaTy)(T[np - (np > 0)] < T[np]) << (saint_bit - 1)); cache[ni].symbol = T[np]; } }
            }
        }
    }

    static void final_bwt_scan_left_to_right_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_bwt_scan_left_to_right_16u(T, SA, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = final_bwt_scan_left_to_right_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size);

                mp::barrier(barrier);

                if (id == 0)
                {
                    fast_sint_t t;
                    for (t = 0; t < num_threads; ++t)
                    {
                        SaTy* RESTRICT temp_bucket = thread_state[t].state.buckets;
                        fast_sint_t c; for (c = 0; c < alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_bucket[c]; induction_bucket[c] = A + B; temp_bucket[c] = A; }
                    }
                }

                mp::barrier(barrier);

                final_order_scan_left_to_right_16u_block_place(SA, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});
    }

    static void final_bwt_aux_scan_left_to_right_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT induction_bucket, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_bwt_aux_scan_left_to_right_16u(T, SA, rm, I, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = final_bwt_scan_left_to_right_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    fast_sint_t t;
                    for (t = 0; t < num_threads; ++t)
                    {
                        SaTy* RESTRICT temp_bucket = thread_state[t].state.buckets;
                        fast_sint_t c; for (c = 0; c < alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_bucket[c]; induction_bucket[c] = A + B; temp_bucket[c] = A; }
                    }
                }
                mp::barrier(barrier);

                final_bwt_aux_scan_left_to_right_16u_block_place(SA, rm, I, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});
    }

    static void final_sorting_scan_left_to_right_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_sorting_scan_left_to_right_16u(T, SA, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = final_sorting_scan_left_to_right_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    fast_sint_t t;
                    for (t = 0; t < num_threads; ++t)
                    {
                        SaTy* RESTRICT temp_bucket = thread_state[t].state.buckets;
                        fast_sint_t c; for (c = 0; c < alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_bucket[c]; induction_bucket[c] = A + B; temp_bucket[c] = A; }
                    }
                }
                mp::barrier(barrier);

                final_order_scan_left_to_right_16u_block_place(SA, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});
    }

    static void final_sorting_scan_left_to_right_32s_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_sorting_scan_left_to_right_32s(T, SA, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                final_sorting_scan_left_to_right_32s_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    final_sorting_scan_left_to_right_32s_block_sort(T, buckets, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                compact_and_place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});
    }

    static void final_bwt_scan_left_to_right_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, fast_sint_t n, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SA[induction_bucket[T[(SaTy)n - 1]]++] = ((SaTy)n - 1) | ((SaTy)(T[(SaTy)n - 2] < T[(SaTy)n - 1]) << (saint_bit - 1));

        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            final_bwt_scan_left_to_right_16u(T, SA, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start;
            for (block_start = 0; block_start < n; )
            {
                if (SA[block_start] == 0)
                {
                    block_start++;
                }
                else
                {
                    fast_sint_t block_max_end = block_start + ((fast_sint_t)mp::getPoolSize(pool)) * (per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)); if (block_max_end > n) { block_max_end = n; }
                    fast_sint_t block_end = block_start + 1; while (block_end < block_max_end && SA[block_end] != 0) { block_end++; }
                    fast_sint_t block_size = block_end - block_start;

                    if (block_size < 32)
                    {
                        for (; block_start < block_end; block_start += 1)
                        {
                            SaTy p = SA[block_start]; SA[block_start] = p & saint_max; if (p > 0) { p--; SA[block_start] = T[p] | saint_min; SA[induction_bucket[T[p]]++] = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); }
                        }
                    }
                    else
                    {
                        final_bwt_scan_left_to_right_16u_block_omp(T, SA, induction_bucket, block_start, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }
    }

    static void final_bwt_aux_scan_left_to_right_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, fast_sint_t n, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SA[induction_bucket[T[(SaTy)n - 1]]++] = ((SaTy)n - 1) | ((SaTy)(T[(SaTy)n - 2] < T[(SaTy)n - 1]) << (saint_bit - 1));

        if ((((SaTy)n - 1) & rm) == 0) { I[((SaTy)n - 1) / (rm + 1)] = induction_bucket[T[(SaTy)n - 1]]; }

        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            final_bwt_aux_scan_left_to_right_16u(T, SA, rm, I, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start;
            for (block_start = 0; block_start < n; )
            {
                if (SA[block_start] == 0)
                {
                    block_start++;
                }
                else
                {
                    fast_sint_t block_max_end = block_start + ((fast_sint_t)mp::getPoolSize(pool)) * (per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)); if (block_max_end > n) { block_max_end = n; }
                    fast_sint_t block_end = block_start + 1; while (block_end < block_max_end && SA[block_end] != 0) { block_end++; }
                    fast_sint_t block_size = block_end - block_start;

                    if (block_size < 32)
                    {
                        for (; block_start < block_end; block_start += 1)
                        {
                            SaTy p = SA[block_start]; SA[block_start] = p & saint_max; if (p > 0) { p--; SA[block_start] = T[p] | saint_min; SA[induction_bucket[T[p]]++] = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); if ((p & rm) == 0) { I[p / (rm + 1)] = induction_bucket[T[p]]; } }
                        }
                    }
                    else
                    {
                        final_bwt_aux_scan_left_to_right_16u_block_omp(T, SA, rm, I, induction_bucket, block_start, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }
    }

    static void final_sorting_scan_left_to_right_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, fast_sint_t n, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SA[induction_bucket[T[(SaTy)n - 1]]++] = ((SaTy)n - 1) | ((SaTy)(T[(SaTy)n - 2] < T[(SaTy)n - 1]) << (saint_bit - 1));

        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            final_sorting_scan_left_to_right_16u(T, SA, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start;
            for (block_start = 0; block_start < n; )
            {
                if (SA[block_start] == 0)
                {
                    block_start++;
                }
                else
                {
                    fast_sint_t block_max_end = block_start + ((fast_sint_t)mp::getPoolSize(pool)) * (per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)); if (block_max_end > n) { block_max_end = n; }
                    fast_sint_t block_end = block_start + 1; while (block_end < block_max_end && SA[block_end] != 0) { block_end++; }
                    fast_sint_t block_size = block_end - block_start;

                    if (block_size < 32)
                    {
                        for (; block_start < block_end; block_start += 1)
                        {
                            SaTy p = SA[block_start]; SA[block_start] = p ^ saint_min; if (p > 0) { p--; SA[induction_bucket[T[p]]++] = p | ((SaTy)(T[p - (p > 0)] < T[p]) << (saint_bit - 1)); }
                        }
                    }
                    else
                    {
                        final_sorting_scan_left_to_right_16u_block_omp(T, SA, induction_bucket, block_start, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }
    }

    static void final_sorting_scan_left_to_right_32s_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SA[induction_bucket[T[n - 1]]++] = (n - 1) | ((SaTy)(T[n - 2] < T[n - 1]) << (saint_bit - 1));

        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            final_sorting_scan_left_to_right_32s(T, SA, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = 0; block_start < n; block_start = block_end)
            {
                block_end = block_start + (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end > n) { block_end = n; }

                final_sorting_scan_left_to_right_32s_block_omp(T, SA, induction_bucket, thread_state[0].state.cache, block_start, block_end - block_start, pool);
            }
        }
    }

    static SaTy final_bwt_scan_right_to_left_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j; SaTy index = -1;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 2 * prefetch_distance]);

            SaTy s0 = SA[i - prefetch_distance - 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - prefetch_distance - 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i - 0]; index = (p0 == 0) ? (SaTy)(i - 0) : index;
            SA[i - 0] = p0 & saint_max; if (p0 > 0) { p0--; AlphabetTy c0 = T[p0 - (p0 > 0)], c1 = T[p0]; SA[i - 0] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p0 : t; }

            SaTy p1 = SA[i - 1]; index = (p1 == 0) ? (SaTy)(i - 1) : index;
            SA[i - 1] = p1 & saint_max; if (p1 > 0) { p1--; AlphabetTy c0 = T[p1 - (p1 > 0)], c1 = T[p1]; SA[i - 1] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p1 : t; }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; index = (p == 0) ? (SaTy)i : index;
            SA[i] = p & saint_max; if (p > 0) { p--; AlphabetTy c0 = T[p - (p > 0)], c1 = T[p]; SA[i] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p : t; }
        }

        return index;
    }

    static void final_bwt_aux_scan_right_to_left_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 2 * prefetch_distance]);

            SaTy s0 = SA[i - prefetch_distance - 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - prefetch_distance - 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i - 0];
            SA[i - 0] = p0 & saint_max; if (p0 > 0) { p0--; AlphabetTy c0 = T[p0 - (p0 > 0)], c1 = T[p0]; SA[i - 0] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p0 : t; if ((p0 & rm) == 0) { I[p0 / (rm + 1)] = induction_bucket[T[p0]] + 1; } }

            SaTy p1 = SA[i - 1];
            SA[i - 1] = p1 & saint_max; if (p1 > 0) { p1--; AlphabetTy c0 = T[p1 - (p1 > 0)], c1 = T[p1]; SA[i - 1] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p1 : t; if ((p1 & rm) == 0) { I[p1 / (rm + 1)] = induction_bucket[T[p1]] + 1; } }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i];
            SA[i] = p & saint_max; if (p > 0) { p--; AlphabetTy c0 = T[p - (p > 0)], c1 = T[p]; SA[i] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p : t; if ((p & rm) == 0) { I[p / (rm + 1)] = induction_bucket[T[p]] + 1; } }
        }
    }

    static void final_sorting_scan_right_to_left_16u(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 2 * prefetch_distance]);

            SaTy s0 = SA[i - prefetch_distance - 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - prefetch_distance - 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i - 0]; SA[i - 0] = p0 & saint_max; if (p0 > 0) { p0--; SA[--induction_bucket[T[p0]]] = p0 | ((SaTy)(T[p0 - (p0 > 0)] > T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i - 1]; SA[i - 1] = p1 & saint_max; if (p1 > 0) { p1--; SA[--induction_bucket[T[p1]]] = p1 | ((SaTy)(T[p1 - (p1 > 0)] > T[p1]) << (saint_bit - 1)); }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; SA[--induction_bucket[T[p]]] = p | ((SaTy)(T[p - (p > 0)] > T[p]) << (saint_bit - 1)); }
        }
    }

    static void final_sorting_scan_right_to_left_32s(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + 2 * prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 3 * prefetch_distance]);

            SaTy s0 = SA[i - 2 * prefetch_distance - 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - 2 * prefetch_distance - 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr);
            SaTy s2 = SA[i - 1 * prefetch_distance - 0]; if (s2 > 0) { prefetchw(&induction_bucket[T[s2 - 1]]); prefetchr(&T[s2] - 2); }
            SaTy s3 = SA[i - 1 * prefetch_distance - 1]; if (s3 > 0) { prefetchw(&induction_bucket[T[s3 - 1]]); prefetchr(&T[s3] - 2); }

            SaTy p0 = SA[i - 0]; SA[i - 0] = p0 & saint_max; if (p0 > 0) { p0--; SA[--induction_bucket[T[p0]]] = p0 | ((SaTy)(T[p0 - (p0 > 0)] > T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i - 1]; SA[i - 1] = p1 & saint_max; if (p1 > 0) { p1--; SA[--induction_bucket[T[p1]]] = p1 | ((SaTy)(T[p1 - (p1 > 0)] > T[p1]) << (saint_bit - 1)); }
        }

        for (j -= 2 * prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; SA[--induction_bucket[T[p]]] = p | ((SaTy)(T[p - (p > 0)] > T[p]) << (saint_bit - 1)); }
        }
    }

    static fast_sint_t final_bwt_scan_right_to_left_16u_block_prepare(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, alphabet_size * sizeof(SaTy));

        fast_sint_t i, j, count = 0;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 2 * prefetch_distance]);

            SaTy s0 = SA[i - prefetch_distance - 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - prefetch_distance - 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i - 0]; SA[i - 0] = p0 & saint_max; if (p0 > 0) { p0--; AlphabetTy c0 = T[p0 - (p0 > 0)], c1 = T[p0]; SA[i - 0] = c1; SaTy t = c0 | saint_min; buckets[cache[count].symbol = c1]++; cache[count++].index = (c0 <= c1) ? p0 : t; }
            SaTy p1 = SA[i - 1]; SA[i - 1] = p1 & saint_max; if (p1 > 0) { p1--; AlphabetTy c0 = T[p1 - (p1 > 0)], c1 = T[p1]; SA[i - 1] = c1; SaTy t = c0 | saint_min; buckets[cache[count].symbol = c1]++; cache[count++].index = (c0 <= c1) ? p1 : t; }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; AlphabetTy c0 = T[p - (p > 0)], c1 = T[p]; SA[i] = c1; SaTy t = c0 | saint_min; buckets[cache[count].symbol = c1]++; cache[count++].index = (c0 <= c1) ? p : t; }
        }

        return count;
    }

    static fast_sint_t final_bwt_aux_scan_right_to_left_16u_block_prepare(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, alphabet_size * sizeof(SaTy));

        fast_sint_t i, j, count = 0;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 2 * prefetch_distance]);

            SaTy s0 = SA[i - prefetch_distance - 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - prefetch_distance - 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i - 0]; SA[i - 0] = p0 & saint_max; if (p0 > 0) { p0--; AlphabetTy c0 = T[p0 - (p0 > 0)], c1 = T[p0]; SA[i - 0] = c1; SaTy t = c0 | saint_min; buckets[cache[count].symbol = c1]++; cache[count].index = (c0 <= c1) ? p0 : t; cache[count + 1].index = p0; count += 2; }
            SaTy p1 = SA[i - 1]; SA[i - 1] = p1 & saint_max; if (p1 > 0) { p1--; AlphabetTy c0 = T[p1 - (p1 > 0)], c1 = T[p1]; SA[i - 1] = c1; SaTy t = c0 | saint_min; buckets[cache[count].symbol = c1]++; cache[count].index = (c0 <= c1) ? p1 : t; cache[count + 1].index = p1; count += 2; }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; AlphabetTy c0 = T[p - (p > 0)], c1 = T[p]; SA[i] = c1; SaTy t = c0 | saint_min; buckets[cache[count].symbol = c1]++; cache[count].index = (c0 <= c1) ? p : t; cache[count + 1].index = p; count += 2; }
        }

        return count;
    }

    static fast_sint_t final_sorting_scan_right_to_left_16u_block_prepare(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        std::memset(buckets, 0, alphabet_size * sizeof(SaTy));

        fast_sint_t i, j, count = 0;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&SA[i - 2 * prefetch_distance]);

            SaTy s0 = SA[i - prefetch_distance - 0]; const AlphabetTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i - prefetch_distance - 1]; const AlphabetTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            SaTy p0 = SA[i - 0]; SA[i - 0] = p0 & saint_max; if (p0 > 0) { p0--; buckets[cache[count].symbol = T[p0]]++; cache[count++].index = p0 | ((SaTy)(T[p0 - (p0 > 0)] > T[p0]) << (saint_bit - 1)); }
            SaTy p1 = SA[i - 1]; SA[i - 1] = p1 & saint_max; if (p1 > 0) { p1--; buckets[cache[count].symbol = T[p1]]++; cache[count++].index = p1 | ((SaTy)(T[p1 - (p1 > 0)] > T[p1]) << (saint_bit - 1)); }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; buckets[cache[count].symbol = T[p]]++; cache[count++].index = p | ((SaTy)(T[p - (p > 0)] > T[p]) << (saint_bit - 1)); }
        }

        return count;
    }

    static void final_order_scan_right_to_left_16u_block_place(SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t count)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = 0, j = count - 3; i < j; i += 4)
        {
            prefetchr(&cache[i + prefetch_distance]);

            SA[--buckets[cache[i + 0].symbol]] = cache[i + 0].index;
            SA[--buckets[cache[i + 1].symbol]] = cache[i + 1].index;
            SA[--buckets[cache[i + 2].symbol]] = cache[i + 2].index;
            SA[--buckets[cache[i + 3].symbol]] = cache[i + 3].index;
        }

        for (j += 3; i < j; i += 1)
        {
            SA[--buckets[cache[i].symbol]] = cache[i].index;
        }
    }

    static void final_bwt_aux_scan_right_to_left_16u_block_place(SaTy* RESTRICT SA, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t count)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = 0, j = count - 6; i < j; i += 8)
        {
            prefetchr(&cache[i + prefetch_distance]);

            SA[--buckets[cache[i + 0].symbol]] = cache[i + 0].index; if ((cache[i + 1].index & rm) == 0) { I[cache[i + 1].index / (rm + 1)] = buckets[cache[i + 0].symbol] + 1; }
            SA[--buckets[cache[i + 2].symbol]] = cache[i + 2].index; if ((cache[i + 3].index & rm) == 0) { I[cache[i + 3].index / (rm + 1)] = buckets[cache[i + 2].symbol] + 1; }
            SA[--buckets[cache[i + 4].symbol]] = cache[i + 4].index; if ((cache[i + 5].index & rm) == 0) { I[cache[i + 5].index / (rm + 1)] = buckets[cache[i + 4].symbol] + 1; }
            SA[--buckets[cache[i + 6].symbol]] = cache[i + 6].index; if ((cache[i + 7].index & rm) == 0) { I[cache[i + 7].index / (rm + 1)] = buckets[cache[i + 6].symbol] + 1; }
        }

        for (j += 6; i < j; i += 2)
        {
            SA[--buckets[cache[i].symbol]] = cache[i].index; if ((cache[i + 1].index & rm) == 0) { I[(cache[i + 1].index & saint_max) / (rm + 1)] = buckets[cache[i].symbol] + 1; }
        }
    }

    static void final_sorting_scan_right_to_left_32s_block_gather(const SaTy* RESTRICT T, SaTy* RESTRICT SA, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 1; i < j; i += 2)
        {
            prefetchw(&SA[i + 2 * prefetch_distance]);

            SaTy s0 = SA[i + prefetch_distance + 0]; const SaTy* Ts0 = &T[s0] - 1; prefetchr(s0 > 0 ? Ts0 : nullptr); Ts0--; prefetchr(s0 > 0 ? Ts0 : nullptr);
            SaTy s1 = SA[i + prefetch_distance + 1]; const SaTy* Ts1 = &T[s1] - 1; prefetchr(s1 > 0 ? Ts1 : nullptr); Ts1--; prefetchr(s1 > 0 ? Ts1 : nullptr);

            prefetchw(&cache[i + prefetch_distance]);

            SaTy symbol0 = saint_min, p0 = SA[i + 0]; SA[i + 0] = p0 & saint_max; if (p0 > 0) { p0--; cache[i + 0].index = p0 | ((SaTy)(T[p0 - (p0 > 0)] > T[p0]) << (saint_bit - 1)); symbol0 = T[p0]; } cache[i + 0].symbol = symbol0;
            SaTy symbol1 = saint_min, p1 = SA[i + 1]; SA[i + 1] = p1 & saint_max; if (p1 > 0) { p1--; cache[i + 1].index = p1 | ((SaTy)(T[p1 - (p1 > 0)] > T[p1]) << (saint_bit - 1)); symbol1 = T[p1]; } cache[i + 1].symbol = symbol1;
        }

        for (j += prefetch_distance + 1; i < j; i += 1)
        {
            SaTy symbol = saint_min, p = SA[i]; SA[i] = p & saint_max; if (p > 0) { p--; cache[i].index = p | ((SaTy)(T[p - (p > 0)] > T[p]) << (saint_bit - 1)); symbol = T[p]; } cache[i].symbol = symbol;
        }
    }

    static void final_sorting_scan_right_to_left_32s_block_sort(const SaTy* RESTRICT T, SaTy* RESTRICT induction_bucket, ThreadCache* RESTRICT cache, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = omp_block_start + omp_block_size - 1, j = omp_block_start + prefetch_distance + 1; i >= j; i -= 2)
        {
            prefetchw(&cache[i - 2 * prefetch_distance]);

            SaTy s0 = cache[i - prefetch_distance - 0].symbol; const SaTy* Is0 = &induction_bucket[s0]; prefetchw(s0 >= 0 ? Is0 : nullptr);
            SaTy s1 = cache[i - prefetch_distance - 1].symbol; const SaTy* Is1 = &induction_bucket[s1]; prefetchw(s1 >= 0 ? Is1 : nullptr);

            SaTy v0 = cache[i - 0].symbol;
            if (v0 >= 0)
            {
                cache[i - 0].symbol = --induction_bucket[v0];
                if (cache[i - 0].symbol >= omp_block_start) { SaTy ni = cache[i - 0].symbol, np = cache[i - 0].index; cache[i - 0].index = np & saint_max; if (np > 0) { np--; cache[ni].index = np | ((SaTy)(T[np - (np > 0)] > T[np]) << (saint_bit - 1)); cache[ni].symbol = T[np]; } }
            }

            SaTy v1 = cache[i - 1].symbol;
            if (v1 >= 0)
            {
                cache[i - 1].symbol = --induction_bucket[v1];
                if (cache[i - 1].symbol >= omp_block_start) { SaTy ni = cache[i - 1].symbol, np = cache[i - 1].index; cache[i - 1].index = np & saint_max; if (np > 0) { np--; cache[ni].index = np | ((SaTy)(T[np - (np > 0)] > T[np]) << (saint_bit - 1)); cache[ni].symbol = T[np]; } }
            }
        }

        for (j -= prefetch_distance + 1; i >= j; i -= 1)
        {
            SaTy v = cache[i].symbol;
            if (v >= 0)
            {
                cache[i].symbol = --induction_bucket[v];
                if (cache[i].symbol >= omp_block_start) { SaTy ni = cache[i].symbol, np = cache[i].index; cache[i].index = np & saint_max; if (np > 0) { np--; cache[ni].index = np | ((SaTy)(T[np - (np > 0)] > T[np]) << (saint_bit - 1)); cache[ni].symbol = T[np]; } }
            }
        }
    }

    static void final_bwt_scan_right_to_left_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_bwt_scan_right_to_left_16u(T, SA, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = final_bwt_scan_right_to_left_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    fast_sint_t t;
                    for (t = num_threads - 1; t >= 0; --t)
                    {
                        SaTy* RESTRICT temp_bucket = thread_state[t].state.buckets;
                        fast_sint_t c; for (c = 0; c < alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_bucket[c]; induction_bucket[c] = A - B; temp_bucket[c] = A; }
                    }
                }
                mp::barrier(barrier);

                final_order_scan_right_to_left_16u_block_place(SA, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});
    }

    static void final_bwt_aux_scan_right_to_left_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT induction_bucket, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_bwt_aux_scan_right_to_left_16u(T, SA, rm, I, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = final_bwt_aux_scan_right_to_left_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    fast_sint_t t;
                    for (t = num_threads - 1; t >= 0; --t)
                    {
                        SaTy* RESTRICT temp_bucket = thread_state[t].state.buckets;
                        fast_sint_t c; for (c = 0; c < alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_bucket[c]; induction_bucket[c] = A - B; temp_bucket[c] = A; }
                    }
                }
                mp::barrier(barrier);

                final_bwt_aux_scan_right_to_left_16u_block_place(SA, rm, I, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});
    }

    static void final_sorting_scan_right_to_left_16u_block_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT induction_bucket, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_sorting_scan_right_to_left_16u(T, SA, induction_bucket, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = final_sorting_scan_right_to_left_16u_block_prepare(T, SA, thread_state[id].state.buckets, thread_state[id].state.cache, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    fast_sint_t t;
                    for (t = num_threads - 1; t >= 0; --t)
                    {
                        SaTy* RESTRICT temp_bucket = thread_state[t].state.buckets;
                        fast_sint_t c; for (c = 0; c < alphabet_size; c += 1) { SaTy A = induction_bucket[c], B = temp_bucket[c]; induction_bucket[c] = A - B; temp_bucket[c] = A; }
                    }
                }
                mp::barrier(barrier);

                final_order_scan_right_to_left_16u_block_place(SA, thread_state[id].state.buckets, thread_state[id].state.cache, thread_state[id].state.count);
            }
        }, mp::ParallelCond{block_size >= 64 * alphabet_size});
    }

    static void final_sorting_scan_right_to_left_32s_block_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy* RESTRICT buckets, ThreadCache* RESTRICT cache, fast_sint_t block_start, fast_sint_t block_size, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (block_size / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : block_size - omp_block_start;

            omp_block_start += block_start;

            if (num_threads == 1)
            {
                final_sorting_scan_right_to_left_32s(T, SA, buckets, omp_block_start, omp_block_size);
            }
            else
            {
                final_sorting_scan_right_to_left_32s_block_gather(T, SA, cache - block_start, omp_block_start, omp_block_size);

                mp::barrier(barrier);
                if (id == 0)
                {
                    final_sorting_scan_right_to_left_32s_block_sort(T, buckets, cache - block_start, block_start, block_size);
                }
                mp::barrier(barrier);

                compact_and_place_cached_suffixes(SA, cache - block_start, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{block_size >= 16384});
    }

    static SaTy final_bwt_scan_right_to_left_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy index = -1;

        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            index = final_bwt_scan_right_to_left_16u(T, SA, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start;
            for (block_start = (fast_sint_t)n - 1; block_start >= 0; )
            {
                if (SA[block_start] == 0)
                {
                    index = (SaTy)block_start--;
                }
                else
                {
                    fast_sint_t block_max_end = block_start - ((fast_sint_t)mp::getPoolSize(pool)) * (per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)); if (block_max_end < 0) { block_max_end = -1; }
                    fast_sint_t block_end = block_start - 1; while (block_end > block_max_end && SA[block_end] != 0) { block_end--; }
                    fast_sint_t block_size = block_start - block_end;

                    if (block_size < 32)
                    {
                        for (; block_start > block_end; block_start -= 1)
                        {
                            SaTy p = SA[block_start]; SA[block_start] = p & saint_max; if (p > 0) { p--; AlphabetTy c0 = T[p - (p > 0)], c1 = T[p]; SA[block_start] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p : t; }
                        }
                    }
                    else
                    {
                        final_bwt_scan_right_to_left_16u_block_omp(T, SA, induction_bucket, block_end + 1, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }

        return index;
    }

    static void final_bwt_aux_scan_right_to_left_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy rm, SaTy* RESTRICT I, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            final_bwt_aux_scan_right_to_left_16u(T, SA, rm, I, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start;
            for (block_start = (fast_sint_t)n - 1; block_start >= 0; )
            {
                if (SA[block_start] == 0)
                {
                    block_start--;
                }
                else
                {
                    fast_sint_t block_max_end = block_start - ((fast_sint_t)mp::getPoolSize(pool)) * ((per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)) / 2); if (block_max_end < 0) { block_max_end = -1; }
                    fast_sint_t block_end = block_start - 1; while (block_end > block_max_end && SA[block_end] != 0) { block_end--; }
                    fast_sint_t block_size = block_start - block_end;

                    if (block_size < 32)
                    {
                        for (; block_start > block_end; block_start -= 1)
                        {
                            SaTy p = SA[block_start]; SA[block_start] = p & saint_max; if (p > 0) { p--; AlphabetTy c0 = T[p - (p > 0)], c1 = T[p]; SA[block_start] = c1; SaTy t = c0 | saint_min; SA[--induction_bucket[c1]] = (c0 <= c1) ? p : t; if ((p & rm) == 0) { I[p / (rm + 1)] = induction_bucket[T[p]] + 1; } }
                        }
                    }
                    else
                    {
                        final_bwt_aux_scan_right_to_left_16u_block_omp(T, SA, rm, I, induction_bucket, block_end + 1, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }
    }

    static void final_sorting_scan_right_to_left_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            final_sorting_scan_right_to_left_16u(T, SA, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start;
            for (block_start = (fast_sint_t)n - 1; block_start >= 0; )
            {
                if (SA[block_start] == 0)
                {
                    block_start--;
                }
                else
                {
                    fast_sint_t block_max_end = block_start - ((fast_sint_t)mp::getPoolSize(pool)) * (per_thread_cache_size - 16 * (fast_sint_t)mp::getPoolSize(pool)); if (block_max_end < -1) { block_max_end = -1; }
                    fast_sint_t block_end = block_start - 1; while (block_end > block_max_end && SA[block_end] != 0) { block_end--; }
                    fast_sint_t block_size = block_start - block_end;

                    if (block_size < 32)
                    {
                        for (; block_start > block_end; block_start -= 1)
                        {
                            SaTy p = SA[block_start]; SA[block_start] = p & saint_max; if (p > 0) { p--; SA[--induction_bucket[T[p]]] = p | ((SaTy)(T[p - (p > 0)] > T[p]) << (saint_bit - 1)); }
                        }
                    }
                    else
                    {
                        final_sorting_scan_right_to_left_16u_block_omp(T, SA, induction_bucket, block_end + 1, block_size, pool, thread_state);
                        block_start = block_end;
                    }
                }
            }
        }
    }

    static void final_sorting_scan_right_to_left_32s_omp(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy* RESTRICT induction_bucket, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (mp::getPoolSize(pool) == 1 || n < 65536)
        {
            final_sorting_scan_right_to_left_32s(T, SA, induction_bucket, 0, n);
        }
        else
        {
            fast_sint_t block_start, block_end;
            for (block_start = (fast_sint_t)n - 1; block_start >= 0; block_start = block_end)
            {
                block_end = block_start - (fast_sint_t)mp::getPoolSize(pool) * per_thread_cache_size; if (block_end < 0) { block_end = -1; }

                final_sorting_scan_right_to_left_32s_block_omp(T, SA, induction_bucket, thread_state[0].state.cache, block_end + 1, block_start - block_end, pool);
            }
        }
    }

    static void clear_lms_suffixes_omp(SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT bucket_start, SaTy* RESTRICT bucket_end, mp::ThreadPool* pool)
    {
        mp::forParallel(pool, 0, k, 1, [&](size_t id, size_t numThreads, ptrdiff_t start, ptrdiff_t stop, ptrdiff_t step, mp::Barrier* barrier)
        {
            for (auto c = start; c < stop; ++c)
            {
                if (bucket_end[c] > bucket_start[c])
                {
                    std::memset(&SA[bucket_start[c]], 0, ((size_t)bucket_end[c] - (size_t)bucket_start[c]) * sizeof(SaTy));
                }
            }
        }, mp::ParallelCond{n >= 65536});
    }

    static SaTy induce_final_order_16u_omp(const AlphabetTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy bwt, SaTy r, SaTy* RESTRICT I, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (!bwt)
        {
            final_sorting_scan_left_to_right_16u_omp(T, SA, n, &buckets[6 * alphabet_size], pool, thread_state);
            if (mp::getPoolSize(pool) > 1 && n >= 65536) { clear_lms_suffixes_omp(SA, n, alphabet_size, &buckets[6 * alphabet_size], &buckets[7 * alphabet_size], pool); }
            final_sorting_scan_right_to_left_16u_omp(T, SA, n, &buckets[7 * alphabet_size], pool, thread_state);
            return 0;
        }
        else if (I != nullptr)
        {
            final_bwt_aux_scan_left_to_right_16u_omp(T, SA, n, r - 1, I, &buckets[6 * alphabet_size], pool, thread_state);
            if (mp::getPoolSize(pool) > 1 && n >= 65536) { clear_lms_suffixes_omp(SA, n, alphabet_size, &buckets[6 * alphabet_size], &buckets[7 * alphabet_size], pool); }
            final_bwt_aux_scan_right_to_left_16u_omp(T, SA, n, r - 1, I, &buckets[7 * alphabet_size], pool, thread_state);
            return 0;
        }
        else
        {
            final_bwt_scan_left_to_right_16u_omp(T, SA, n, &buckets[6 * alphabet_size], pool, thread_state);
            if (mp::getPoolSize(pool) > 1 && n >= 65536) { clear_lms_suffixes_omp(SA, n, alphabet_size, &buckets[6 * alphabet_size], &buckets[7 * alphabet_size], pool); }
            return final_bwt_scan_right_to_left_16u_omp(T, SA, n, &buckets[7 * alphabet_size], pool, thread_state);
        }
    }

    static void induce_final_order_32s_6k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        final_sorting_scan_left_to_right_32s_omp(T, SA, n, &buckets[4 * k], pool, thread_state);
        final_sorting_scan_right_to_left_32s_omp(T, SA, n, &buckets[5 * k], pool, thread_state);
    }

    static void induce_final_order_32s_4k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        final_sorting_scan_left_to_right_32s_omp(T, SA, n, &buckets[2 * k], pool, thread_state);
        final_sorting_scan_right_to_left_32s_omp(T, SA, n, &buckets[3 * k], pool, thread_state);
    }

    static void induce_final_order_32s_2k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        final_sorting_scan_left_to_right_32s_omp(T, SA, n, &buckets[1 * k], pool, thread_state);
        final_sorting_scan_right_to_left_32s_omp(T, SA, n, &buckets[0 * k], pool, thread_state);
    }

    static void induce_final_order_32s_1k(const SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        count_suffixes_32s(T, n, k, buckets);
        initialize_buckets_start_32s_1k(k, buckets);
        final_sorting_scan_left_to_right_32s_omp(T, SA, n, buckets, pool, thread_state);

        count_suffixes_32s(T, n, k, buckets);
        initialize_buckets_end_32s_1k(k, buckets);
        final_sorting_scan_right_to_left_32s_omp(T, SA, n, buckets, pool, thread_state);
    }

    static SaTy renumber_unique_and_nonunique_lms_suffixes_32s(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy m, SaTy f, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT SAm = &SA[m];

        SaTy i, j;
        for (i = (SaTy)omp_block_start, j = (SaTy)omp_block_start + (SaTy)omp_block_size - 2 * (SaTy)prefetch_distance - 3; i < j; i += 4)
        {
            prefetchr(&SA[i + 3 * prefetch_distance]);

            prefetchw(&SAm[to_unsigned(SA[i + 2 * prefetch_distance + 0]) >> 1]);
            prefetchw(&SAm[to_unsigned(SA[i + 2 * prefetch_distance + 1]) >> 1]);
            prefetchw(&SAm[to_unsigned(SA[i + 2 * prefetch_distance + 2]) >> 1]);
            prefetchw(&SAm[to_unsigned(SA[i + 2 * prefetch_distance + 3]) >> 1]);

            auto q0 = to_unsigned(SA[i + prefetch_distance + 0]); const SaTy* Tq0 = &T[q0]; prefetchw(SAm[q0 >> 1] < 0 ? Tq0 : nullptr);
            auto q1 = to_unsigned(SA[i + prefetch_distance + 1]); const SaTy* Tq1 = &T[q1]; prefetchw(SAm[q1 >> 1] < 0 ? Tq1 : nullptr);
            auto q2 = to_unsigned(SA[i + prefetch_distance + 2]); const SaTy* Tq2 = &T[q2]; prefetchw(SAm[q2 >> 1] < 0 ? Tq2 : nullptr);
            auto q3 = to_unsigned(SA[i + prefetch_distance + 3]); const SaTy* Tq3 = &T[q3]; prefetchw(SAm[q3 >> 1] < 0 ? Tq3 : nullptr);

            auto p0 = to_unsigned(SA[i + 0]); SaTy s0 = SAm[p0 >> 1]; if (s0 < 0) { T[p0] |= saint_min; f++; s0 = i + 0 + saint_min + f; } SAm[p0 >> 1] = s0 - f;
            auto p1 = to_unsigned(SA[i + 1]); SaTy s1 = SAm[p1 >> 1]; if (s1 < 0) { T[p1] |= saint_min; f++; s1 = i + 1 + saint_min + f; } SAm[p1 >> 1] = s1 - f;
            auto p2 = to_unsigned(SA[i + 2]); SaTy s2 = SAm[p2 >> 1]; if (s2 < 0) { T[p2] |= saint_min; f++; s2 = i + 2 + saint_min + f; } SAm[p2 >> 1] = s2 - f;
            auto p3 = to_unsigned(SA[i + 3]); SaTy s3 = SAm[p3 >> 1]; if (s3 < 0) { T[p3] |= saint_min; f++; s3 = i + 3 + saint_min + f; } SAm[p3 >> 1] = s3 - f;
        }

        for (j += 2 * (SaTy)prefetch_distance + 3; i < j; i += 1)
        {
            auto p = to_unsigned(SA[i]); SaTy s = SAm[p >> 1]; if (s < 0) { T[p] |= saint_min; f++; s = i + saint_min + f; } SAm[p >> 1] = s - f;
        }

        return f;
    }

    static void compact_unique_and_nonunique_lms_suffixes_32s(SaTy* RESTRICT SA, SaTy m, fast_sint_t* pl, fast_sint_t* pr, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT SAl = &SA[0];
        SaTy* RESTRICT SAr = &SA[0];

        fast_sint_t i, j, l = *pl - 1, r = *pr - 1;
        for (i = (fast_sint_t)m + omp_block_start + omp_block_size - 1, j = (fast_sint_t)m + omp_block_start + 3; i >= j; i -= 4)
        {
            prefetchr(&SA[i - prefetch_distance]);

            SaTy p0 = SA[i - 0]; SAl[l] = p0 & saint_max; l -= p0 < 0; SAr[r] = p0 - 1; r -= p0 > 0;
            SaTy p1 = SA[i - 1]; SAl[l] = p1 & saint_max; l -= p1 < 0; SAr[r] = p1 - 1; r -= p1 > 0;
            SaTy p2 = SA[i - 2]; SAl[l] = p2 & saint_max; l -= p2 < 0; SAr[r] = p2 - 1; r -= p2 > 0;
            SaTy p3 = SA[i - 3]; SAl[l] = p3 & saint_max; l -= p3 < 0; SAr[r] = p3 - 1; r -= p3 > 0;
        }

        for (j -= 3; i >= j; i -= 1)
        {
            SaTy p = SA[i]; SAl[l] = p & saint_max; l -= p < 0; SAr[r] = p - 1; r -= p > 0;
        }

        *pl = l + 1; *pr = r + 1;
    }


    static SaTy count_unique_suffixes(SaTy* RESTRICT SA, SaTy m, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        SaTy* RESTRICT SAm = &SA[m];

        fast_sint_t i, j; SaTy f0 = 0, f1 = 0, f2 = 0, f3 = 0;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - prefetch_distance - 3; i < j; i += 4)
        {
            prefetchr(&SA[i + 2 * prefetch_distance]);

            prefetchr(&SAm[to_unsigned(SA[i + prefetch_distance + 0]) >> 1]);
            prefetchr(&SAm[to_unsigned(SA[i + prefetch_distance + 1]) >> 1]);
            prefetchr(&SAm[to_unsigned(SA[i + prefetch_distance + 2]) >> 1]);
            prefetchr(&SAm[to_unsigned(SA[i + prefetch_distance + 3]) >> 1]);

            f0 += SAm[to_unsigned(SA[i + 0]) >> 1] < 0;
            f1 += SAm[to_unsigned(SA[i + 1]) >> 1] < 0;
            f2 += SAm[to_unsigned(SA[i + 2]) >> 1] < 0;
            f3 += SAm[to_unsigned(SA[i + 3]) >> 1] < 0;
        }

        for (j += prefetch_distance + 3; i < j; i += 1)
        {
            f0 += SAm[to_unsigned(SA[i]) >> 1] < 0;
        }

        return f0 + f1 + f2 + f3;
    }

    static SaTy renumber_unique_and_nonunique_lms_suffixes_32s_omp(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy m, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy f = 0;
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (m / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : m - omp_block_start;

            if (num_threads == 1)
            {
                f = renumber_unique_and_nonunique_lms_suffixes_32s(T, SA, m, 0, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = count_unique_suffixes(SA, m, omp_block_start, omp_block_size);

                mp::barrier(barrier);

                fast_sint_t t, count = 0; for (t = 0; t < id; ++t) { count += thread_state[t].state.count; }

                if (id == num_threads - 1)
                {
                    f = (SaTy)(count + thread_state[id].state.count);
                }

                renumber_unique_and_nonunique_lms_suffixes_32s(T, SA, m, (SaTy)count, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{m >= 65536});

        return f;
    }

    static void compact_unique_and_nonunique_lms_suffixes_32s_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy fs, SaTy f, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (((fast_sint_t)n >> 1) / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : ((fast_sint_t)n >> 1) - omp_block_start;

            if (num_threads == 1)
            {
                fast_sint_t l = m, r = (fast_sint_t)n + (fast_sint_t)fs;
                compact_unique_and_nonunique_lms_suffixes_32s(SA, m, &l, &r, omp_block_start, omp_block_size);
            }
            else
            {
                {
                    thread_state[id].state.position = (fast_sint_t)m + ((fast_sint_t)n >> 1) + omp_block_start + omp_block_size;
                    thread_state[id].state.count = (fast_sint_t)m + omp_block_start + omp_block_size;

                    compact_unique_and_nonunique_lms_suffixes_32s(SA, m, &thread_state[id].state.position, &thread_state[id].state.count, omp_block_start, omp_block_size);
                }

                mp::barrier(barrier);

                if (id == 0)
                {
                    fast_sint_t t, position;

                    for (position = m, t = num_threads - 1; t >= 0; --t)
                    {
                        fast_sint_t omp_block_end = t < num_threads - 1 ? omp_block_stride * (t + 1) : ((fast_sint_t)n >> 1);
                        fast_sint_t count = ((fast_sint_t)m + ((fast_sint_t)n >> 1) + omp_block_end - thread_state[t].state.position);

                        if (count > 0)
                        {
                            position -= count; std::memcpy(&SA[position], &SA[thread_state[t].state.position], (size_t)count * sizeof(SaTy));
                        }
                    }

                    for (position = (fast_sint_t)n + (fast_sint_t)fs, t = num_threads - 1; t >= 0; --t)
                    {
                        fast_sint_t omp_block_end = t < num_threads - 1 ? omp_block_stride * (t + 1) : ((fast_sint_t)n >> 1);
                        fast_sint_t count = ((fast_sint_t)m + omp_block_end - thread_state[t].state.count);

                        if (count > 0)
                        {
                            position -= count; std::memcpy(&SA[position], &SA[thread_state[t].state.count], (size_t)count * sizeof(SaTy));
                        }
                    }
                }
            }
        }, mp::ParallelCond{n >= 131072 && m < fs});

        std::memcpy(&SA[(fast_sint_t)n + (fast_sint_t)fs - (fast_sint_t)m], &SA[(fast_sint_t)m - (fast_sint_t)f], (size_t)f * sizeof(SaTy));
    }

    static SaTy compact_lms_suffixes_32s_omp(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy fs, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        SaTy f = renumber_unique_and_nonunique_lms_suffixes_32s_omp(T, SA, m, pool, thread_state);
        compact_unique_and_nonunique_lms_suffixes_32s_omp(SA, n, m, fs, f, pool, thread_state);

        return f;
    }

    static void merge_unique_lms_suffixes_32s(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, fast_sint_t l, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        const SaTy* RESTRICT SAnm = &SA[(fast_sint_t)n - (fast_sint_t)m - 1 + l];

        SaTy i, j; fast_sint_t tmp = *SAnm++;
        for (i = (SaTy)omp_block_start, j = (SaTy)omp_block_start + (SaTy)omp_block_size - 6; i < j; i += 4)
        {
            prefetchr(&T[i + prefetch_distance]);

            SaTy c0 = T[i + 0]; if (c0 < 0) { T[i + 0] = c0 & saint_max; SA[tmp] = i + 0; i++; tmp = *SAnm++; }
            SaTy c1 = T[i + 1]; if (c1 < 0) { T[i + 1] = c1 & saint_max; SA[tmp] = i + 1; i++; tmp = *SAnm++; }
            SaTy c2 = T[i + 2]; if (c2 < 0) { T[i + 2] = c2 & saint_max; SA[tmp] = i + 2; i++; tmp = *SAnm++; }
            SaTy c3 = T[i + 3]; if (c3 < 0) { T[i + 3] = c3 & saint_max; SA[tmp] = i + 3; i++; tmp = *SAnm++; }
        }

        for (j += 6; i < j; i += 1)
        {
            SaTy c = T[i]; if (c < 0) { T[i] = c & saint_max; SA[tmp] = i; i++; tmp = *SAnm++; }
        }
    }

    static void merge_nonunique_lms_suffixes_32s(SaTy* RESTRICT SA, SaTy n, SaTy m, fast_sint_t l, fast_sint_t omp_block_start, fast_sint_t omp_block_size)
    {
        const fast_sint_t prefetch_distance = 32;

        const SaTy* RESTRICT SAnm = &SA[(fast_sint_t)n - (fast_sint_t)m - 1 + l];

        fast_sint_t i, j; SaTy tmp = *SAnm++;
        for (i = omp_block_start, j = omp_block_start + omp_block_size - 3; i < j; i += 4)
        {
            prefetchr(&SA[i + prefetch_distance]);

            if (SA[i + 0] == 0) { SA[i + 0] = tmp; tmp = *SAnm++; }
            if (SA[i + 1] == 0) { SA[i + 1] = tmp; tmp = *SAnm++; }
            if (SA[i + 2] == 0) { SA[i + 2] = tmp; tmp = *SAnm++; }
            if (SA[i + 3] == 0) { SA[i + 3] = tmp; tmp = *SAnm++; }
        }

        for (j += 3; i < j; i += 1)
        {
            if (SA[i] == 0) { SA[i] = tmp; tmp = *SAnm++; }
        }
    }

    static void merge_unique_lms_suffixes_32s_omp(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

            if (num_threads == 1)
            {
                merge_unique_lms_suffixes_32s(T, SA, n, m, 0, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = count_negative_marked_suffixes(T, omp_block_start, omp_block_size);

                mp::barrier(barrier);

                fast_sint_t t, count = 0; for (t = 0; t < id; ++t) { count += thread_state[t].state.count; }

                merge_unique_lms_suffixes_32s(T, SA, n, m, count, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{n >= 65536});
    }

    static void merge_nonunique_lms_suffixes_32s_omp(SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy f, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = (m / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : m - omp_block_start;

            if (num_threads == 1)
            {
                merge_nonunique_lms_suffixes_32s(SA, n, m, f, omp_block_start, omp_block_size);
            }
            else
            {
                thread_state[id].state.count = count_zero_marked_suffixes(SA, omp_block_start, omp_block_size);

                mp::barrier(barrier);

                fast_sint_t t, count = f; for (t = 0; t < id; ++t) { count += thread_state[t].state.count; }

                merge_nonunique_lms_suffixes_32s(SA, n, m, count, omp_block_start, omp_block_size);
            }
        }, mp::ParallelCond{m >= 65536});
    }

    static void merge_compacted_lms_suffixes_32s_omp(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy f, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        merge_unique_lms_suffixes_32s_omp(T, SA, n, m, pool, thread_state);
        merge_nonunique_lms_suffixes_32s_omp(SA, n, m, f, pool, thread_state);
    }

    static void reconstruct_compacted_lms_suffixes_32s_2k_omp(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy m, SaTy fs, SaTy f, SaTy* RESTRICT buckets, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (f > 0)
        {
            memmove(&SA[n - m - 1], &SA[n + fs - m], (size_t)f * sizeof(SaTy));
        
            count_and_gather_compacted_lms_suffixes_32s_2k_omp(T, SA, n, k, buckets, pool, thread_state);
            reconstruct_lms_suffixes_omp(SA, n, m - f, pool);
        
            std::memcpy(&SA[n - m - 1 + f], &SA[0], ((size_t)m - (size_t)f) * sizeof(SaTy));
            std::memset(&SA[0], 0, (size_t)m * sizeof(SaTy));

            merge_compacted_lms_suffixes_32s_omp(T, SA, n, m, f, pool, thread_state);
        }
        else
        {
            count_and_gather_lms_suffixes_32s_2k(T, SA, n, k, buckets, 0, n);
            reconstruct_lms_suffixes_omp(SA, n, m, pool);
        }
    }

    static void reconstruct_compacted_lms_suffixes_32s_1k_omp(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy m, SaTy fs, SaTy f, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        if (f > 0)
        {
            memmove(&SA[n - m - 1], &SA[n + fs - m], (size_t)f * sizeof(SaTy));

            gather_compacted_lms_suffixes_32s(T, SA, n);
            reconstruct_lms_suffixes_omp(SA, n, m - f, pool);

            std::memcpy(&SA[n - m - 1 + f], &SA[0], ((size_t)m - (size_t)f) * sizeof(SaTy));
            std::memset(&SA[0], 0, (size_t)m * sizeof(SaTy));

            merge_compacted_lms_suffixes_32s_omp(T, SA, n, m, f, pool, thread_state);
        }
        else
        {
            gather_lms_suffixes_32s(T, SA, n);
            reconstruct_lms_suffixes_omp(SA, n, m, pool);
        }
    }

    static SaTy main_32s(SaTy* RESTRICT T, SaTy* RESTRICT SA, SaTy n, SaTy k, SaTy fs, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        fs = fs < (saint_max - n) ? fs : (saint_max - n);

        if (k > 0 && fs / k >= 6)
        {
            ptrdiff_t alignment = (fs - 1024) / k >= 6 ? 1024 : 16;
            SaTy* RESTRICT buckets = (fs - alignment) / k >= 6 ? (SaTy*)align_up(&SA[n + fs - 6 * k - alignment], (size_t)alignment * sizeof(SaTy)) : &SA[n + fs - 6 * k];

            SaTy m = count_and_gather_lms_suffixes_32s_4k_omp(T, SA, n, k, buckets, pool, thread_state);
            if (m > 1)
            {
                std::memset(SA, 0, ((size_t)n - (size_t)m) * sizeof(SaTy));

                SaTy first_lms_suffix = SA[n - m];
                SaTy left_suffixes_count = initialize_buckets_for_lms_suffixes_radix_sort_32s_6k(T, k, buckets, first_lms_suffix);

                radix_sort_lms_suffixes_32s_6k_omp(T, SA, n, m, &buckets[4 * k], pool, thread_state);
                radix_sort_set_markers_32s_6k_omp(SA, k, &buckets[4 * k], pool);
            
                if (mp::getPoolSize(pool) > 1 && n >= 65536) { std::memset(&SA[(fast_sint_t)n - (fast_sint_t)m], 0, (size_t)m * sizeof(SaTy)); }
            
                initialize_buckets_for_partial_sorting_32s_6k(T, k, buckets, first_lms_suffix, left_suffixes_count);
                induce_partial_order_32s_6k_omp(T, SA, n, k, buckets, first_lms_suffix, left_suffixes_count, pool, thread_state);
            
                SaTy names = renumber_and_mark_distinct_lms_suffixes_32s_4k_omp(SA, n, m, pool, thread_state);
            
                if (names < m)
                {
                    SaTy f = compact_lms_suffixes_32s_omp(T, SA, n, m, fs, pool, thread_state);
                
                    if (main_32s(SA + n + fs - m + f, SA, m - f, names - f, fs + n - 2 * m + f, pool, thread_state) != 0)
                    {
                        return -2;
                    }
                
                    reconstruct_compacted_lms_suffixes_32s_2k_omp(T, SA, n, k, m, fs, f, buckets, pool, thread_state);
                }
                else
                {
                    count_lms_suffixes_32s_2k(T, n, k, buckets);
                }
            
                initialize_buckets_start_and_end_32s_4k(k, buckets);
                place_lms_suffixes_histogram_32s_4k(SA, n, k, m, buckets);
                induce_final_order_32s_4k(T, SA, n, k, buckets, pool, thread_state);
            }
            else
            {
                SA[0] = SA[n - 1];

                initialize_buckets_start_and_end_32s_6k(k, buckets);
                place_lms_suffixes_histogram_32s_6k(SA, n, k, m, buckets);
                induce_final_order_32s_6k(T, SA, n, k, buckets, pool, thread_state);
            }

            return 0;
        }
        else if (k > 0 && fs / k >= 4)
        {
            ptrdiff_t alignment = (fs - 1024) / k >= 4 ? 1024 : 16;
            SaTy* RESTRICT buckets = (fs - alignment) / k >= 4 ? (SaTy*)align_up(&SA[n + fs - 4 * k - alignment], (size_t)alignment * sizeof(SaTy)) : &SA[n + fs - 4 * k];

            SaTy m = count_and_gather_lms_suffixes_32s_2k_omp(T, SA, n, k, buckets, pool, thread_state);
            if (m > 1)
            {
                initialize_buckets_for_radix_and_partial_sorting_32s_4k(T, k, buckets, SA[n - m]);

                radix_sort_lms_suffixes_32s_2k_omp(T, SA, n, m, &buckets[1], pool, thread_state);
                radix_sort_set_markers_32s_4k_omp(SA, k, &buckets[1], pool);

                place_lms_suffixes_interval_32s_4k(SA, n, k, m - 1, buckets);
                induce_partial_order_32s_4k_omp(T, SA, n, k, buckets, pool, thread_state);

                SaTy names = renumber_and_mark_distinct_lms_suffixes_32s_4k_omp(SA, n, m, pool, thread_state);
                if (names < m)
                {
                    SaTy f = compact_lms_suffixes_32s_omp(T, SA, n, m, fs, pool, thread_state);

                    if (main_32s(SA + n + fs - m + f, SA, m - f, names - f, fs + n - 2 * m + f, pool, thread_state) != 0)
                    {
                        return -2;
                    }

                    reconstruct_compacted_lms_suffixes_32s_2k_omp(T, SA, n, k, m, fs, f, buckets, pool, thread_state);
                }
                else
                {
                    count_lms_suffixes_32s_2k(T, n, k, buckets);
                }
            }
            else
            {
                SA[0] = SA[n - 1];
            }

            initialize_buckets_start_and_end_32s_4k(k, buckets);
            place_lms_suffixes_histogram_32s_4k(SA, n, k, m, buckets);
            induce_final_order_32s_4k(T, SA, n, k, buckets, pool, thread_state);

            return 0;
        }
        else if (k > 0 && fs / k >= 2)
        {
            ptrdiff_t alignment = (fs - 1024) / k >= 2 ? 1024 : 16;
            SaTy* RESTRICT buckets = (fs - alignment) / k >= 2 ? (SaTy*)align_up(&SA[n + fs - 2 * k - alignment], (size_t)alignment * sizeof(SaTy)) : &SA[n + fs - 2 * k];

            SaTy m = count_and_gather_lms_suffixes_32s_2k_omp(T, SA, n, k, buckets, pool, thread_state);
            if (m > 1)
            {
                initialize_buckets_for_lms_suffixes_radix_sort_32s_2k(T, k, buckets, SA[n - m]);

                radix_sort_lms_suffixes_32s_2k_omp(T, SA, n, m, &buckets[1], pool, thread_state);
                place_lms_suffixes_interval_32s_2k(SA, n, k, m - 1, buckets);

                initialize_buckets_start_and_end_32s_2k(k, buckets);
                induce_partial_order_32s_2k_omp(T, SA, n, k, buckets, pool, thread_state);

                SaTy names = renumber_and_mark_distinct_lms_suffixes_32s_1k_omp(T, SA, n, m, pool);
                if (names < m)
                {
                    SaTy f = compact_lms_suffixes_32s_omp(T, SA, n, m, fs, pool, thread_state);

                    if (main_32s(SA + n + fs - m + f, SA, m - f, names - f, fs + n - 2 * m + f, pool, thread_state) != 0)
                    {
                        return -2;
                    }

                    reconstruct_compacted_lms_suffixes_32s_2k_omp(T, SA, n, k, m, fs, f, buckets, pool, thread_state);
                }
                else
                {
                    count_lms_suffixes_32s_2k(T, n, k, buckets);
                }
            }
            else
            {
                SA[0] = SA[n - 1];
            }

            initialize_buckets_end_32s_2k(k, buckets);
            place_lms_suffixes_histogram_32s_2k(SA, n, k, m, buckets);

            initialize_buckets_start_and_end_32s_2k(k, buckets);
            induce_final_order_32s_2k(T, SA, n, k, buckets, pool, thread_state);

            return 0;
        }
        else
        {
            SaTy* buffer = fs < k ? (SaTy*)alloc_aligned((size_t)k * sizeof(SaTy), 4096) : (SaTy*)nullptr;

            ptrdiff_t alignment = fs - 1024 >= k ? 1024 : 16;
            SaTy* RESTRICT buckets = fs - alignment >= k ? (SaTy*)align_up(&SA[n + fs - k - alignment], (size_t)alignment * sizeof(SaTy)) : fs >= k ? &SA[n + fs - k] : buffer;

            if (buckets == nullptr) { return -2; }

            std::memset(SA, 0, (size_t)n * sizeof(SaTy));

            count_suffixes_32s(T, n, k, buckets);
            initialize_buckets_end_32s_1k(k, buckets);

            SaTy m = radix_sort_lms_suffixes_32s_1k(T, SA, n, buckets);
            if (m > 1)
            {
                induce_partial_order_32s_1k_omp(T, SA, n, k, buckets, pool, thread_state);

                SaTy names = renumber_and_mark_distinct_lms_suffixes_32s_1k_omp(T, SA, n, m, pool);
                if (names < m)
                {
                    if (buffer != nullptr) { free_aligned(buffer); buckets = nullptr; }

                    SaTy f = compact_lms_suffixes_32s_omp(T, SA, n, m, fs, pool, thread_state);

                    if (main_32s(SA + n + fs - m + f, SA, m - f, names - f, fs + n - 2 * m + f, pool, thread_state) != 0)
                    {
                        return -2;
                    }

                    reconstruct_compacted_lms_suffixes_32s_1k_omp(T, SA, n, m, fs, f, pool, thread_state);

                    if (buckets == nullptr) { buckets = buffer = (SaTy*)alloc_aligned((size_t)k * sizeof(SaTy), 4096); }
                    if (buckets == nullptr) { return -2; }
                }

                count_suffixes_32s(T, n, k, buckets);
                initialize_buckets_end_32s_1k(k, buckets);
                place_lms_suffixes_interval_32s_1k(T, SA, k, m, buckets);
            }

            induce_final_order_32s_1k(T, SA, n, k, buckets, pool, thread_state);
            free_aligned(buffer);

            return 0;
        }
    }

    static SaTy main_16u(const AlphabetTy* T, SaTy* SA, SaTy n, SaTy* RESTRICT buckets, SaTy bwt, SaTy r, SaTy* RESTRICT I, SaTy fs, SaTy* freq, mp::ThreadPool* pool, ThreadState* RESTRICT thread_state)
    {
        fs = fs < (saint_max - n) ? fs : (saint_max - n);

        SaTy m = count_and_gather_lms_suffixes_16u_omp(T, SA, n, buckets, pool, thread_state);

        initialize_buckets_start_and_end_16u(buckets, freq);

        if (m > 0)
        {
            SaTy first_lms_suffix = SA[n - m];
            SaTy left_suffixes_count = initialize_buckets_for_lms_suffixes_radix_sort_16u(T, buckets, first_lms_suffix);

            if (mp::getPoolSize(pool) > 1 && n >= 65536) { std::memset(SA, 0, ((size_t)n - (size_t)m) * sizeof(SaTy)); }
            radix_sort_lms_suffixes_16u_omp(T, SA, n, m, buckets, pool, thread_state);
            if (mp::getPoolSize(pool) > 1 && n >= 65536) { std::memset(&SA[(fast_sint_t)n - (fast_sint_t)m], 0, (size_t)m * sizeof(SaTy)); }

            initialize_buckets_for_partial_sorting_16u(T, buckets, first_lms_suffix, left_suffixes_count);
            induce_partial_order_16u_omp(T, SA, n, buckets, first_lms_suffix, left_suffixes_count, pool, thread_state);

            SaTy names = renumber_and_gather_lms_suffixes_16u_omp(SA, n, m, fs, pool, thread_state);
            if (names < m)
            {
                if (main_32s(SA + n + fs - m, SA, m, names, fs + n - 2 * m, pool, thread_state) != 0)
                {
                    return -2;
                }

                gather_lms_suffixes_16u_omp(T, SA, n, pool, thread_state);
                reconstruct_lms_suffixes_omp(SA, n, m, pool);
            }

            place_lms_suffixes_interval_16u(SA, n, m, buckets);
        }
        else
        {
            std::memset(SA, 0, (size_t)n * sizeof(SaTy));
        }

        return induce_final_order_16u_omp(T, SA, n, bwt, r, I, buckets, pool, thread_state);
    }

    static SaTy main_ctx(const Context* ctx, const AlphabetTy* T, SaTy* SA, SaTy n, SaTy bwt, SaTy r, SaTy* I, SaTy fs, SaTy* freq)
    {
        return ctx != nullptr && (ctx->buckets != nullptr && (ctx->thread_state != nullptr || ctx->pool))
            ? main_16u(T, SA, n, ctx->buckets, bwt, r, I, fs, freq, ctx->pool, ctx->thread_state)
            : -2;
    }

    static void bwt_copy_16u(AlphabetTy* RESTRICT U, SaTy* RESTRICT A, SaTy n)
    {
        const fast_sint_t prefetch_distance = 32;

        fast_sint_t i, j;
        for (i = 0, j = (fast_sint_t)n - 7; i < j; i += 8)
        {
            prefetchr(&A[i + prefetch_distance]);

            U[i + 0] = (AlphabetTy)A[i + 0];
            U[i + 1] = (AlphabetTy)A[i + 1];
            U[i + 2] = (AlphabetTy)A[i + 2];
            U[i + 3] = (AlphabetTy)A[i + 3];
            U[i + 4] = (AlphabetTy)A[i + 4];
            U[i + 5] = (AlphabetTy)A[i + 5];
            U[i + 6] = (AlphabetTy)A[i + 6];
            U[i + 7] = (AlphabetTy)A[i + 7];
        }

        for (j += 7; i < j; i += 1)
        {
            U[i] = (AlphabetTy)A[i];
        }
    }

    static void bwt_copy_16u_omp(AlphabetTy* RESTRICT U, SaTy* RESTRICT A, SaTy n, mp::ThreadPool* pool)
    {
        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = ((fast_sint_t)n / num_threads) & (-16);
            fast_sint_t omp_block_start = id * omp_block_stride;
            fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : (fast_sint_t)n - omp_block_start;

            bwt_copy_16u(U + omp_block_start, A + omp_block_start, (SaTy)omp_block_size);
        }, mp::ParallelCond{n >= 65536});
    }

    static UnbwtContext* unbwt_create_ctx_main(mp::ThreadPool* pool)
    {
        UnbwtContext* RESTRICT ctx = (UnbwtContext*)alloc_aligned(sizeof(UnbwtContext), 64);
        SaTy* RESTRICT bucket2 = (SaTy*)alloc_aligned(alphabet_size * sizeof(SaTy), 4096);
        AlphabetTy* RESTRICT fastbits = (AlphabetTy*)alloc_aligned((1 + (1 << UNBWT_FASTBITS)) * sizeof(AlphabetTy), 4096);
        SaTy* RESTRICT buckets = mp::getPoolSize(pool) > 1 ? (SaTy*)alloc_aligned(mp::getPoolSize(pool) * alphabet_size * sizeof(SaTy), 4096) : nullptr;

        if (ctx != nullptr && bucket2 != nullptr && fastbits != nullptr && (buckets != nullptr || mp::getPoolSize(pool) == 1))
        {
            ctx->bucket2 = bucket2;
            ctx->fastbits = fastbits;
            ctx->buckets = buckets;
            ctx->pool = pool;

            return ctx;
        }

        free_aligned(buckets);
        free_aligned(fastbits);
        free_aligned(bucket2);
        free_aligned(ctx);

        return nullptr;
    }

    static void unbwt_free_ctx_main(UnbwtContext* ctx)
    {
        if (ctx != nullptr)
        {
            free_aligned(ctx->buckets);
            free_aligned(ctx->fastbits);
            free_aligned(ctx->bucket2);
            free_aligned(ctx);
        }
    }

    static void unbwt_compute_histogram(const AlphabetTy* RESTRICT T, fast_sint_t n, SaTy* RESTRICT count)
    {
        fast_sint_t i; for (i = 0; i < n; i += 1) { count[T[i]]++; }
    }

    static void unbwt_calculate_fastbits(SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift)
    {
        fast_uint_t v, w, sum;
        for (v = 0, sum = 1, w = 0; w < alphabet_size; ++w)
        {
            fast_uint_t prev = sum; sum += bucket2[w]; bucket2[w] = (SaTy)prev;
            if (prev != sum)
            {
                for (; v <= ((sum - 1) >> shift); ++v) { fastbits[v] = (AlphabetTy)w; }
            }
        }
    }

    static void unbwt_calculate_P(const AlphabetTy* RESTRICT T, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, fast_uint_t index, fast_sint_t omp_block_start, fast_sint_t omp_block_end)
    {
        {
            fast_sint_t i = omp_block_start, j = (fast_sint_t)index; if (omp_block_end < j) { j = omp_block_end; }
            for (; i < j; ++i) { fast_uint_t c = T[i]; P[bucket2[c]++] = (SaTy)i; }
        }

        {
            fast_sint_t i = (fast_sint_t)index, j = omp_block_end; if (omp_block_start > i) { i = omp_block_start; }
            for (T -= 1, i += 1; i <= j; ++i) { fast_uint_t c = T[i]; P[bucket2[c]++] = (SaTy)i; }
        }
    }

    static void unbwt_init_single(const AlphabetTy* RESTRICT T, SaTy* RESTRICT P, SaTy n, const SaTy* freq, const SaTy* RESTRICT I, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits)
    {
        fast_uint_t index = I[0];
        fast_uint_t shift = 0; while ((n >> shift) > (1 << UNBWT_FASTBITS)) { shift++; }

        if (freq != nullptr)
        {
            std::memcpy(bucket2, freq, alphabet_size * sizeof(SaTy));
        }
        else
        {
            std::memset(bucket2, 0, alphabet_size * sizeof(SaTy));
            unbwt_compute_histogram(T, n, bucket2);
        }

        unbwt_calculate_fastbits(bucket2, fastbits, shift);
        unbwt_calculate_P(T, P, bucket2, index, 0, n);
    }

    static void unbwt_init_parallel(const AlphabetTy* RESTRICT T, SaTy* RESTRICT P, SaTy n, const SaTy* freq, const SaTy* RESTRICT I, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, SaTy* RESTRICT buckets, mp::ThreadPool* pool)
    {
        fast_uint_t index = I[0];
        fast_uint_t shift = 0; while ((n >> shift) > (1 << UNBWT_FASTBITS)) { shift++; }

        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            if (num_threads == 1)
            {
                unbwt_init_single(T, P, n, freq, I, bucket2, fastbits);
            }
            else
            {
                SaTy* RESTRICT bucket2_local = buckets + id * alphabet_size;
                fast_sint_t omp_block_stride = (n / num_threads) & (-16);
                fast_sint_t omp_block_start = id * omp_block_stride;
                fast_sint_t omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

                std::memset(bucket2_local, 0, alphabet_size * sizeof(SaTy));
                unbwt_compute_histogram(T + omp_block_start, omp_block_size, bucket2_local);

                mp::barrier(barrier);

                SaTy* RESTRICT bucket2_temp = buckets;
                omp_block_stride = (alphabet_size / num_threads) & (-16);
                omp_block_start = id * omp_block_stride;
                omp_block_size = id < num_threads - 1 ? omp_block_stride : alphabet_size - omp_block_start;

                std::memset(bucket2 + omp_block_start, 0, (size_t)omp_block_size * sizeof(SaTy));

                fast_sint_t t;
                for (t = 0; t < num_threads; ++t, bucket2_temp += alphabet_size)
                {
                    fast_sint_t c; for (c = omp_block_start; c < omp_block_start + omp_block_size; c += 1) { auto A = to_unsigned(bucket2[c]), B = to_unsigned(bucket2_temp[c]); bucket2[c] = A + B; bucket2_temp[c] = A; }
                }

                mp::barrier(barrier);

                if (id == 0)
                {
                    unbwt_calculate_fastbits(bucket2, fastbits, shift);
                }

                mp::barrier(barrier);

                bucket2_local = buckets + id * alphabet_size;
                omp_block_stride = (n / num_threads) & (-16);
                omp_block_start = id * omp_block_stride;
                omp_block_size = id < num_threads - 1 ? omp_block_stride : n - omp_block_start;

                fast_sint_t c; for (c = 0; c < alphabet_size; c += 1) { auto A = to_unsigned(bucket2[c]), B = to_unsigned(bucket2_local[c]); bucket2_local[c] = A + B; }

                unbwt_calculate_P(T, P, bucket2_local, index, omp_block_start, omp_block_start + omp_block_size);
            
                mp::barrier(barrier);

                if (id == 0)
                {
                    std::memcpy(bucket2, buckets + (num_threads - 1) * alphabet_size, alphabet_size * sizeof(SaTy));
                }
            }
        }, mp::ParallelCond{n >= 65536});
    }

    static void unbwt_decode_1(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t* i0, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;

        fast_uint_t i, p0 = *i0;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
        }

        *i0 = p0;
    }

    static void unbwt_decode_2(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t r, fast_uint_t* i0, fast_uint_t* i1, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;
        AlphabetTy* RESTRICT U1 = U0 + r;

        fast_uint_t i, p0 = *i0, p1 = *i1;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
            AlphabetTy c1 = fastbits[p1 >> shift]; if (bucket2[c1] <= p1) { do { c1++; } while (bucket2[c1] <= p1); } p1 = P[p1]; U1[i] = c1;
        }

        *i0 = p0; *i1 = p1;
    }

    static void unbwt_decode_3(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t r, fast_uint_t* i0, fast_uint_t* i1, fast_uint_t* i2, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;
        AlphabetTy* RESTRICT U1 = U0 + r;
        AlphabetTy* RESTRICT U2 = U1 + r;

        fast_uint_t i, p0 = *i0, p1 = *i1, p2 = *i2;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
            AlphabetTy c1 = fastbits[p1 >> shift]; if (bucket2[c1] <= p1) { do { c1++; } while (bucket2[c1] <= p1); } p1 = P[p1]; U1[i] = c1;
            AlphabetTy c2 = fastbits[p2 >> shift]; if (bucket2[c2] <= p2) { do { c2++; } while (bucket2[c2] <= p2); } p2 = P[p2]; U2[i] = c2;
        }

        *i0 = p0; *i1 = p1; *i2 = p2;
    }

    static void unbwt_decode_4(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t r, fast_uint_t* i0, fast_uint_t* i1, fast_uint_t* i2, fast_uint_t* i3, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;
        AlphabetTy* RESTRICT U1 = U0 + r;
        AlphabetTy* RESTRICT U2 = U1 + r;
        AlphabetTy* RESTRICT U3 = U2 + r;

        fast_uint_t i, p0 = *i0, p1 = *i1, p2 = *i2, p3 = *i3;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
            AlphabetTy c1 = fastbits[p1 >> shift]; if (bucket2[c1] <= p1) { do { c1++; } while (bucket2[c1] <= p1); } p1 = P[p1]; U1[i] = c1;
            AlphabetTy c2 = fastbits[p2 >> shift]; if (bucket2[c2] <= p2) { do { c2++; } while (bucket2[c2] <= p2); } p2 = P[p2]; U2[i] = c2;
            AlphabetTy c3 = fastbits[p3 >> shift]; if (bucket2[c3] <= p3) { do { c3++; } while (bucket2[c3] <= p3); } p3 = P[p3]; U3[i] = c3;
        }

        *i0 = p0; *i1 = p1; *i2 = p2; *i3 = p3;
    }

    static void unbwt_decode_5(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t r, fast_uint_t* i0, fast_uint_t* i1, fast_uint_t* i2, fast_uint_t* i3, fast_uint_t* i4, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;
        AlphabetTy* RESTRICT U1 = U0 + r;
        AlphabetTy* RESTRICT U2 = U1 + r;
        AlphabetTy* RESTRICT U3 = U2 + r;
        AlphabetTy* RESTRICT U4 = U3 + r;

        fast_uint_t i, p0 = *i0, p1 = *i1, p2 = *i2, p3 = *i3, p4 = *i4;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
            AlphabetTy c1 = fastbits[p1 >> shift]; if (bucket2[c1] <= p1) { do { c1++; } while (bucket2[c1] <= p1); } p1 = P[p1]; U1[i] = c1;
            AlphabetTy c2 = fastbits[p2 >> shift]; if (bucket2[c2] <= p2) { do { c2++; } while (bucket2[c2] <= p2); } p2 = P[p2]; U2[i] = c2;
            AlphabetTy c3 = fastbits[p3 >> shift]; if (bucket2[c3] <= p3) { do { c3++; } while (bucket2[c3] <= p3); } p3 = P[p3]; U3[i] = c3;
            AlphabetTy c4 = fastbits[p4 >> shift]; if (bucket2[c4] <= p4) { do { c4++; } while (bucket2[c4] <= p4); } p4 = P[p4]; U4[i] = c4;
        }

        *i0 = p0; *i1 = p1; *i2 = p2; *i3 = p3; *i4 = p4;
    }

    static void unbwt_decode_6(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t r, fast_uint_t* i0, fast_uint_t* i1, fast_uint_t* i2, fast_uint_t* i3, fast_uint_t* i4, fast_uint_t* i5, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;
        AlphabetTy* RESTRICT U1 = U0 + r;
        AlphabetTy* RESTRICT U2 = U1 + r;
        AlphabetTy* RESTRICT U3 = U2 + r;
        AlphabetTy* RESTRICT U4 = U3 + r;
        AlphabetTy* RESTRICT U5 = U4 + r;

        fast_uint_t i, p0 = *i0, p1 = *i1, p2 = *i2, p3 = *i3, p4 = *i4, p5 = *i5;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
            AlphabetTy c1 = fastbits[p1 >> shift]; if (bucket2[c1] <= p1) { do { c1++; } while (bucket2[c1] <= p1); } p1 = P[p1]; U1[i] = c1;
            AlphabetTy c2 = fastbits[p2 >> shift]; if (bucket2[c2] <= p2) { do { c2++; } while (bucket2[c2] <= p2); } p2 = P[p2]; U2[i] = c2;
            AlphabetTy c3 = fastbits[p3 >> shift]; if (bucket2[c3] <= p3) { do { c3++; } while (bucket2[c3] <= p3); } p3 = P[p3]; U3[i] = c3;
            AlphabetTy c4 = fastbits[p4 >> shift]; if (bucket2[c4] <= p4) { do { c4++; } while (bucket2[c4] <= p4); } p4 = P[p4]; U4[i] = c4;
            AlphabetTy c5 = fastbits[p5 >> shift]; if (bucket2[c5] <= p5) { do { c5++; } while (bucket2[c5] <= p5); } p5 = P[p5]; U5[i] = c5;
        }

        *i0 = p0; *i1 = p1; *i2 = p2; *i3 = p3; *i4 = p4; *i5 = p5;
    }

    static void unbwt_decode_7(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t r, fast_uint_t* i0, fast_uint_t* i1, fast_uint_t* i2, fast_uint_t* i3, fast_uint_t* i4, fast_uint_t* i5, fast_uint_t* i6, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;
        AlphabetTy* RESTRICT U1 = U0 + r;
        AlphabetTy* RESTRICT U2 = U1 + r;
        AlphabetTy* RESTRICT U3 = U2 + r;
        AlphabetTy* RESTRICT U4 = U3 + r;
        AlphabetTy* RESTRICT U5 = U4 + r;
        AlphabetTy* RESTRICT U6 = U5 + r;

        fast_uint_t i, p0 = *i0, p1 = *i1, p2 = *i2, p3 = *i3, p4 = *i4, p5 = *i5, p6 = *i6;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
            AlphabetTy c1 = fastbits[p1 >> shift]; if (bucket2[c1] <= p1) { do { c1++; } while (bucket2[c1] <= p1); } p1 = P[p1]; U1[i] = c1;
            AlphabetTy c2 = fastbits[p2 >> shift]; if (bucket2[c2] <= p2) { do { c2++; } while (bucket2[c2] <= p2); } p2 = P[p2]; U2[i] = c2;
            AlphabetTy c3 = fastbits[p3 >> shift]; if (bucket2[c3] <= p3) { do { c3++; } while (bucket2[c3] <= p3); } p3 = P[p3]; U3[i] = c3;
            AlphabetTy c4 = fastbits[p4 >> shift]; if (bucket2[c4] <= p4) { do { c4++; } while (bucket2[c4] <= p4); } p4 = P[p4]; U4[i] = c4;
            AlphabetTy c5 = fastbits[p5 >> shift]; if (bucket2[c5] <= p5) { do { c5++; } while (bucket2[c5] <= p5); } p5 = P[p5]; U5[i] = c5;
            AlphabetTy c6 = fastbits[p6 >> shift]; if (bucket2[c6] <= p6) { do { c6++; } while (bucket2[c6] <= p6); } p6 = P[p6]; U6[i] = c6;
        }

        *i0 = p0; *i1 = p1; *i2 = p2; *i3 = p3; *i4 = p4; *i5 = p5; *i6 = p6;
    }

    static void unbwt_decode_8(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_uint_t shift, fast_uint_t r, fast_uint_t* i0, fast_uint_t* i1, fast_uint_t* i2, fast_uint_t* i3, fast_uint_t* i4, fast_uint_t* i5, fast_uint_t* i6, fast_uint_t* i7, fast_uint_t k)
    {
        AlphabetTy* RESTRICT U0 = U;
        AlphabetTy* RESTRICT U1 = U0 + r;
        AlphabetTy* RESTRICT U2 = U1 + r;
        AlphabetTy* RESTRICT U3 = U2 + r;
        AlphabetTy* RESTRICT U4 = U3 + r;
        AlphabetTy* RESTRICT U5 = U4 + r;
        AlphabetTy* RESTRICT U6 = U5 + r;
        AlphabetTy* RESTRICT U7 = U6 + r;

        fast_uint_t i, p0 = *i0, p1 = *i1, p2 = *i2, p3 = *i3, p4 = *i4, p5 = *i5, p6 = *i6, p7 = *i7;

        for (i = 0; i != k; ++i)
        {
            AlphabetTy c0 = fastbits[p0 >> shift]; if (bucket2[c0] <= p0) { do { c0++; } while (bucket2[c0] <= p0); } p0 = P[p0]; U0[i] = c0;
            AlphabetTy c1 = fastbits[p1 >> shift]; if (bucket2[c1] <= p1) { do { c1++; } while (bucket2[c1] <= p1); } p1 = P[p1]; U1[i] = c1;
            AlphabetTy c2 = fastbits[p2 >> shift]; if (bucket2[c2] <= p2) { do { c2++; } while (bucket2[c2] <= p2); } p2 = P[p2]; U2[i] = c2;
            AlphabetTy c3 = fastbits[p3 >> shift]; if (bucket2[c3] <= p3) { do { c3++; } while (bucket2[c3] <= p3); } p3 = P[p3]; U3[i] = c3;
            AlphabetTy c4 = fastbits[p4 >> shift]; if (bucket2[c4] <= p4) { do { c4++; } while (bucket2[c4] <= p4); } p4 = P[p4]; U4[i] = c4;
            AlphabetTy c5 = fastbits[p5 >> shift]; if (bucket2[c5] <= p5) { do { c5++; } while (bucket2[c5] <= p5); } p5 = P[p5]; U5[i] = c5;
            AlphabetTy c6 = fastbits[p6 >> shift]; if (bucket2[c6] <= p6) { do { c6++; } while (bucket2[c6] <= p6); } p6 = P[p6]; U6[i] = c6;
            AlphabetTy c7 = fastbits[p7 >> shift]; if (bucket2[c7] <= p7) { do { c7++; } while (bucket2[c7] <= p7); } p7 = P[p7]; U7[i] = c7;
        }

        *i0 = p0; *i1 = p1; *i2 = p2; *i3 = p3; *i4 = p4; *i5 = p5; *i6 = p6; *i7 = p7;
    }

    static void unbwt_decode(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy n, SaTy r, const SaTy* RESTRICT I, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, fast_sint_t blocks, fast_uint_t reminder)
    {
        fast_uint_t shift = 0; while ((n >> shift) > (1 << UNBWT_FASTBITS)) { shift++; }
        fast_uint_t offset = 0;

        while (blocks > 8)
        {
            fast_uint_t i0 = I[0], i1 = I[1], i2 = I[2], i3 = I[3], i4 = I[4], i5 = I[5], i6 = I[6], i7 = I[7];
            unbwt_decode_8(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, &i5, &i6, &i7, (fast_uint_t)r);
            I += 8; blocks -= 8; offset += 8 * (fast_uint_t)r;
        }

        if (blocks == 1)
        {
            fast_uint_t i0 = I[0];
            unbwt_decode_1(U + offset, P, bucket2, fastbits, shift, &i0, reminder);
        }
        else if (blocks == 2)
        {
            fast_uint_t i0 = I[0], i1 = I[1];
            unbwt_decode_2(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, reminder);
            unbwt_decode_1(U + offset + reminder, P, bucket2, fastbits, shift, &i0, ((fast_uint_t)r) - reminder);
        }
        else if (blocks == 3)
        {
            fast_uint_t i0 = I[0], i1 = I[1], i2 = I[2];
            unbwt_decode_3(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, reminder);
            unbwt_decode_2(U + offset + reminder, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, ((fast_uint_t)r) - reminder);
        }
        else if (blocks == 4)
        {
            fast_uint_t i0 = I[0], i1 = I[1], i2 = I[2], i3 = I[3];
            unbwt_decode_4(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, reminder);
            unbwt_decode_3(U + offset + reminder, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, ((fast_uint_t)r) - reminder);
        }
        else if (blocks == 5)
        {
            fast_uint_t i0 = I[0], i1 = I[1], i2 = I[2], i3 = I[3], i4 = I[4];
            unbwt_decode_5(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, reminder);
            unbwt_decode_4(U + offset + reminder, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, ((fast_uint_t)r) - reminder);
        }
        else if (blocks == 6)
        {
            fast_uint_t i0 = I[0], i1 = I[1], i2 = I[2], i3 = I[3], i4 = I[4], i5 = I[5];
            unbwt_decode_6(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, &i5, reminder);
            unbwt_decode_5(U + offset + reminder, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, ((fast_uint_t)r) - reminder);
        }
        else if (blocks == 7)
        {
            fast_uint_t i0 = I[0], i1 = I[1], i2 = I[2], i3 = I[3], i4 = I[4], i5 = I[5], i6 = I[6];
            unbwt_decode_7(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, &i5, &i6, reminder);
            unbwt_decode_6(U + offset + reminder, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, &i5, ((fast_uint_t)r) - reminder);
        }
        else
        {
            fast_uint_t i0 = I[0], i1 = I[1], i2 = I[2], i3 = I[3], i4 = I[4], i5 = I[5], i6 = I[6], i7 = I[7];
            unbwt_decode_8(U + offset, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, &i5, &i6, &i7, reminder);
            unbwt_decode_7(U + offset + reminder, P, bucket2, fastbits, shift, (fast_uint_t)r, &i0, &i1, &i2, &i3, &i4, &i5, &i6, ((fast_uint_t)r) - reminder);
        }
    }

    static void unbwt_decode_omp(AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy n, SaTy r, const SaTy* RESTRICT I, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, mp::ThreadPool* pool)
    {
        fast_sint_t blocks = 1 + (((fast_sint_t)n - 1) / (fast_sint_t)r);
        fast_uint_t reminder = (fast_uint_t)n - ((fast_uint_t)r * ((fast_uint_t)blocks - 1));

        mp::runParallel(pool, [&](std::ptrdiff_t id, std::ptrdiff_t num_threads, mp::Barrier* barrier)
        {
            fast_sint_t omp_block_stride = blocks / num_threads;
            fast_sint_t omp_block_reminder = blocks % num_threads;
            fast_sint_t omp_block_size = omp_block_stride + (id < omp_block_reminder);
            fast_sint_t omp_block_start = omp_block_stride * id + (id < omp_block_reminder ? id : omp_block_reminder);

            unbwt_decode(U + r * omp_block_start, P, n, r, I + omp_block_start, bucket2, fastbits, omp_block_size, id < num_threads - 1 ? (fast_uint_t)r : reminder);
        }, mp::MaximumWorkers{ (size_t)blocks }, mp::ParallelCond{n >= 65536});
    }

    static SaTy unbwt_core(const AlphabetTy* RESTRICT T, AlphabetTy* RESTRICT U, SaTy* RESTRICT P, SaTy n, const SaTy* freq, SaTy r, const SaTy* RESTRICT I, SaTy* RESTRICT bucket2, AlphabetTy* RESTRICT fastbits, SaTy* RESTRICT buckets, mp::ThreadPool* pool)
    {
        if (mp::getPoolSize(pool) > 1 && n >= 262144)
        {
            unbwt_init_parallel(T, P, n, freq, I, bucket2, fastbits, buckets, pool);
        }
        else
        {
            unbwt_init_single(T, P, n, freq, I, bucket2, fastbits);
        }

        unbwt_decode_omp(U, P, n, r, I, bucket2, fastbits, pool);
        return 0;
    }

    static SaTy unbwt_main(const AlphabetTy* T, AlphabetTy* U, SaTy* P, SaTy n, const SaTy* freq, SaTy r, const SaTy* I, mp::ThreadPool* pool)
    {
        fast_uint_t shift = 0; while ((n >> shift) > (1 << UNBWT_FASTBITS)) { shift++; }

        SaTy* RESTRICT bucket2 = (SaTy*)alloc_aligned(alphabet_size * sizeof(SaTy), 4096);
        AlphabetTy* RESTRICT fastbits = (AlphabetTy*)alloc_aligned(((size_t)1 + (size_t)(n >> shift)) * sizeof(AlphabetTy), 4096);
        SaTy* RESTRICT buckets = mp::getPoolSize(pool) > 1 && n >= 262144 ? (SaTy*)alloc_aligned(mp::getPoolSize(pool) * alphabet_size * sizeof(SaTy), 4096) : nullptr;

        SaTy index = bucket2 != nullptr && fastbits != nullptr && (buckets != nullptr || mp::getPoolSize(pool) == 1 || n < 262144)
            ? unbwt_core(T, U, P, n, freq, r, I, bucket2, fastbits, buckets, pool)
            : -2;

        free_aligned(buckets);
        free_aligned(fastbits);
        free_aligned(bucket2);

        return index;
    }

    static SaTy unbwt_main_ctx(const UnbwtContext* ctx, const AlphabetTy* T, AlphabetTy* U, SaTy* P, SaTy n, const SaTy* freq, SaTy r, const SaTy* I)
    {
        return ctx != nullptr && ctx->bucket2 != nullptr && ctx->fastbits != nullptr && (ctx->buckets != nullptr || ctx->pool)
            ? unbwt_core(T, U, P, n, freq, r, I, ctx->bucket2, ctx->fastbits, ctx->buckets, ctx->pool)
            : -2;
    }

    static void* unbwt_create_ctx(void)
    {
        return (void*)unbwt_create_ctx_main(nullptr);
    }

    static void unbwt_free_ctx(void* ctx)
    {
        unbwt_free_ctx_main((UnbwtContext*)ctx);
    }

    static SaTy unbwt_aux(const AlphabetTy* T, AlphabetTy* U, SaTy* A, SaTy n, const SaTy* freq, SaTy r, const SaTy* I, mp::ThreadPool* pool = nullptr)
    {
        if ((T == nullptr) || (U == nullptr) || (A == nullptr) || (n < 0) || ((r != n) && ((r < 2) || ((r & (r - 1)) != 0))) || (I == nullptr))
        {
            return -1;
        }
        else if (n <= 1)
        {
            if (I[0] != n) { return -1; }
            if (n == 1) { U[0] = T[0]; }
            return 0;
        }

        fast_sint_t t; for (t = 0; t <= (n - 1) / r; ++t) { if (I[t] <= 0 || I[t] > n) { return -1; } }

        return unbwt_main(T, U, A, n, freq, r, I, pool);
    }

    static SaTy unbwt_aux_ctx(const void* ctx, const AlphabetTy* T, AlphabetTy* U, SaTy* A, SaTy n, const SaTy* freq, SaTy r, const SaTy* I)
    {
        if ((T == nullptr) || (U == nullptr) || (A == nullptr) || (n < 0) || ((r != n) && ((r < 2) || ((r & (r - 1)) != 0))) || (I == nullptr))
        {
            return -1;
        }
        else if (n <= 1)
        {
            if (I[0] != n) { return -1; }
            if (n == 1) { U[0] = T[0]; }
            return 0;
        }

        fast_sint_t t; for (t = 0; t <= (n - 1) / r; ++t) { if (I[t] <= 0 || I[t] > n) { return -1; } }

        return unbwt_main_ctx((const UnbwtContext*)ctx, T, U, A, n, freq, r, I);
    }

    static SaTy unbwt_ctx(const void* ctx, const AlphabetTy* T, AlphabetTy* U, SaTy* A, SaTy n, const SaTy* freq, SaTy i)
    {
        return unbwt_aux_ctx(ctx, T, U, A, n, freq, n, &i);
    }

public:

    static SaTy sais(const AlphabetTy* T, SaTy* SA, SaTy n, SaTy fs, SaTy* freq, mp::ThreadPool* pool = nullptr)
    {
        if ((T == nullptr) || (SA == nullptr) || (n < 0) || (fs < 0))
        {
            return -1;
        }
        else if (n < 2)
        {
            if (freq != nullptr) { std::memset(freq, 0, alphabet_size * sizeof(SaTy)); }
            if (n == 1) { SA[0] = 0; if (freq != nullptr) { freq[T[0]]++; } }
            return 0;
        }

        return main(T, SA, n, 0, 0, nullptr, fs, freq, pool);
    }

    static SaTy main(const AlphabetTy* T, SaTy* SA, SaTy n, SaTy bwt, SaTy r, SaTy* I, SaTy fs, SaTy* freq, mp::ThreadPool* pool = nullptr)
    {
        ThreadState* RESTRICT thread_state = mp::getPoolSize(pool) > 1 ? alloc_thread_state(pool) : nullptr;
        SaTy* RESTRICT buckets = (SaTy*)alloc_aligned(8 * alphabet_size * sizeof(SaTy), 4096);

        SaTy index = buckets != nullptr && (thread_state != nullptr || mp::getPoolSize(pool) == 1)
            ? main_16u(T, SA, n, buckets, bwt, r, I, fs, freq, pool, thread_state)
            : -2;

        free_aligned(buckets);
        free_thread_state(thread_state);

        return index;
    }

    static SaTy bwt(const AlphabetTy* T, AlphabetTy* U, SaTy* A, SaTy n, SaTy fs, SaTy* freq, mp::ThreadPool* pool)
    {
        if ((T == nullptr) || (U == nullptr) || (A == nullptr) || (n < 0) || (fs < 0))
        {
            return -1;
        }
        else if (n <= 1)
        {
            if (freq != nullptr) { std::memset(freq, 0, alphabet_size * sizeof(SaTy)); }
            if (n == 1) { U[0] = T[0]; if (freq != nullptr) { freq[T[0]]++; } }
            return n;
        }

        SaTy index = main(T, A, n, 1, 0, nullptr, fs, freq, pool);
        if (index >= 0)
        {
            index++;

            U[0] = T[n - 1];
            bwt_copy_16u_omp(U + 1, A, index - 1, pool);
            bwt_copy_16u_omp(U + index, A + index, n - index, pool);
        }
        return index;
    }

    static SaTy unbwt(const AlphabetTy* T, AlphabetTy* U, SaTy* A, SaTy n, const SaTy* freq, SaTy i, mp::ThreadPool* pool = nullptr)
    {
        return unbwt_aux(T, U, A, n, freq, n, &i, pool);
    }

};

template<class AlphabetTy, class SaTy>
SaTy sais(const AlphabetTy* T, SaTy* SA, SaTy n, SaTy bwt, SaTy r, SaTy* I, SaTy fs, SaTy* freq = nullptr, mp::ThreadPool* pool = nullptr)
{
    return SaisImpl<AlphabetTy, SaTy>::main(T, SA, n, bwt, r, I, fs, freq, pool);
}

template<class AlphabetTy, class SaTy>
SaTy bwt(const AlphabetTy* T, AlphabetTy* U, SaTy* A, SaTy n, SaTy fs, SaTy* freq = nullptr, mp::ThreadPool* pool = nullptr)
{
    return SaisImpl<AlphabetTy, SaTy>::bwt(T, U, A, n, fs, freq, pool);
}

template<class AlphabetTy, class SaTy>
SaTy unbwt(const AlphabetTy* T, AlphabetTy* U, SaTy* A, SaTy n, const SaTy* freq, SaTy i, mp::ThreadPool* pool = nullptr)
{
    return SaisImpl<AlphabetTy, SaTy>::unbwt(T, U, A, n, freq, i, pool);
}

}