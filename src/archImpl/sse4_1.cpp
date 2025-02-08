#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::sse4_1, uint8_t, 8>;
		template class SkipBigramModel<ArchType::sse4_1, uint16_t, 8>;
		template class SkipBigramModel<ArchType::sse4_1, uint32_t, 8>;
		template class SkipBigramModel<ArchType::sse4_1, uint64_t, 8>;
	}
}
