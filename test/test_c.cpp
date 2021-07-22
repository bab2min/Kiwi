#include "gtest/gtest.h"
#include <cstring>
#include <kiwi/capi.h>
#include "common.h"

TEST(KiwiC, InitClose) 
{
	kiwi_h kw = kiwi_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_close(kw), 0);
}

TEST(KiwiC, BuilderInitClose)
{
	kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kb, nullptr);
	kiwi_h kw = kiwi_builder_build(kb);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_builder_close(kb), 0);
	EXPECT_EQ(kiwi_close(kw), 0);
}

TEST(KiwiC, BuilderAddWords)
{
	kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kb, nullptr);
	EXPECT_EQ(kiwi_builder_add_word(kb, KWORD8, "NNP", 0.0), 0);
	kiwi_h kw = kiwi_builder_build(kb);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_builder_close(kb), 0);
	
	kiwi_res_h res = kiwi_analyze(kw, KWORD8, 1, KIWI_MATCH_ALL);
	EXPECT_NE(res, nullptr);
	const char* word = kiwi_res_form(res, 0, 0);
	EXPECT_NE(word, nullptr);
	EXPECT_EQ(strcmp(word, KWORD8), 0);
	EXPECT_EQ(kiwi_res_close(res), 0);
	EXPECT_EQ(kiwi_close(kw), 0);
}

int mt_reader(int idx, char* buf, void* user)
{
	auto& data = *(std::vector<std::string>*)user;
	if (idx >= data.size()) return 0;
	if (buf == nullptr) return data[idx].size() + 1;
	std::memcpy(buf, data[idx].c_str(), data[idx].size() + 1);
	return 0;
}

int mt_receiver(int idx, kiwi_res_h res, void* user)
{
	kiwi_res_close(res);
	return 0;
}

TEST(KiwiC, AnalyzeMultithread)
{
	auto data = loadTestCorpus();
	kiwi_h kw = kiwi_init(MODEL_PATH, 2, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_analyze_m(kw, mt_reader, mt_receiver, &data, 1, KIWI_MATCH_ALL), data.size());
	EXPECT_EQ(kiwi_close(kw), 0);
}
