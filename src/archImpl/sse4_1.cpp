#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace sb
	{
		template<>
		struct LogExpSum<ArchType::sse4_1>
		{
			template<size_t size>
			float operator()(const float* arr, std::integral_constant<size_t, size>)
			{
				return logExpSumImpl<ArchType::sse4_1, size>(arr);
			}
		};

		template class SkipBigramModel<ArchType::sse4_1, uint8_t, 8>;
		template class SkipBigramModel<ArchType::sse4_1, uint16_t, 8>;
		template class SkipBigramModel<ArchType::sse4_1, uint32_t, 8>;
		template class SkipBigramModel<ArchType::sse4_1, uint64_t, 8>;
	}
}
