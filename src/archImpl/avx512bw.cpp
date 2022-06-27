#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace sb
	{
		template<>
		struct LogExpSum<ArchType::avx512bw>
		{
			template<size_t size>
			float operator()(const float* arr, std::integral_constant<size_t, size>)
			{
				return logExpSumImpl<ArchType::avx512bw, size>(arr);
			}
		};

		template class SkipBigramModel<ArchType::avx512bw, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint64_t, 8>;
	}
}
