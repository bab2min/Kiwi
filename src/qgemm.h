#pragma once
#include <cstdint>
#include <kiwi/ArchUtils.h>

namespace kiwi
{
	namespace qgemm
	{
		template<ArchType archType>
		int32_t dotprod(
			const uint8_t* a, const int8_t* b, size_t n
		);

		template<ArchType archType>
		void scatteredGEMM(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<ArchType archType>
		void scatteredGEMMBaseline(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);

		template<ArchType archType>
		void scatteredGEMMOpt(
			size_t m, size_t n, size_t k,
			const uint8_t* aBase, const int32_t* aIdx, size_t aIdxScale,
			const int8_t* bBase, const int32_t* bIdx, size_t bIdxScale,
			float* c, size_t ldc
		);
	}
}
