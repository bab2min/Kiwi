#pragma once

#include <cmath>
#include "SkipBigramModel.hpp"
#include "SIMD.hpp"

namespace kiwi
{
	namespace sb
	{
		template<ArchType archType>
		struct LogExpSum
		{
			template<size_t size>
			float operator()(const float* arr, std::integral_constant<size_t, size>)
			{
				float maxValue = *std::max_element(arr, arr + size);
				float sum = 0;
				for (size_t i = 0; i < size; ++i)
				{
					sum += std::exp(arr[i] - maxValue);
				}
				return std::log(sum) + maxValue;
			}
		};

		template<ArchType archType, size_t size>
		float logExpSumImpl(const float* arr)
		{
			simd::Operator<archType> op;

			auto pmax = op.loadf(arr);
			for (size_t i = op.packetSize; i < size; i += op.packetSize)
			{
				pmax = op.maxf(pmax, op.loadf(&arr[i]));
			}
			pmax = op.redmaxbf(pmax);

			auto sum = op.zerof();
			for (size_t i = 0; i < size; i += op.packetSize)
			{
				sum = op.addf(sum, op.expf(op.subf(op.loadf(&arr[i]), pmax)));
			}
			return std::log(op.redsumf(sum)) + op.firstf(pmax);
		}

		template<ArchType arch, class KeyType, size_t windowSize>
		float SkipBigramModel<arch, KeyType, windowSize>::evaluate(const KeyType* history, size_t cnt, KeyType next, float base) const
		{
			if (!cnt) return base;
			if (!vocabValidness[next]) return base;

#if defined(__GNUC__) && __GNUC__ < 5
			alignas(256) float arr[windowSize * 2];
#else
			alignas(ArchInfo<arch>::alignment) float arr[windowSize * 2];
#endif
			std::fill(arr, arr + windowSize, base);
			std::fill(arr + windowSize, arr + windowSize * 2, -INFINITY);

			size_t b = ptrs[next], e = ptrs[next + 1];
			size_t size = e - b;

			for (size_t i = 0; i < cnt; ++i)
			{
				arr[i] = discnts[history[i]] + base;
				float out;
				if (nst::search<arch>(&keyData[b], &compensations[b], size, history[i], out))
				{
					arr[i + windowSize] = out;
				}
			}
			return LogExpSum<arch>{}(arr, std::integral_constant<size_t, windowSize * 2>{}) - logWindowSize;
		}
	}
}
