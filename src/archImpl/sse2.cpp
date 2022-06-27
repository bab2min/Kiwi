#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace sb
	{
		template<>
		struct LogExpSum<ArchType::sse2>
		{
			template<size_t size>
			float operator()(const float* arr, std::integral_constant<size_t, size>)
			{
				return logExpSumImpl<ArchType::sse2, size>(arr);
			}
		};

		template class SkipBigramModel<ArchType::sse2, uint8_t, 8>;
		template class SkipBigramModel<ArchType::sse2, uint16_t, 8>;
		template class SkipBigramModel<ArchType::sse2, uint32_t, 8>;
		template class SkipBigramModel<ArchType::sse2, uint64_t, 8>;
	}
}
