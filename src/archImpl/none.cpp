#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace sb
	{
		template class SkipBigramModel<ArchType::none, uint8_t, 8>;
		template class SkipBigramModel<ArchType::none, uint16_t, 8>;
		template class SkipBigramModel<ArchType::none, uint32_t, 8>;
		template class SkipBigramModel<ArchType::none, uint64_t, 8>;

		template class SkipBigramModel<ArchType::balanced, uint8_t, 8>;
		template class SkipBigramModel<ArchType::balanced, uint16_t, 8>;
		template class SkipBigramModel<ArchType::balanced, uint32_t, 8>;
		template class SkipBigramModel<ArchType::balanced, uint64_t, 8>;
	}
}
