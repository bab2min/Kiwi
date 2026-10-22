#include "gtest/gtest.h"
#include <algorithm>

#include <kiwi/ArchUtils.h>
#include "../src/search.h"

using namespace kiwi;

TEST(Search, FindAllFullWidth)
{
	// all bytes match, so every bit must be set, also when size is the full width of size_t
	static constexpr size_t numBits = sizeof(size_t) * 8;
	uint8_t arr[64];
	std::fill(arr, arr + 64, 7);
#if defined(__x86_64__) || defined(_M_X64) || defined(KIWI_ARCH_X86) || defined(KIWI_ARCH_X86_64)
	EXPECT_EQ(nst::findAll<ArchType::sse2>(arr, numBits, 7), ~(size_t)0);
#endif
#if defined(KIWI_ARCH_ARM64) || defined(_M_ARM64)
	EXPECT_EQ(nst::findAll<ArchType::neon>(arr, numBits, 7), ~(size_t)0);
#endif
}
