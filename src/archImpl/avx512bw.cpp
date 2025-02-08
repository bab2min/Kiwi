#include "../SkipBigramModelImpl.hpp"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::avx512bw, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx512bw, uint64_t, 8>;

		template class SkipBigramModel<ArchType::avx512vnni, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx512vnni, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx512vnni, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx512vnni, uint64_t, 8>;
	}
}
