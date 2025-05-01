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

		template<ArchType archType>
		void invNormS8(
			size_t m, size_t k,
			const int8_t* a, size_t lda,
			float* out
		);

		template<ArchType archType>
		void invNormU8(
			size_t m, size_t k,
			const uint8_t* a, size_t lda,
			float* out
		);

		template<ArchType archType>
		void gemv(
			size_t m, size_t k,
			const uint8_t* a,
			const int8_t* b, size_t ldb,
			float* c
		);

		template<ArchType archType>
		void gemvS8S8(
			size_t m, size_t k,
			const int8_t* a,
			const int8_t* b, size_t ldb,
			float* c
		);

		template<ArchType archType>
		void gemvU8U8(
			size_t m, size_t k,
			const uint8_t* a,
			const uint8_t* b, size_t ldb,
			float* c
		);

		template<ArchType archType>
		float dotS8S8(
			size_t k,
			const int8_t* a,
			const int8_t* b
		);

		template<ArchType archType>
		float dotU8U8(
			size_t k,
			const uint8_t* a,
			const uint8_t* b
		);

		template<ArchType archType>
		float requantizePackedU4(
			size_t n,
			size_t qgroup,
			const uint8_t* packedInput,
			const uint8_t* localScale,
			float globalScale,
			bool toUint8,
			uint8_t* out
		);
	}
}
