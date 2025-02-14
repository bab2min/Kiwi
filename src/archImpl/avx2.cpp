#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::avx2, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx2, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx2, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx2, uint64_t, 8>;
	}
}
