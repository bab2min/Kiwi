#pragma once

#include <cmath>
#include "SkipBigramModel.hpp"
#include "MathFunc.hpp"

namespace kiwi
{
	namespace lm
	{
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
			return LogSumExp<arch>{}(arr, std::integral_constant<size_t, windowSize * 2>{}) - logWindowSize;
		}
	}
}
