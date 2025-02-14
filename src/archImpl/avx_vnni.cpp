#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::avx_vnni, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx_vnni, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx_vnni, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx_vnni, uint64_t, 8>;
	}
}
