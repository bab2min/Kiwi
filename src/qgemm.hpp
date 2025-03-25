#pragma once
#include <cstring>
#include <kiwi/Types.h>
#include "qgemm.h"
#include "SIMD.hpp"

namespace kiwi
{
	namespace qgemm
	{
		static constexpr size_t TLBSize = 32768;

		template<size_t size = TLBSize>
		struct SharedThreadLocalBuffer
		{
			thread_local static uint8_t buffer[size];
			static uint8_t* get()
			{
				return buffer;
			}
		};

		template<size_t size>
		thread_local uint8_t SharedThreadLocalBuffer<size>::buffer[size];

		template<ArchType archType>
		int32_t dotprod(
			const uint8_t* a, const int8_t* b, size_t n
		)
		{
			simd::Operator<archType> op;
			return op.dotprod(a, b, n);
		}

		template<ArchType archType>
		void scatteredGEMMBaseline(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		)
		{
			thread_local Vector<uint8_t> buffer;
			buffer.resize((m + n) * (k + 8));
			uint8_t* aBuffer = buffer.data();
			int8_t* bBuffer = reinterpret_cast<int8_t*>(aBuffer + m * (k + 8));
			simd::Operator<archType> op;

			for (size_t i = 0; i < m; ++i)
			{
				std::memcpy(aBuffer + i * (k + 8), &aBase[aIdx[i] * aIdxScale], k + 8);
			}
			for (size_t i = 0; i < n; ++i)
			{
				std::memcpy(bBuffer + i * (k + 8), &bBase[bIdx[i] * bIdxScale], k + 8);
			}

			for (size_t i = 0; i < m; ++i)
			{
				for (size_t j = 0; j < n; ++j)
				{
					const auto* aPtr = aBuffer + i * (k + 8);
					const auto* bPtr = bBuffer + j * (k + 8);
					int32_t acc = op.dotprod(aPtr, bPtr, k);
					const float contextScale = *reinterpret_cast<const float*>(aPtr + k),
						outputScale = *reinterpret_cast<const float*>(bPtr + k),
						contextBias = *reinterpret_cast<const float*>(aPtr + k + 4);
					const int32_t hsum = *reinterpret_cast<const int32_t*>(bPtr + k + 4);
					c[i * ldc + j] = (acc - hsum) * contextScale * outputScale + contextBias;
				}
			}
		}

		template<ArchType archType>
		inline void scatteredGEMV(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			const int32_t bIdx[1] = { 0 };
			return scatteredGEMMBaseline<archType>(m, 1, k, aBase, aIdx, aIdxScale, b, bIdx, 0, c, 1);
		}

		template<ArchType archType>
		inline void scatteredGEMV8x1(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* b,
			float* c
		)
		{
			const int32_t bIdx[1] = { 0 };
			return scatteredGEMMBaseline<archType>(m, 1, k, aBase, aIdx, aIdxScale, b, bIdx, 0, c, 1);
		}

		template<ArchType archType>
		inline void scatteredGEMV2(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMMBaseline<archType>(m, 2, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, 2);
		}

		template<ArchType archType>
		inline void scatteredGEMV3(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMMBaseline<archType>(m, 3, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, 3);
		}

		template<ArchType archType>
		inline void scatteredGEMV4(
			size_t m, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c
		)
		{
			return scatteredGEMMBaseline<archType>(m, 4, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, 4);
		}

		template<ArchType archType>
		struct ScatteredGEMMSmall
		{
			template<size_t m, size_t n>
			static void op(
				size_t, size_t, size_t k,
				const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
				const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
				float* c, size_t ldc)
			{
				return scatteredGEMMBaseline<archType>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}
		};

		template<ArchType archType>
		void scatteredGEMMOpt(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		)
		{
			using Fn = decltype(&scatteredGEMMBaseline<ArchType::none>);
			static constexpr Fn fnTable[] = {
				scatteredGEMMBaseline<archType>,
				ScatteredGEMMSmall<archType>::template op<1, 2>,
				ScatteredGEMMSmall<archType>::template op<1, 3>,
				ScatteredGEMMSmall<archType>::template op<2, 1>,
				ScatteredGEMMSmall<archType>::template op<2, 2>,
				ScatteredGEMMSmall<archType>::template op<2, 3>,
				ScatteredGEMMSmall<archType>::template op<3, 1>,
				ScatteredGEMMSmall<archType>::template op<3, 2>,
				ScatteredGEMMSmall<archType>::template op<3, 3>
			};

			if (m <= 3 && n <= 3)
			{
				return (*fnTable[(m - 1) * 3 + (n - 1)])(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
			}

			if (n == 1 && ldc == 1)
			{
				if (m == 8)
				{
					return scatteredGEMV8x1<archType>(m, k, aBase, aIdx, aIdxScale, bBase + bIdx[0] * bIdxScale, c);
				}
				else
				{
					return scatteredGEMV<archType>(m, k, aBase, aIdx, aIdxScale, bBase + bIdx[0] * bIdxScale, c);
				}
			}

			if (m >= 4)
			{
				if (n == 2 && ldc == 2) return scatteredGEMV2<archType>(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
				if (n == 3 && ldc == 3) return scatteredGEMV3<archType>(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
				if (n == 4 && ldc == 4) return scatteredGEMV4<archType>(m, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c);
			}
			return scatteredGEMMBaseline<archType>(m, n, k, aBase, aIdx, aIdxScale, bBase, bIdx, bIdxScale, c, ldc);
		}

		// real implementations are in `archImpl/<name>.cpp`
	}
}
