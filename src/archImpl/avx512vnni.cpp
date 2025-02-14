#include "../SkipBigramModelImpl.hpp"
#include "../qgemm.h"

namespace kiwi
{
	namespace lm
	{
		template class SkipBigramModel<ArchType::avx512vnni, uint8_t, 8>;
		template class SkipBigramModel<ArchType::avx512vnni, uint16_t, 8>;
		template class SkipBigramModel<ArchType::avx512vnni, uint32_t, 8>;
		template class SkipBigramModel<ArchType::avx512vnni, uint64_t, 8>;
	}

	namespace qgemm
	{

	}
}
