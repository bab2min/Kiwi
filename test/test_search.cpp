#include "gtest/gtest.h"
#include <algorithm>
#include <vector>

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

TEST(Search, SearchKVNone)
{
	// searchKV for none expects the layout built by prepareKV (keys in BST order followed by their values)
	for (size_t size : { 1, 2, 3, 7, 8, 100, 1000 })
	{
		std::vector<uint32_t> kv(size * 2);
		for (size_t i = 0; i < size; ++i)
		{
			kv[i] = (uint32_t)(i * 2 + 1);
			kv[size + i] = (uint32_t)(i + 1);
		}
		kiwi::Vector<uint8_t> tempBuf;
		nst::prepareKV<ArchType::none, uint32_t, uint32_t>(kv.data(), size, tempBuf);
		for (size_t i = 0; i < size; ++i)
		{
			EXPECT_EQ((nst::searchKV<ArchType::none, uint32_t, uint32_t, uint32_t>(kv.data(), size, (uint32_t)(i * 2 + 1))), i + 1);
			EXPECT_EQ((nst::searchKV<ArchType::none, uint32_t, uint32_t, uint32_t>(kv.data(), size, (uint32_t)(i * 2))), 0u);
		}
		std::vector<std::pair<uint32_t, uint32_t>> extracted;
		for (size_t i = 0; i < size; ++i)
		{
			extracted.emplace_back(nst::extractKV<ArchType::none, uint32_t, uint32_t>(kv.data(), size, i));
		}
		std::sort(extracted.begin(), extracted.end());
		for (size_t i = 0; i < size; ++i)
		{
			EXPECT_EQ(extracted[i].first, (uint32_t)(i * 2 + 1));
			EXPECT_EQ(extracted[i].second, (uint32_t)(i + 1));
		}
	}
}
