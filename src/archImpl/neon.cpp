#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace sb
	{
		template<>
		struct LogExpSum<ArchType::neon>
		{
			template<size_t size>
			float operator()(const float* arr, std::integral_constant<size_t, size>)
			{
				return logExpSumImpl<ArchType::neon, size>(arr);
			}
		};

		template class SkipBigramModel<ArchType::neon, uint8_t, 8>;
		template class SkipBigramModel<ArchType::neon, uint16_t, 8>;
		template class SkipBigramModel<ArchType::neon, uint32_t, 8>;
		template class SkipBigramModel<ArchType::neon, uint64_t, 8>;
	}
}
