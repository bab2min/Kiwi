#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::neon, uint8_t, 8>;
		template class SkipBigramModel<ArchType::neon, uint16_t, 8>;
		template class SkipBigramModel<ArchType::neon, uint32_t, 8>;
		template class SkipBigramModel<ArchType::neon, uint64_t, 8>;
	}
}
