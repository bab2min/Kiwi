#include "gtest/gtest.h"
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

#define KWORD u8"킼윜"

TEST(KiwiC, BuilderAddWords)
{
	kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kb, nullptr);
	EXPECT_EQ(kiwi_builder_add_word(kb, KWORD, "NNP", 0.0), 0);
	kiwi_h kw = kiwi_builder_build(kb);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_builder_close(kb), 0);
	
	kiwi_res_h res = kiwi_analyze(kw, KWORD, 1, KIWI_MATCH_ALL);
	EXPECT_NE(res, nullptr);
	const char* word = kiwi_res_form(res, 0, 0);
	EXPECT_NE(word, nullptr);
	EXPECT_EQ(strcmp(word, KWORD), 0);
	EXPECT_EQ(kiwi_res_close(res), 0);
	EXPECT_EQ(kiwi_close(kw), 0);
}

