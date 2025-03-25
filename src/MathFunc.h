#pragma once

#include <kiwi/ArchUtils.h>

namespace kiwi
{
	namespace lm
	{
		template<ArchType archType>
		float logSumExp(const float* arr, size_t size);

		template<ArchType archType>
		void logSumExpTransposed(float* arr, size_t size, size_t batchSize, size_t stride);

		template<ArchType archType>
		void logSoftmax(float* arr, size_t size);

		template<ArchType archType>
		void logSoftmaxTransposed(float* arr, size_t size, size_t batchSize, size_t stride);
	}
}
